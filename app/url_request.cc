#include <unistd.h>
#include <string.h>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "url_log.h"
#include "url_mysql.h"
#include "url_bloom.h"
#include "url_request.h"
#include "url_threadpool.h"
#include "url_socket_epoll.h"

using namespace std;

extern CSocket m_socket;
extern string PATH;

BloomFilter surfilter("URLFilter.bf"); // 短链过滤器

pthread_mutex_t MimeType::lock = PTHREAD_MUTEX_INITIALIZER;
unordered_map<string, string> MimeType::mime;

ssize_t readn(int fd, void *buff, size_t n)
{
    size_t nleft = n;
    ssize_t nread = 0;
    ssize_t readsum = 0;
    char *ptr = (char *)buff;
    while (nleft > 0)
    {
        if ((nread = read(fd, ptr, nleft)) < 0)
        {
            if (errno == EINTR) //信号导致的中断
                nread = 0;
            else if (errno == EAGAIN) // 没有数据可读了
                return readsum;
            else
                return -1;
        }
        else if (nread == 0)
            return -2; // 对方关闭了连接
        readsum += nread;
        nleft -= nread;
        ptr += nread;
    }
    return readsum;
}

ssize_t writen(int fd, void *buff, size_t n)
{
    size_t nleft = n;
    ssize_t nwritten = 0;
    ssize_t writeSum = 0;
    char *ptr = (char *)buff;
    while (nleft > 0)
    {
        if ((nwritten = write(fd, ptr, nleft)) <= 0)
        {
            if (nwritten < 0)
            {
                if (errno == EINTR || errno == EAGAIN)
                {
                    nwritten = 0;
                    continue;
                }
                else
                    return -1;
            }
        }
        writeSum += nwritten;
        nleft -= nwritten;
        ptr += nwritten;
    }
    return writeSum;
}

string MimeType::getMime(const string &suffix)
{
    if (mime.size() == 0)
    {
        pthread_mutex_lock(&lock);
        if (mime.size() == 0)
        {
            mime[".html"] = "text/html";
            mime[".avi"] = "video/x-msvideo";
            mime[".bmp"] = "image/bmp";
            mime[".c"] = "text/plain";
            mime[".doc"] = "application/msword";
            mime[".gif"] = "image/gif";
            mime[".gz"] = "application/x-gzip";
            mime[".htm"] = "text/html";
            mime[".ico"] = "application/x-ico";
            mime[".jpg"] = "image/jpeg";
            mime[".png"] = "image/png";
            mime[".txt"] = "text/plain";
            mime[".mp3"] = "audio/mp3";
            mime["default"] = "text/html";
        }
        pthread_mutex_unlock(&lock);
    }
    if (mime.count(suffix) != 0)
        return mime[suffix];
    return mime["default"];
}

requestData::requestData(lpurl_connect_t c, string path) : c(c), now_read_pos(0), state(STATE_PARSE_URI), h_state(h_start),
                                                           keep_alive(false), path(path)
{
    fd = c->fd; //赋值，不是初始化
}

requestData::~requestData()
{
}

