#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory>

#include "url_cfg.h"
#include "url_log.h"
#include "url_request.h"
#include "url_threadpool.h"
#include "url_socket_epoll.h"

extern string PATH;
extern CSocket m_socket;
extern Threadpool threadpool;

CSocket::CSocket() : m_worker_connections(1), m_ListenPortCount(1), m_epollhandle(-1),
                     m_pconnections(nullptr), m_pfree_connections(nullptr), m_connection_n(0), m_free_connection_n(0)
{
}

// 析构函数
CSocket::~CSocket()
{
    for (auto iter = m_ListenSocketList.begin(); iter != m_ListenSocketList.end(); iter++)
    {
        delete (*iter);
    }
    m_ListenSocketList.clear();

    if (m_pconnections)
        delete[] m_pconnections;
}

int CSocket::Initialize()
{
    CConfig *p_config = CConfig::GetInstance();
    m_worker_connections = stoi(p_config->get_item("worker_connections", "1024")); //epoll连接的最大项数
    m_ListenPortCount = stoi(p_config->get_item("ListenPortCount", "1"));          //取得要监听的端口数量

    for (int i = 0; i < m_ListenPortCount; i++)
    {
        string sport = "listenport" + to_string(i + 1);
        int port = stoi(p_config->get_item(sport, "80"));
        int listen_fd = CSocket::socket_bind_listen(port);

        setSocketNonBlocking(listen_fd);

        if (listen_fd < 0)
        {
            ngx_log_stderr(errno, "CSocekt::Initialize()中监听%d端口失败", port);
            return false;
        }

        lpurl_listen_t p = new url_listen_s;
        memset(p, 0, sizeof(url_listen_s));
        p->fd = listen_fd;
        p->port = port;
        ngx_log_error_core(NGX_LOG_INFO, 0, "监听%d端口成功!", port); //显示一些信息到日志中
        m_ListenSocketList.push_back(p);                              //加入到队列中
    }
    if (m_ListenSocketList.empty())
        return -1;

    return 0;
}

//--------------------------------------------------------------------
//socket相关
int CSocket::socket_bind_listen(int port)
{
    //检查端口是否在正确的区间
    if (port < 0 || port > 65535)
        port = 80;

    // 创建TCP套接字
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1)
        return -1;

    // 解决 Address already in use 错误
    int one = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1)
        return -1;

    // 同样的方式可以设置nagle算法

    // 绑定IP和端口
    struct sockaddr_in server_addr;
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((unsigned int)port);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        return -1;

    // 开始监听
    if (listen(listen_fd, URL_LISTEN_BACKLOG) == -1)
        return -1;

    return listen_fd;
}

int CSocket::setSocketNonBlocking(int fd)
{
    int flag = fcntl(fd, F_GETFL, 0);
    if (flag == -1)
        return -1;

    flag |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flag) == -1)
        return -1;
    return 0;
}

//--------------------------------------------------------------------
//epoll相关
int CSocket::url_epoll_init()
{
    m_epollhandle = epoll_create(m_worker_connections);
    if (m_epollhandle < 0)
    {
        ngx_log_stderr(errno, "epoll_create()失败.");
        return -1;
    }

    m_connection_n = m_worker_connections;
    m_pconnections = new url_connect_s[m_connection_n];

    // 构建单向链表
    lpurl_connect_t next = nullptr;
    lpurl_connect_t c = m_pconnections;
    for (int i = m_connection_n - 1; i >= 0; i--)
    {
        c[i].data = next;
        c[i].fd = -1;
        next = &c[i];
    }

    m_pfree_connections = next;           // 空闲连接目前也指向首地址，
    m_free_connection_n = m_connection_n; // 目前都是空闲连接

    // 遍历监听端口并将其对应的套接字添加到epoll中进行监听
    for (auto &p_listen_t : m_ListenSocketList)
    {
        int fd = p_listen_t->fd;

        c = url_get_connection(fd);
        c->listening = p_listen_t;
        p_listen_t->connect = c;
        c->rhandle = &CSocket::url_event_accept;

        // 往epoll中添加监听事件
        __uint32_t event = EPOLLIN | EPOLLET;
        url_epoll_add(fd, event, c);
    }
    return 0;
}

int CSocket::url_epoll_add(int fd, __uint32_t events, lpurl_connect_t c)
{
    struct epoll_event event;
    event.data.ptr = c;
    event.events = events;
    if (epoll_ctl(m_epollhandle, EPOLL_CTL_ADD, fd, &event) < 0)
    {
        ngx_log_stderr(errno, "epoll add eeeor");
        return -1;
    }
    return 0;
}

