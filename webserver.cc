#include "webserver.h"

using namespace std;

WebServer::WebServer() {
    // HttpConn类对象
    users = new HttpConn[MAX_FD];

    // root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char*)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    // 定时器
    users_timer = new ClientData[MAX_FD];
}

WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete [] users;
    delete [] users_timer;
    delete m_pool;
}

void  WebServer::init(int port, string user, string passwd, string db_name,
           int log_write, int opt_linger, int trigger_mode, int sql_num,
           int thread_num, int close_log, int actor_model) {
    m_port = port;
    m_user = user;
    m_passwd = passwd;
    m_db_name = db_name;
    m_log_async = log_write;
    m_opt_linger = opt_linger;
    m_trigger_mode = trigger_mode;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_close_log = close_log;
    m_actor_model = actor_model;
}

void WebServer::triggerMode() {
    // LT + LT
    if (m_trigger_mode == 0) {  
        m_listen_trigmode = 0;
        m_conn_trigmode = 0;
    } 
    // LT + ET
    else if (m_trigger_mode == 1) {
        m_listen_trigmode = 0;
        m_conn_trigmode = 1;
    }
    // ET + LT
    else if (m_trigger_mode == 2) {
        m_listen_trigmode = 1;
        m_conn_trigmode = 0;
    } 
    // ET + ET
    else if (m_trigger_mode == 3) {
        m_listen_trigmode = 1;
        m_conn_trigmode = 0;
    }
}

void WebServer::logWrite() {
    if (m_close_log == 0) {
        // 初始化日志
        if (m_log_async == 1)
            Log::GetInstance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::GetInstance()->init("./ServerLog", m_close_log, 2000, 500000, 0);
    }
}

void WebServer::sqlPool() {
    // 初始化数据库连接池
    m_connpool = ConnectionPool::GetInstance();
    m_connpool->init("localhost", m_user, m_passwd, m_db_name, 3306, m_sql_num, m_close_log);

    // 初始化数据库读取表
    users->initMysqlResult(m_connpool);
}

void WebServer::threadPool() {
    m_pool = new ThreadPool<HttpConn>(m_actor_model, m_connpool, m_thread_num);
}

void WebServer::eventListen() {
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    if (m_opt_linger == 0) {
        struct linger tmp = {0, 1}; // 底层会将未发送完的数据发送完成后再释放资源
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    } else if (m_opt_linger == 1) {
        struct linger tmp = {1, 1}; // 等待一会，close阻塞，超时再强制关闭
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY); // 将套接字绑定到主机上的所有网络接口
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    // epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_listen_trigmode);
    HttpConn::m_epollfd = m_epollfd;

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setNonblock(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);    // 套接字不再连接时产生sigpipe，忽略之 
    utils.addsig(SIGALRM, utils.sigHandler, false);
    utils.addsig(SIGTERM, utils.sigHandler, false);

    alarm(TIMESLOT);

    // 工具类，信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address) {
    users[connfd].init(connfd, client_address, m_root, m_conn_trigmode,
                       m_close_log, m_user, m_passwd, m_db_name);   // addfd

    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    UtilTimer* timer = new UtilTimer;
    timer->user_data = &users_timer[connfd];
    timer->cbFunc = cbFunc;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.addTimer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjustTimer(UtilTimer* timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjustTimer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::dealTimer(UtilTimer* timer, int sockfd) {
    // 关闭连接，移除定时器
    timer->cbFunc(&users_timer[sockfd]);
    if (timer) {
        utils.m_timer_lst.delTimer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

// 接受连接，连上定时器
bool WebServer::dealClientData() {
    struct sockaddr_in client_address;
    socklen_t client_addr_len = sizeof(client_address);

    if (m_listen_trigmode == 0) {
        int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addr_len);
        if (connfd < 0) {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (HttpConn::m_user_count >= MAX_FD) {
            utils.showError(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    } else {
        // 需要循环接收数据
        while (1) {
            int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addr_len);
            if (connfd < 0) {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (HttpConn::m_user_count >= MAX_FD) {
                utils.showError(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }

    return true;
}

// 接收信号
bool WebServer::dealWithSignal(bool& timeout, bool& stop_server) {
    int ret = 0;
    int sig;
    char signals[1024];
   
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1) {
        return false;
    } else if (ret == 0) {
        return false;
    } else {
        for (int i = 0; i < ret; i++) {
            switch (signals[i]) {
            case SIGALRM:
                timeout = true;
                break;
            
            case SIGTERM:
                stop_server = true;
                break;
            }
        }
    }

    return true;
}

void WebServer::dealWithRead(int sockfd) {
    UtilTimer* timer = users_timer[sockfd].timer;

    // reactor
    if (m_actor_model == 1) {
        if (timer) {
        adjustTimer(timer);
        }

        // 检测到读事件，放入请求队列
        m_pool->append(users + sockfd, 0);

        while (1) {
            if (users[sockfd].improv == 1) {
                if (users[sockfd].timer_flag == 1) {
                    // readOnce出错
                    dealTimer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    } else {
    // proactor
        if (users[sockfd].readOnce()) {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].getAddress()->sin_addr));

            // 若检测到读事件，加入请求队列
            m_pool->appendNonStat(users + sockfd);

            if (timer) {
                adjustTimer(timer);
            }
        } else {
            dealTimer(timer, sockfd);
        }
    }
}

void WebServer::dealWithWrite(int sockfd) {
    UtilTimer* timer = users_timer[sockfd].timer;

    // reactor
    if (m_actor_model == 1) {
        // 因为是线程，可能出现阻塞，我们先要把定时器往后延
        if (timer) {
            adjustTimer(timer);
        }
        m_pool->append(users + sockfd, 1);

        while (1) {
            // 若只有一个flag，有初值，再次用到这个会直接执行下列语句，而我们需要工作线程做完任务
            if (users[sockfd].improv == 1) {
                if (users[sockfd].timer_flag == 1) {
                    dealTimer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    } else {
    // proactor
        if (users[sockfd].write()) {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].getAddress()->sin_addr));

            if (timer) {
                adjustTimer(timer);
            }
        } else {
            dealTimer(timer, sockfd);
        }
    }
}

void WebServer::eventLoop() {
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server) {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            if (sockfd == m_listenfd) {
                // 处理新到的客户链接
                bool flag = dealClientData();
                if (flag == false) {
                    continue;
                }
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 服务器关闭连接，移除对应的定时器
                UtilTimer* timer = users_timer[sockfd].timer;
                dealTimer(timer, sockfd);                
            } else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {
                // 处理信号
                bool flag = dealWithSignal(timeout, stop_server); 
                if (flag == false) {
                    LOG_ERROR("%s", "dealWithSiganl failure");
                }
            } else if (events[i].events & EPOLLIN) {
                // 处理客户连接上接受到的数据
                dealWithRead(sockfd);
            } else if (events[i].events & EPOLLOUT) {
                // 可写响应报文
                dealWithWrite(sockfd);
            }
        }

        if (timeout) {
            utils.timerHandler();   // tick alarm

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}