void requestData::handleRequest()
{
    char buff[MAX_BUFF];
    bool isError = false;
    while (true)
    {
        int n = readn(fd, buff, MAX_BUFF);
        if (n == -1)
        {
            ngx_log_error_core(NGX_LOG_ERR, errno, "handleRequest read 出错");
            isError = true;
            break;
        }
        else if (n == -2)
        {
            ngx_log_error_core(NGX_LOG_INFO, 0, "对端关闭了连接");
            isError = true;
            break;
        }
        else
            ;
        string now_read(buff, buff + n);
        content += now_read;

        // cout << content << endl;

        if (state == STATE_PARSE_URI)
        {
            int flag = this->parse_URI();
            if (flag == PARSE_URI_AGAIN)
                break;
            else if (flag == PARSE_URI_ERROR)
            {
                ngx_log_error_core(NGX_LOG_INFO, 0, "PARSE_URI_ERROR");
                isError = true;
                break;
            }
        }
        if (state == STATE_PARSE_HEADERS)
        {
            int flag = this->parse_Headers();
            // 输出heard
            // for (auto iter = headers.begin(); iter != headers.end(); iter++)
            // {
            //     cout << iter->first << ":" << iter->second << endl;
            // }

            // example：
            // Accept-Encoding:gzip, deflate
            // Connection:keep-alive
            // Cache-Control:max-age=0
            // Accept-Language:zh-CN,zh;q=0.9,en;q=0.8
            // Upgrade-Insecure-Requests:1
            // User-Agent:Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.198 Safari/537.36
            // Host:applestar.xyz:8080
            // Accept:text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9

            if (flag == PARSE_HEADER_AGAIN)
            {
                break;
            }
            else if (flag == PARSE_HEADER_ERROR)
            {
                ngx_log_error_core(NGX_LOG_INFO, 0, "PARSE_HEADER_ERROR");
                isError = true;
                break;
            }
            if (method == METHOD_POST)
            {
                state = STATE_RECV_BODY;
            }
            else
            {
                state = STATE_ANALYSIS;
            }
        }

        if (state == STATE_RECV_BODY)
        {
            int content_length = -1;
            if (headers.find("Content-Length") != headers.end())
                content_length = stoi(headers["Content-Length"]);
            else
            {
                isError = true;
                break;
            }
            if (content.size() < content_length)
                continue;
            state = STATE_ANALYSIS;
        }
        if (state == STATE_ANALYSIS)
        {
            int flag = this->analysisRequest();
            if (flag < 0)
            {
                isError = true;
                break;
            }
            else if (flag == ANALYSIS_SUCCESS)
            {
                state = STATE_FINISH;
                break;
            }
            else
            {
                isError = true;
                break;
            }
        }
    }
    if (isError)
    {
        ngx_log_error_core(NGX_LOG_INFO, 0, "handleRequest ERROR");
        return;
    }
}

int requestData::parse_URI()
{
    string &str = content;
    int pos = str.find('\r', now_read_pos);
    if (pos < 0)
        return PARSE_URI_AGAIN;
    string request_line = str.substr(0, pos);

    if (str.size() > pos + 1)
        str = str.substr(pos + 1);
    else
        str.clear();

    //Method
    pos = request_line.find("GET");
    if (pos < 0)
    {
        pos = request_line.find("POST");
        if (pos < 0)
            return PARSE_URI_ERROR;
        else
            method = METHOD_POST;
    }
    else
        method = METHOD_GET;
    pos = request_line.find('/', pos);
    if (pos < 0)
        return PARSE_URI_ERROR;
    else
    {
        int _pos = request_line.find(' ', pos);
        if (_pos < 0)
            return PARSE_URI_ERROR;
        else
        {
            if (_pos - pos > 1)
            {
                file_name = request_line.substr(pos + 1, _pos - pos - 1);
                int __pos = file_name.find('?');
                if (__pos >= 0)
                    file_name = file_name.substr(0, __pos);
            }
            else
                file_name = "index.html";
        }
        pos = _pos;
    }
    // HTTP 版本号
    pos = request_line.find("/", pos);
    if (pos < 0)
    {
        return PARSE_URI_ERROR;
    }
    else
    {
        if (request_line.size() - pos <= 3)
            return PARSE_URI_ERROR;
        else
        {
            string ver = request_line.substr(pos + 1, 3);
            if (ver == "1.0")
                HTTPversion = HTTP_10;
            else if (ver == "1.1")
                HTTPversion = HTTP_11;
            else
                return PARSE_URI_ERROR;
        }
    }
    state = STATE_PARSE_HEADERS;
    return PARSE_URI_SUCCESS;
}

