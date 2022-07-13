#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65536;   // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;     // 最小超时单位

class WebServer {
public:
    WebServer();
    ~WebServer();

    void  init(int port, std::string user, std::string passwd, std::string db_name,
               int log_write, int opt_linger, int trigger_mode, int sql_num,
               int thread_num, int close_log, int actor_model);
    void threadPool();
    void sqlPool();
    void logWrite();
    void triggerMode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjustTimer(UtilTimer* timer);
    void dealTimer(UtilTimer* timer, int sockfd);
    bool dealClientData();
    bool dealWithSignal(bool& timeout, bool& stop_server);
    void dealWithRead(int sockfd);
    void dealWithWrite(int sockfd);

public:
    // 基础
    int m_port;
    char* m_root;
    int m_log_async;    
    int m_close_log;    
    int m_actor_model;

    int m_pipefd[2];
    int m_epollfd;
    HttpConn* users;

    // 数据库相关
    ConnectionPool* m_connpool;
    std::string m_user;
    std::string m_passwd;
    std::string m_db_name;
    int m_sql_num;

    // 线程池相关
    ThreadPool<HttpConn>* m_pool;
    int m_thread_num;

    // epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_opt_linger;
    int m_trigger_mode;
    int  m_listen_trigmode;
    int m_conn_trigmode;

    // 定时器相关
    ClientData* users_timer;
    Utils utils;
};

#endif