int CSocket::url_epoll_del(int fd, __uint32_t events, lpurl_connect_t c)
{
    struct epoll_event event;
    // event.data.ptr = c;  // DEL的时候只需要提供fd就可以了吧，按理说是这样
    event.events = events;
    if (epoll_ctl(m_epollhandle, EPOLL_CTL_DEL, fd, &event) < 0)
    {
        perror("epoll del eeeor");
        return -1;
    }
    return 0;
}

int CSocket::url_epoll_mod(int fd, __uint32_t events, lpurl_connect_t c)
{
    struct epoll_event event;
    event.data.ptr = c;
    event.events = events;
    if (epoll_ctl(m_epollhandle, EPOLL_CTL_MOD, fd, &event) < 0)
    {
        perror("epoll mod error");
        return -1;
    }
    return 0;
}

int CSocket::url_epoll_wait(int timer)
{
    int event_num = epoll_wait(m_epollhandle, m_events, URL_MAX_EVENTS, timer);

    if (event_num == -1)
    {
        if (errno == EINTR)
        {
            //信号所致，直接返回
            ngx_log_error_core(NGX_LOG_INFO, errno, "CSocekt::ngx_epoll_process_events()中epoll_wait()失败!");
            return 0; //正常返回
        }
        else
        {
            ngx_log_error_core(NGX_LOG_ALERT, errno, "CSocekt::ngx_epoll_process_events()中epoll_wait()失败!");
            return -1; //非正常返回
        }
    }

    if (event_num == 0) //超时，但没事件来
    {
        if (timer != -1)
        {
            //要求epoll_wait阻塞一定的时间而不是一直阻塞，这属于阻塞到时间了，则正常返回
            return 0;
        }
        //无限等待【所以不存在超时】，但却没返回任何事件，这应该不正常有问题
        ngx_log_error_core(NGX_LOG_ALERT, 0, "CSocekt::ngx_epoll_process_events()中epoll_wait()没超时却没返回任何事件!");
        return -1; //非正常返回
    }

    // 正常收到
    ngx_log_error_core(NGX_LOG_INFO, 0, "收到了%d个事件", event_num);
    for (int i = 0; i < event_num; i++)
    {
        lpurl_connect_t c = (lpurl_connect_t)(m_events[i].data.ptr);

        uint32_t revents = m_events[i].events; //取出事件类型

        struct in_addr in = (c->c_sockaddr).sin_addr;
        char str[INET_ADDRSTRLEN]; //INET_ADDRSTRLEN这个宏系统默认定义 16
        //成功的话此时IP地址保存在str字符串中。
        inet_ntop(AF_INET, &in, str, sizeof(str));

        if (revents & (EPOLLERR | EPOLLHUP)) //例如对方close掉套接字或者错误发生
        {
            ngx_log_error_core(NGX_LOG_INFO, 0, "客户端%s:%d 已关闭", str, (c->c_sockaddr).sin_port);
        }

        if (revents & EPOLLIN)
        {
            (this->*(c->rhandle))(c);
        }

        if (revents & EPOLLOUT) //如果是写事件
        {
            //....待扩展

            ngx_log_stderr(errno, "111111111111111111111111111111.");
        }
    }

    return 1;
}

//--------------------------------------------------------------------
//业务处理相关

//接收监听套接字的新连接
void CSocket::url_event_accept(lpurl_connect_t oldc)
{
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_in));
    socklen_t client_addr_len = sizeof(client_addr);
    int accept_fd = 0;
    while ((accept_fd = accept4(oldc->fd, (struct sockaddr *)&client_addr, &client_addr_len, SOCK_NONBLOCK)) > 0)
    {
        struct in_addr in = client_addr.sin_addr;
        char str[INET_ADDRSTRLEN]; //INET_ADDRSTRLEN这个宏系统默认定义 16
        //成功的话此时IP地址保存在str字符串中。
        inet_ntop(AF_INET, &in, str, sizeof(str));
        ngx_log_error_core(NGX_LOG_INFO, 0, "监听端口：%d\tclient:%s:%d ", oldc->listening->port, str, client_addr.sin_port);

        lpurl_connect_t newc = url_get_connection(accept_fd);

        memcpy(&newc->c_sockaddr, &client_addr, client_addr_len);
        newc->listening = oldc->listening;
        __uint32_t _epo_event;
        if (oldc->listening->port == 8090)
        {
            _epo_event = EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP;
            newc->rhandle = &CSocket::tcp_request_handler;
        }
        else
        {
            _epo_event = EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP | EPOLLONESHOT;
            newc->rhandle = &CSocket::url_request_handler;
        }

        // 文件描述符可以读，边缘触发(Edge Triggered)模式，保证一个socket连接在任一时刻只被一个线程处理
        // __uint32_t _epo_event = EPOLLIN | EPOLLET | EPOLLONESHOT;

        url_epoll_add(accept_fd, _epo_event, newc);

        ngx_log_error_core(NGX_LOG_INFO, 0, "accept_fd:%d 加入了epoll中", accept_fd);
        // break;
    }
}