int requestData::parse_Headers()
{
    string &str = content;

    // cout<<"parse_headers:"<<endl;
    // cout<<str<<endl;

    int key_start = -1, key_end = -1, value_start = -1, value_end = -1;
    int now_read_line_begin = 0;
    bool notFinish = true;
    for (int i = 0; i < str.size() && notFinish; ++i)
    {
        switch (h_state)
        {
        case h_start:
        {
            if (str[i] == '\n' || str[i] == '\r')
                break;
            h_state = h_key;
            key_start = i;
            now_read_line_begin = i;
            break;
        }
        case h_key:
        {
            if (str[i] == ':')
            {
                key_end = i;
                if (key_end - key_start <= 0)
                    return PARSE_HEADER_ERROR;
                h_state = h_colon;
            }
            else if (str[i] == '\n' || str[i] == '\r')
                return PARSE_HEADER_ERROR;
            break;
        }
        case h_colon:
        {
            if (str[i] == ' ')
            {
                h_state = h_spaces_after_colon;
            }
            else
                return PARSE_HEADER_ERROR;
            break;
        }
        case h_spaces_after_colon:
        {
            h_state = h_value;
            value_start = i;
            break;
        }
        case h_value:
        {
            if (str[i] == '\r')
            {
                h_state = h_CR;
                value_end = i;
                if (value_end - value_start <= 0)
                    return PARSE_HEADER_ERROR;
            }
            else if (i - value_start > 1024)
                return PARSE_HEADER_ERROR;
            break;
        }
        case h_CR:
        {
            if (str[i] == '\n')
            {
                h_state = h_LF;
                string key(str.begin() + key_start, str.begin() + key_end);
                string value(str.begin() + value_start, str.begin() + value_end);
                headers[key] = value;
                now_read_line_begin = i;
            }
            else
                return PARSE_HEADER_ERROR;
            break;
        }
        case h_LF:
        {
            if (str[i] == '\r')
            {
                h_state = h_end_CR;
            }
            else
            {
                key_start = i;
                h_state = h_key;
            }
            break;
        }
        case h_end_CR:
        {
            if (str[i] == '\n')
            {
                h_state = h_end_LF;
            }
            else
                return PARSE_HEADER_ERROR;
            break;
        }
        case h_end_LF:
        {
            notFinish = false;
            key_start = i;
            now_read_line_begin = i;
            break;
        }
        }
    }
    if (h_state == h_end_LF)
    {
        str = str.substr(now_read_line_begin);
        return PARSE_HEADER_SUCCESS;
    }
    str = str.substr(now_read_line_begin);
    return PARSE_HEADER_AGAIN;
}

int requestData::analysisRequest()
{
    if (method == METHOD_POST)
    {
        char header[MAX_BUFF];
        sprintf(header, "HTTP/1.1 %d %s\r\n", 200, "OK");
        // cout << "POST content:" << content << endl;
        int pos = content.find("=");
        if (pos < 0)
        {
            perror("post not find =");
            return ANALYSIS_ERROR;
        }

        string url, defsuffix;
        string short_url;
        int _pos = content.rfind("&");
        if (_pos > 0 && content.substr(_pos + 1, 6) == "defurl")
        {
            url = content.substr(pos + 1, _pos - pos - 1);
            pos = content.find("=", _pos);
            defsuffix = content.substr(pos + 1);
            // cout << "defsuffix:" << defsuffix << endl;
            //添加重复判断
            MyDB *mysql = MyDB::Getinstance();
            if (mysql->find_lurl_SQL(defsuffix) == "-1")
            {
                short_url = "http://www.applestar.xyz/" + defsuffix;
                mysql->add_surl_SQL(defsuffix, url);
                surfilter.insert(defsuffix);
            }
            else
            {
                short_url = "Please change to another id";
            }
        }
        else
        {
            url = content.substr(pos + 1);
            // 添加hash计算
            auto x = MurmurHash2(url.c_str(), url.size(), 97);
            string short_id = to_62(x);

            MyDB *mysql = MyDB::Getinstance();

            if (surfilter.contains(short_id))
            {
                // 要么是添加过了的长链，要么是hash碰撞，MySQL一查便知
                if (mysql->find_lurl_SQL(short_id) == "-1") // 没查到，hash碰撞了或者布隆过滤器太满了;可以用while，但是感觉没有必要，如果布隆过滤器满了，在这里一直循环就影响到正常的用户了，得不偿失
                {
                    // +自定义字符串之后重新hash，后面需要添加判断条件把新加的字符串去掉
                    // url = "&" + url;
                    // x = MurmurHash2(url.c_str(), url.size(), 97);
                    // short_id = to_62(x);
                }

                ngx_log_error_core(6, 0, "%s 判定存在", short_id.c_str());
                short_url = "http://www.applestar.xyz/" + short_id;
            }
            else
            {
                short_url = "http://www.applestar.xyz/" + short_id;
                // 添加到数据库中
                mysql->add_surl_SQL(short_id, url);
                ngx_log_error_core(6, 0, "短链： %s URL:%s", short_id.c_str(), url.c_str());
                surfilter.insert(short_id);
            }
        }

        char *send_content = const_cast<char *>(short_url.c_str());

        sprintf(header, "%sContent-length: %zu\r\n", header, strlen(send_content));
        sprintf(header, "%s\r\n", header);
        size_t send_len = (size_t)writen(fd, header, strlen(header));
        if (send_len != strlen(header))
        {
            perror("Send header failed");
            return ANALYSIS_ERROR;
        }

        send_len = (size_t)writen(fd, send_content, strlen(send_content));
        if (send_len != strlen(send_content))
        {
            perror("Send content failed");
            return ANALYSIS_ERROR;
        }
        return ANALYSIS_SUCCESS;
    }

    else if (method == METHOD_GET)
    {
        char header[MAX_BUFF];
        sprintf(header, "HTTP/1.1 %d %s\r\n", 200, "OK");

        int dot_pos = file_name.find('.');
        string str_ftype;
        if (dot_pos < 0)
        {
            str_ftype = MimeType::getMime("default");
            MyDB *mysql = MyDB::Getinstance();
            if (surfilter.contains(file_name))
            {
                string url = mysql->find_lurl_SQL(file_name);
                if (url != "-1")
                {
                    handle30x(302, "Moved Temporarily", url);
                    return ANALYSIS_SUCCESS;
                }
            }
        }
        else
            str_ftype = MimeType::getMime(file_name.substr(dot_pos));

        const char *filetype = str_ftype.c_str();

        file_name.insert(0, PATH);
        struct stat sbuf;
        if (stat(file_name.c_str(), &sbuf) < 0)
        {
            handleError(404, "Not Found");
            // handle30x(fd, 301, "Moved Permanently");
            // handle30x(302, "Moved Temporarily", "http://applestar.xyz");
            return ANALYSIS_ERROR;
        }

        sprintf(header, "%sContent-type: %s\r\n", header, filetype);
        // 通过Content-length返回文件大小
        sprintf(header, "%sContent-length: %ld\r\n", header, sbuf.st_size);

        sprintf(header, "%s\r\n", header);

        size_t send_len = (size_t)writen(fd, header, strlen(header));
        if (send_len != strlen(header))
        {
            ngx_log_error_core(NGX_LOG_ERR, errno, "Send header failed");
            return ANALYSIS_ERROR;
        }
        int src_fd = open(file_name.c_str(), O_RDONLY, 0);
        char *src_addr = static_cast<char *>(mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0));
        close(src_fd);

        // 发送文件并校验完整性
        send_len = writen(fd, src_addr, sbuf.st_size);
        if (send_len != sbuf.st_size)
        {
            perror("Send file failed");
            return ANALYSIS_ERROR;
        }
        munmap(src_addr, sbuf.st_size);

        return ANALYSIS_SUCCESS;
    }
    else
        return ANALYSIS_ERROR;
}

