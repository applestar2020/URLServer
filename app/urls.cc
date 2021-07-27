// 程序入口函数


#include <string>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <sys/stat.h>



#include "url_cfg.h"
#include "url_log.h"
#include "url_deamon.h"
#include "url_threadpool.h"
#include "url_socket_epoll.h"

using namespace std;

// 全局对象
string PATH = "www/";
CSocket m_socket;
Threadpool threadpool;

void signalHandler( int signum );


int main()
{
    int ret = 0;
    cout << "--------------begin-----------" << endl;
    // 读取配置文件
    CConfig *p_config = CConfig::GetInstance();

    // 日志文件初始化
    ngx_log_init(stoi(p_config->get_item("append", "0")));
    ngx_log_error_core(6, 0, "--------------sever start--------------");

    // 守护进程，默认是
    if (stoi(p_config->get_item("deamon", "1")))
    {
        ret = url_daemon();
        if (ret == -1)
        {
            //非正常退出
            return -1;
        }
        else if (ret == 1)
        {
            return 1;
        }
    }

    // 主进程
    pid_t pid = getpid();
    pid_t ppid = getppid();
    ngx_log_error_core(6, 0, "pid=%d", pid);
    ngx_log_error_core(6, 0, "ppid=%d", ppid);

    // 处理退出的信号
    signal(SIGINT, signalHandler);  
    signal(SIGTERM, signalHandler);  

    // 创建线程池
    int threadnum = stoi(p_config->get_item("threadnum", "4"));
    threadpool.addThread(threadnum);
    ngx_log_error_core(NGX_LOG_INFO, 0, "线程数量：%d ；当前空闲线程数量：%d ", threadpool.thrCount(), threadpool.idlCount());

    ret = m_socket.Initialize();
    if (ret < 0)
    {
        ngx_log_stderr(errno, "socket初始化失败");
        return ret;
    }
    ret = m_socket.url_epoll_init();
    if (ret < 0)
    {
        ngx_log_stderr(errno, "epoll初始化失败");
        return ret;
    }

    while (1)
    {
        m_socket.url_epoll_wait(-1);
    }

    return 0;
}


void signalHandler( int signum )
{
    ngx_log_error_core(1, 0, "Interrupt signal %d received", signum);
 
    // 清理并关闭
    // 终止程序  
 
   exit(signum); 
}
 

