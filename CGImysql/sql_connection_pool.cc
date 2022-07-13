#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <string>
#include <iostream>
#include <list>
#include <mysql/mysql.h>
#include "sql_connection_pool.h"

using namespace std;

ConnectionPool::ConnectionPool(): m_curconn(0), m_freeconn(0) {}

ConnectionPool* ConnectionPool::GetInstance() {
    static ConnectionPool connpool;
    return &connpool;
}

ConnectionPool::~ConnectionPool() {
    destoryPool();
}

// 构造初始化
void ConnectionPool::init(string url, string user, string password, string db_name,
                          int port, int maxconn, int close_log) {
    m_url = url;
    m_port = port;
    m_user = user;
    m_password = password;
    m_close_log = close_log;
    m_database_name = db_name;

    // 创建maxocnn条连接
    for (int i = 0; i < maxconn; i++) {
        MYSQL* con = NULL;  

        con = mysql_init(con);
        if (con == NULL) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        // 尝试与运行在主机上的MySQL数据库引擎建立连接  
        con = mysql_real_connect(con, url.c_str(), user.c_str(), password.c_str(),
                                 db_name.c_str(), port, NULL, 0);
        if (con == NULL) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }

        // 更新连接池和空闲连接数量
        connlist.push_back(con);
        ++m_freeconn;
    }

    // 将信号量初始化为最大连接数目
    reserve = Sem(m_freeconn);
    m_maxconn = m_freeconn;
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL* ConnectionPool::getConnection() {
    MYSQL* con = NULL;

    if (connlist.size() == 0) return NULL;

    reserve.wait();
    // 多线程操作连接池，会造成竞争
    lock.lock();
    con = connlist.front();
    connlist.pop_front();
    --m_freeconn;
    ++m_curconn;

    lock.unlock();
    return con;
}

// 释放当前使用连接
bool ConnectionPool::releaseConnection(MYSQL* con) {
    if (con == NULL) return false;

    lock.lock();
    connlist.push_back(con);
    ++m_freeconn;
    --m_curconn;

    lock.unlock();

    reserve.post();
    return true;
}

// 销毁数据库连接池
void ConnectionPool::destoryPool() {
    lock.lock(); 
    if (connlist.size() > 0) {
        list<MYSQL*>::iterator it;
        for (it = connlist.begin(); it != connlist.end(); it++) {
            MYSQL* con = *it;
            mysql_close(con);
        }
        m_curconn  = 0;
        m_freeconn = 0;
        connlist.clear();
    }

    lock.unlock();
}

// 当前空闲的连接数
int ConnectionPool::getFreeconn() {
    return this->m_freeconn;
}

ConnectionRAII::ConnectionRAII(MYSQL** conn, ConnectionPool* connpool) {
    *conn = connpool->getConnection();

    conn_raii = *conn;
    pool_raii = connpool;
}

ConnectionRAII::~ConnectionRAII() {
    pool_raii->releaseConnection(conn_raii);
}