// <html>
// <head><title>404 Not Found</title></head>
// <body bgcolor="white">
// <center><h1>404 Not Found</h1></center>
// <hr><center>nginx/1.14.0 (Ubuntu)</center>
// </body>
// </html>

void requestData::handleError(int errNum, string msg)
{
    msg = " " + msg;
    char send_buff[MAX_BUFF];
    string body_buff, header_buff;
    body_buff += "<html><title>Error</title>";
    body_buff += "<body bgcolor=\"white\">";
    body_buff += "<center><h1>" + to_string(errNum) + msg + "</h1></center>";
    body_buff += "<hr><center>Short URL Server (Ubuntu)</center>\n</body></html>";

    header_buff += "HTTP/1.1 " + to_string(errNum) + msg + "\r\n";
    header_buff += "Content-type: text/html\r\n";
    header_buff += "Connection: close\r\n";
    header_buff += "Content-length: " + to_string(body_buff.size()) + "\r\n";
    header_buff += "\r\n";
    sprintf(send_buff, "%s", header_buff.c_str());
    writen(fd, send_buff, strlen(send_buff));
    sprintf(send_buff, "%s", body_buff.c_str());
    writen(fd, send_buff, strlen(send_buff));
}

void requestData::handle30x(int errNum, string msg, string url)
{
    string header_buff;

    // header_buff += "HTTP/1.1 302 Moved Temporarily\r\n";
    header_buff += "HTTP/1.1 " + to_string(errNum) + msg + "\r\n";
    header_buff += "Location: " + url + "\r\n";
    // header_buff += "Location: http://www.qq.com\r\n";
    // header_buff += "Connection: close\r\n";
    // header_buff += "Content-length: " + to_string(body_buff.size()) + "\r\n";
    header_buff += "\r\n";

    char send_buff[MAX_BUFF];

    sprintf(send_buff, "%s", header_buff.c_str());
    writen(fd, send_buff, strlen(send_buff));
}