void tcp_handler(lpurl_connect_t c)
{
    // ngx_log_error_core(NGX_LOG_WARN, 0, "当前执行线程ID：%d ", pthread_self());

    char buff[MAX_BUFF];
    bool isError = false;
    while (true)
    {
        int n = readn(c->fd, buff, MAX_BUFF);
        if (n == -1)
        {
            ngx_log_error_core(NGX_LOG_ERR, errno, "TCP read 出错");
            isError = true;
            break;
        }
        else if (n == -2)
        {
            struct in_addr in = (c->c_sockaddr).sin_addr;
            char str[INET_ADDRSTRLEN]; //INET_ADDRSTRLEN这个宏系统默认定义 16
            //成功的话此时IP地址保存在str字符串中。
            inet_ntop(AF_INET, &in, str, sizeof(str));
            ngx_log_error_core(NGX_LOG_INFO, 0, "客户端 %s:%d 关闭了连接", str, (c->c_sockaddr).sin_port);
            isError = true;
            break;
        }
        else if (n == 0 && errno == EAGAIN)
            break;
        else
            ;
        if (writen(c->fd, buff, n) < 0)
        {
            ngx_log_error_core(NGX_LOG_INFO, 0, "writen error");
            isError = true;
            break;
        }
    }
    if (isError)
    {
        uint32_t _epo_event = EPOLLIN | EPOLLET;
        m_socket.url_epoll_del(c->fd, _epo_event, c);
        m_socket.url_free_close_connection(c);
    }
}

void CSocket::tcp_request_handler(lpurl_connect_t c)
{
    threadpool.commit(tcp_handler, c);
    // tcp_handler(c);
}

void url_handler(lpurl_connect_t c)
{
    // ngx_log_error_core(NGX_LOG_WARN, 0, "当前执行线程ID：%d ", pthread_self());

    struct in_addr in = (c->c_sockaddr).sin_addr;
    char str[INET_ADDRSTRLEN]; //INET_ADDRSTRLEN这个宏系统默认定义 16
    //成功的话此时IP地址保存在str字符串中。
    inet_ntop(AF_INET, &in, str, sizeof(str));
    ngx_log_error_core(NGX_LOG_INFO, 0, "收到了客户端 %s:%d 的HTTP请求", str, (c->c_sockaddr).sin_port);

    shared_ptr<requestData> req(new requestData(c, PATH));
    req->handleRequest();
    //请求处理完毕，将其从epoll中删除
    uint32_t _epo_event = EPOLLIN | EPOLLET | EPOLLONESHOT;
    m_socket.url_epoll_del(c->fd, _epo_event, c);
    m_socket.url_free_close_connection(c);
}

void CSocket::url_request_handler(lpurl_connect_t c)
{
    threadpool.commit(url_handler, c);
    // url_handler(c);
}

//--------------------------------------------------------------------
//连接（池）相关
// 从空闲连接从获取一个连接
lpurl_connect_t CSocket::url_get_connection(int fd)
{
    lpurl_connect_t c = m_pfree_connections;
    c->fd = fd;
    m_pfree_connections = c->data;
    --m_free_connection_n;
    return c;
}

//归还参数c所代表的连接到到连接池中，注意参数类型是lpngx_connection_t
void CSocket::url_free_connection(lpurl_connect_t c)
{
    c->data = m_pfree_connections;
    m_pfree_connections = c;
    ++m_free_connection_n;
}

// 处理完成之后释放连接并关闭套接字
void CSocket::url_free_close_connection(lpurl_connect_t c)
{
    int fd = c->fd;
    url_free_connection(c);
    c->fd = -1;
    close(fd);
    // ngx_log_error_core(NGX_LOG_INFO, 0, "fd:%d 已关闭", fd);
}