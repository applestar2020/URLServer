#ifndef __URL_SOCKET_EPOLL_
#define __URL_SOCKET_EPOLL_

#include <vector>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>

// 宏定义
#define URL_LISTEN_BACKLOG 1024
#define URL_MAX_EVENTS 1024

typedef struct url_listen_s url_listen_t, *lpurl_listen_t;
typedef struct url_connect_s url_connect_t, *lpurl_connect_t;
class CSocket;

typedef void (CSocket::*url_event_handler_ptr)(lpurl_connect_t c);

// 与监听端口有关的结构，每个监听套接字都为其分配一个url_connect_s结构的连接对象
struct url_listen_s
{
    int port=-1;
    int fd;
    lpurl_connect_t connect;
};

// 表示一个连接的TCP结构体，作为epoll中data.ptr的传入数据结构
struct url_connect_s
{
    int fd;                   // 套接字句柄
    lpurl_listen_t listening; // 对应的监听套接字

    struct sockaddr_in c_sockaddr; // 对端的地址信息

    url_event_handler_ptr rhandle; // 对应读事件处理函数指针
    url_event_handler_ptr whandle; // 对应写事件处理函数指针

    lpurl_connect_t data; // 指向下一个连接对象，构成单向链表
};

// SOCKET相关类，全局只创建一个
class CSocket
{
public:
    CSocket();  //构造函数
    ~CSocket(); //释放函数
public:
    int Initialize(); //初始化函数

public:
    int url_epoll_init();                                            //epoll功能初始化
    int url_epoll_add(int fd, __uint32_t events, lpurl_connect_t c); //epoll增加事件
    int url_epoll_mod(int fd, __uint32_t events, lpurl_connect_t c); //epoll修改事件
    int url_epoll_del(int fd, __uint32_t events, lpurl_connect_t c); //epoll删除
    int url_epoll_wait(int timer);                                   //epoll等待接收和处理事件

    void url_free_close_connection(lpurl_connect_t c);

private:
    int socket_bind_listen(int port);
    void url_close_listening_sockets(); //关闭监听套接字
    int setSocketNonBlocking(int fd);   //设置非阻塞套接字

    //一些业务处理函数handler
    void url_event_accept(lpurl_connect_t oldc);      //建立新连接

    void url_request_handler(lpurl_connect_t c); //http业务处理句柄
    void tcp_request_handler(lpurl_connect_t c); //tcp业务处理句柄

    //连接池 或 连接 相关
    lpurl_connect_t url_get_connection(int isock); //从连接池中获取一个空闲连接
    void url_free_connection(lpurl_connect_t c);   //归还参数c所代表的连接到到连接池中

private:
    int m_worker_connections; //epoll连接的最大项数
    int m_ListenPortCount;    //所监听的端口数量
    int m_epollhandle;        //epoll_create返回的句柄

    //和连接池有关的
    lpurl_connect_t m_pconnections;      //注意这里可是个指针，其实这是个连接池的首地址
    lpurl_connect_t m_pfree_connections; //空闲连接链表头，连接池中总是有某些连接被占用，为了快速在池中找到一个空闲的连接，我把空闲的连接专门用该成员记录;
                                         //【串成一串，其实这里指向的都是m_pconnections连接池里的没有被使用的成员】
    int m_connection_n;                  //当前进程中所有连接对象的总数【连接池大小】
    int m_free_connection_n;             //连接池中可用连接总数

    std::vector<lpurl_listen_t> m_ListenSocketList; //监听套接字队列

    struct epoll_event m_events[URL_MAX_EVENTS]; //用于在epoll_wait()中承载返回的所发生的事
};

#endif