#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

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
#include <errno.h>
#include <stdarg.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <map>
#include <string>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../log/log.h"
#include "../timer/lst_timer.h"

class HttpConn {
public:
    static const int FILENAME_LEN  = 200;        // 设置读取文件的名称m_real_file大小
    static const int READ_BUFFER_SIZE = 2048;    // 设置读缓冲区m_read_buffer大小
    static const int WRITE_BUFFER_SIZE = 1024;   // 设置写缓冲区m_write_buffer大小
    enum Method {       // 报文的请求方法，本项目只用到post和get
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CheckState {       // 主状态机状态
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HttpCode {         // 报文解析结果
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUSET,
        INTERNAL_REQUSET,
        CLOSED_CONNECTION
    };
    enum LineStatus {       // 从状态机状态
        LINE_OK,
        LINE_BAD,
        LIEN_OPEN
    };

public:
    HttpConn() {}
    ~HttpConn() {}

public:
    // 初始化套接字地址，函数内会调用私有方法init
    void init(int sockfd, const sockaddr_in& addr, char*, int, int,
              std::string  user, std::string passwd, std::string sqlname);
    // 关闭http连接
    void closeConn(bool read_close = true);
    void process();
    // 读取客户端发来的全部数据
    bool readOnce();
    // 响应报文写入函数
    bool write();
    sockaddr_in* getAddress() { return &m_address; }
    // 同步线程初始化数据库读取表
    void initMysqlResult(ConnectionPool* connpool);
    int timer_flag;
    int improv;

private:
    void init();
    // 从m_read_buf读取并处理请求报文
    HttpCode processRead();
    // 向m_write_buf写入响应报文数据
    bool processWrite(HttpCode ret);
    // 主状态机解析报文中的请求行数据
    HttpCode parseRequestLine(char* text);
    // 主状态机解析报文中的请求头数据
    HttpCode parseHeaders(char* text);
    // 主状态机解析报文中的请求内容
    HttpCode parseContent(char* text);
    // 生成响应报文
    HttpCode doRequest();
    // m_start_line是已经解析的字符，getLine用于将指针向后偏移，指向未处理的字符
    char* getLine() { return m_read_buf + m_start_line; }
    // //从状态机读取一行，分析是请求报文的哪一部分
    LineStatus parseLine();
    void unmap();
    // 根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    bool addResponse(const char* format, ...);
    bool addContent(const char* content);
    bool addStatusLine(int status, const char* title);
    bool addHeaders(int content_length);
    bool addContentType();
    bool addContentLength(int content_length);
    bool addLinger();  
    bool addBlankLine();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL* mysql;
    int m_state;    // 读为0，写为1 

private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];   // 存储读取的请求报文数据
    int m_read_idx;     //缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    int m_checked_idx;  //m_read_buf读取的位置m_checked_idx
    int m_start_line;   //m_read_buf中已经解析的字符个数
    char m_write_buf[WRITE_BUFFER_SIZE]; //存储发出的响应报文数据
    int m_write_idx;     //指示buffer中的长度
    CheckState m_check_state;    //主状态机的状态
    Method m_method;    //请求方法

    //以下为解析请求报文中对应的6个变量
    //存储读取文件的名称
    char m_real_file[FILENAME_LEN];
    char* m_url;
    char* m_version;
    char* m_host;
    int m_content_length;
    bool m_linger;  // 保留

    char* m_file_address;   // 文件映射区地址
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    int cgi;        // 是否启用的post   
    char* m_string; // 存储请求数据
    int bytes_to_send;      //剩余发送字节数
    int bytes_have_send;    //已发送字节数
    char* doc_root;         // //网站根目录，文件夹内存放请求的资源和跳转的html文件

    std::map<std::string, std::string> m_users;
    int m_trig_mode;
    int m_close_log;
    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif