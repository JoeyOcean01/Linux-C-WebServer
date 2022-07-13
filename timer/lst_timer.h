#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>
#include "../log/log.h"

// 连接资源结构体成员需要用到定时器类，提前声明
class UtilTimer;

// 连接资源
struct ClientData {
    sockaddr_in address;    // 客户端socket地址
    int sockfd;             // socket文件描述符
    UtilTimer* timer;       // 定时器
};

// 定时器类
class UtilTimer {
public:
    UtilTimer(): prev(NULL), next(NULL) {}
public:
    time_t expire;                  // 超时时间
    void (*cbFunc)(ClientData*);   // 回调函数
    ClientData* user_data;          // 连接资源
    UtilTimer* prev;                // 前驱定时器
    UtilTimer* next;                // 后继定时器
};

// 定时器容器类
class SortTimerLst {
public:
    SortTimerLst();
    ~SortTimerLst();
    void addTimer(UtilTimer* timer);
    void adjustTimer(UtilTimer* timer);
    void delTimer(UtilTimer* timer);
    void tick();

private:
    void addTimer(UtilTimer* timer, UtilTimer* lst_head);
private:
    UtilTimer* head;
    UtilTimer* tail;
};

class Utils {
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setNonblock(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sigHandler(int sig);

    //设置信号函数
    void addsig(int sig, void(*handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timerHandler();

    void showError(int connfd, const char *info);

public:
    static int* u_pipefd;
    SortTimerLst m_timer_lst; // 容器
    static int u_epollfd;
    int m_timeslot;     // 时间片
};

void cbFunc(ClientData *user_data);

#endif