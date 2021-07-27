
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <iostream>

// #include <stdio.h>
// #include <stdlib.h>

#include "url_log.h"



#define NGX_ERROR -1
#define NGX_OK 0


// int ngx_demon()
// {
//     switch (fork())
//     {
//     case -1: // 创建子进程失败
//         return -1;
//     case 0: // 子进程，跳出往下执行
//         break;
//     default: // 父进程，不知道之前的日志文件描述符之类的是否需要关闭？
//         return 1;
//     }

//     // 子进程脱离终端，
//     if (setsid() == -1)
//     {
//         // 可以加日志输出
//         return -1;
//     }

//     // 文件权限给最大
//     umask(0);

//     close(STDIN_FILENO); //关闭和终端的联系，文件描述符
//     close(STDOUT_FILENO);
//     close(STDERR_FILENO);

//     return 0;
// }


int url_daemon()
{
    int  fd;

    switch (fork()) {
    case -1:
        ngx_log_error_core(NGX_LOG_EMERG, errno, "fork() failed");
        return NGX_ERROR;

    case 0:
        break;

    default:
        _exit(0);
    }

    // ngx_parent = ngx_pid;
    // ngx_pid = ngx_getpid();

    if (setsid() == -1) {
        ngx_log_error_core(NGX_LOG_EMERG, errno, "setsid() failed");
        return NGX_ERROR;
    }

    umask(0);

    fd = open("/dev/null", O_RDWR);
    if (fd == -1) {
        ngx_log_error_core(NGX_LOG_EMERG, errno,
                      "open(\"/dev/null\") failed");
        return NGX_ERROR;
    }

    if (dup2(fd, STDIN_FILENO) == -1) {
        ngx_log_error_core(NGX_LOG_EMERG, errno, "dup2(STDIN) failed");
        return NGX_ERROR;
    }

    if (dup2(fd, STDOUT_FILENO) == -1) {
        ngx_log_error_core(NGX_LOG_EMERG, errno, "dup2(STDOUT) failed");
        return NGX_ERROR;
    }

#if 0
    if (dup2(fd, STDERR_FILENO) == -1) {
        ngx_log_error_core(NGX_LOG_EMERG, errno, "dup2(STDERR) failed");
        return NGX_ERROR;
    }
#endif

    if (fd > STDERR_FILENO) {
        if (close(fd) == -1) {
            ngx_log_error_core(NGX_LOG_EMERG, errno, "close() failed");
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}
