#ifndef _URL_REQUEST_H__
#define _URL_REQUEST_H__

// refrence: linyacool

#include <unordered_map>

#include "url_socket_epoll.h"

using namespace std;

const int STATE_PARSE_URI = 1;
const int STATE_PARSE_HEADERS = 2;
const int STATE_RECV_BODY = 3;
const int STATE_ANALYSIS = 4;
const int STATE_FINISH = 5;

const int MAX_BUFF = 4096;

const int PARSE_URI_AGAIN = -1;
const int PARSE_URI_ERROR = -2;
const int PARSE_URI_SUCCESS = 0;

const int PARSE_HEADER_AGAIN = -1;
const int PARSE_HEADER_ERROR = -2;
const int PARSE_HEADER_SUCCESS = 0;

const int ANALYSIS_ERROR = -2;
const int ANALYSIS_SUCCESS = 0;

const int METHOD_POST = 1;
const int METHOD_GET = 2;
const int HTTP_10 = 1;
const int HTTP_11 = 2;

ssize_t readn(int fd, void *buff, size_t n);
ssize_t writen(int fd, void *buff, size_t n);

class MimeType
{
private:
    static pthread_mutex_t lock;
    static std::unordered_map<std::string, std::string> mime;
    MimeType();
    MimeType(const MimeType &m);

public:
    static std::string getMime(const std::string &suffix);
};

enum HeadersState
{
    h_start = 0,
    h_key,
    h_colon,
    h_spaces_after_colon,
    h_value,
    h_CR,
    h_LF,
    h_end_CR,
    h_end_LF
};

class requestData
{
private:
    int epollfd;
    int fd;
    int method;
    int HTTPversion;
    int now_read_pos;
    int state;
    int h_state;
    bool isfinish;
    bool keep_alive;
    string path;
    string content;
    string file_name;
    unordered_map<string, string> headers;

    lpurl_connect_t c;

private:
    int parse_URI();
    int parse_Headers();
    int analysisRequest();

public:
    requestData(lpurl_connect_t c, string _path);
    ~requestData();
    int getFd();
    void setFd(int _fd);
    void handleRequest();
    void handleError(int errNum, string msg);

    void handle30x(int errNum, string msg, string url);
};

#endif
