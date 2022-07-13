#ifndef SQL_CONNECTION_POOL_H
#define SQL_CONNECTION_POOL_H

#include <stdio.h>
#include <error.h>
#include <string.h>
#include <list>
#include <iostream>
#include <string>
#include <mysql/mysql.h>
#include "../lock/locker.h"
#include "../log/log.h"

class ConnectionPool {
public:
    MYSQL* getConnection();                 // 获取数据库连接
    bool releaseConnection(MYSQL* conn);    // 释放连接
    int getFreeconn();                      // 获取连接
    void destoryPool();                     // 销毁所有连接

    // 单列模式
    static ConnectionPool* GetInstance();

    void init(std::string url, std::string user, std::string password, std::string database_name,
              int port, int maxconn, int close_log);
public:
    std::string m_url;              // 主机地址
    std::string m_port;             // 数据库端口号
    std::string m_user;             // 登录数据库用户名
    std::string m_password;         // 登录数据库密码
    std::string m_database_name;    // 使用数据库名
    int m_close_log;                // 日志开关

private:
    ConnectionPool();
    ~ConnectionPool();
private:
    int m_maxconn;  // 最大连接数
    int m_curconn;  // 当前已使用的连接数
    int m_freeconn; // 当前空闲的连接数
    Locker lock;
    std::list<MYSQL*> connlist; // 连接池
    Sem reserve;    // 预留连接
};

// 将数据库连接的获取与释放通过RAII机制封装，避免手动释放
class ConnectionRAII {
public:
    // 需要传入二级指针修改MYSQL*的值
    ConnectionRAII(MYSQL** conn, ConnectionPool* connpool);
    ~ConnectionRAII();

private:
    MYSQL* conn_raii;
    ConnectionPool* pool_raii;
};

#endif