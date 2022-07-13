#include "lst_timer.h"
#include "../http/http_conn.h"

SortTimerLst::SortTimerLst(): head(NULL), tail(NULL) {}

//常规销毁链表
SortTimerLst::~SortTimerLst() {
    UtilTimer* tmp = head;
    while (tmp) {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

//添加定时器，内部调用私有成员add_timer
void SortTimerLst::addTimer(UtilTimer* timer) {
    if (!timer) {
        return;
    }
    if (!head) {
        head = tail = timer;
        return;
    }
    //如果新的定时器超时时间小于当前头部结点
    //直接将当前定时器结点作为头部结点
    if (timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    //否则调用私有成员，调整内部结点
    addTimer(timer, head);
}

//调整定时器，任务发生变化时，调整定时器在链表中的位置
void SortTimerLst::adjustTimer(UtilTimer* timer) {
    if (!timer) {
        return;
    }
    //被调整的定时器在链表尾部
    //定时器超时值仍然小于下一个定时器超时值，不调整
    UtilTimer* tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire)) {
        return;
    }
    //被调整定时器在头部或内部，将定时器取出，重新插入
    if (timer == head) {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL; 
        addTimer(timer, head);
    } else {
        timer->prev->next = timer->next;    
        timer->next->prev = timer->prev;    
        addTimer(timer, timer->next);
    }
}

//删除定时器
void SortTimerLst::delTimer(UtilTimer* timer) {
    if (!timer) {
        return;
    }
    if ((timer == head) && (timer == tail)) {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if (timer == head) {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    if (timer == tail) {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

void SortTimerLst::addTimer(UtilTimer* timer, UtilTimer* lst_head) {
    UtilTimer* prev = lst_head;
    UtilTimer* tmp = prev->next; 
    while (tmp) {
        if (timer->expire < tmp->expire) {
            prev->next = timer;
            timer->prev = prev;
            timer->next = tmp;
            tmp->prev = timer;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    if (!tmp) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

//定时任务处理函数
void SortTimerLst::tick() {
    if (!head) {
        return;
    }

    time_t cur = time(NULL);
    UtilTimer* tmp = head;
    while (tmp) {
        if (cur < tmp->expire) {
            break;
        }
        //当前定时器到期，则调用回调函数，执行定时事件
        tmp->cbFunc(tmp->user_data);
        head = tmp->next;
        if (head) {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}

void Utils::init(int timeslot) {
    m_timeslot = timeslot;
}

int Utils::setNonblock(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void Utils::addfd(int epollfd, int fd, bool one_shot, int trig_mode) {
    epoll_event event;
    event.data.fd = fd;

    if (trig_mode == 1) 
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setNonblock(fd);
}

// 信号处理程序
void Utils::sigHandler(int sig) {
    // 为保证函数的可重入性，保留原来的errno
    // 可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;
    // 将信号值从管道写端写入，传输字符类型，而非整型
    send(u_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void Utils::addsig(int sig, void(*handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    //将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);
    //执行sigaction函数
    assert(sigaction(sig, &sa, NULL) != -1);
    // assert作用是如果它的条件返回错误，则终止程序执行，
}

void Utils::timerHandler() {
    m_timer_lst.tick();
    alarm(m_timeslot);
}

void Utils::showError(int connfd, const char* info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int* Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cbFunc(ClientData* user_data) {
    //删除非活动连接在socket上的注册事件
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    //关闭文件描述符
    close(user_data->sockfd);
    //减少连接数
    HttpConn::m_user_count--;
}
