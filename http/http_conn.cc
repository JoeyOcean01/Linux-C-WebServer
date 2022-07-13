#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

using namespace std;

// 定义http响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found  on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

Locker m_lock;
map<string, string> users;

void HttpConn::initMysqlResult(ConnectionPool* connpool) {
    // 先从连接池中取一个 连接
    MYSQL* mysql = NULL;
    ConnectionRAII(&mysql, connpool);

    // 在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username, passwd FROM user")) {
        LOG_ERROR("SELECT error: %s\n", mysql_error(mysql));
    }

    // 从表中检索完整的结果集, 返回具有多个结果的MYSQL_RES结果集合
    MYSQL_RES* result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码，存入map中, 行内值的数目由mysql_num_fields(result)给出
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

// 对文件描述符设置非阻塞
int setNonblock(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int trig_mode) {
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

// 从内核事件表中删除描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void modifyfd(int epollfd, int fd, int ev, int trig_mode) {
    epoll_event event;
    event.data.fd = fd;

    if (trig_mode == 1)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP; 
    else 
        event.events = ev | EPOLLONESHOT| EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);      
}

int HttpConn::m_user_count = 0;
int HttpConn::m_epollfd = -1;

// 关闭连接，关闭一个连接，客户总量减一
void HttpConn::closeConn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 初始化连接，外部调用初始化套接字地址
void HttpConn::init(int sockfd, const sockaddr_in& addr, char* root, int trig_mode,
                    int close_log, string user, string passwd, string sqlname) {
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_trig_mode);
    m_user_count++;

    // 当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或访问的文件中任容为空
    doc_root = root;
    m_trig_mode = trig_mode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

// 初始化新接受的连接，check_state默认为分析请求行状态
void HttpConn::init() {
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// 从状态机，用于分析出一行的内容
// 返回值为行的读取状态，有LINE_OK, LINE_BAD, LINE_OPEN
HttpConn::LineStatus HttpConn::parseLine() {
    char tmp;
    for (; m_checked_idx < m_read_idx; m_checked_idx++) {
        tmp = m_read_buf[m_checked_idx];
        //如果当前是\r字符，则有可能会读取到完整行
        if (tmp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx){
                //下一个字符达到了buffer结尾，则接收不完整，需要继续接收
                return LIEN_OPEN;
            } else if(m_read_buf[m_checked_idx + 1] == '\n') {
                //下一个字符是\n，将\r\n改为\0\0
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                // 每次解析完指向下一行
                return LINE_OK;
            }
            //如果都不符合，则返回语法错误
            return LINE_BAD;
        } else if (tmp == '\n') {
        //如果当前字符是\n，也有可能读取到完整行
        //一般是上次读取到\r就到buffer末尾了，没有接收完整，再次接收时会出现这种情况
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;              
            }
            return LINE_BAD;
        }
    }
    //并没有找到\r\n，需要继续接收
    return LIEN_OPEN;
}

// 循环读取客户数据，直到无数据可读或对方关闭连接
// 读取到m_read_buffer中，并更新m_read_idx
// 非阻塞ET工作模式下，需要一次性将数据读完
bool HttpConn::readOnce() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;
    // LT读取数据
    if (m_trig_mode == 0) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, 
                          READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if (bytes_read <= 0) {
            return false;
        }

        return true;
    } else {    // ET读数据
        while (1) {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, 
                              READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;  //非阻塞ET模式下，需要一次性将数据读完
                return false;
            } else if (bytes_read == 0) {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }      
}

// 解析http请求行，获得请求方法，目标url及http版本号
HttpConn::HttpCode HttpConn::parseRequestLine(char* text) {
    //在HTTP报文中，请求行用来说明请求类型,要访问的资源以及所使用的HTTP版本，其中各个部分之间通过\t或空格分隔。
    //请求行中最先含有空格和\t任一字符的位置并返回
    m_url = strpbrk(text, " \t");
    //如果没有空格或\t，则报文格式有误
    if (!m_url) {
        return BAD_REQUEST;
    }
    //将该位置改为\0，用于将前面数据取出方法
    *m_url++ = '\0';
    //取出数据，并通过与GET和POST比较，以确定请求方式
    char* method = text;
    if (strcasecmp(method, "GET") == 0){
        m_method = GET;
    } else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    } else {
        return BAD_REQUEST;
    }
    //m_url此时跳过了第一个空格或\t字符，但不知道之后是否还有
    //将m_url向后偏移，通过查找，继续跳过空格和\t字符，指向请求资源的第一个字符
    m_url += strspn(m_url, " \t");  // 顺序不重要但是得是连续的
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");  // 跳过多余
    //仅支持HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    //对请求资源前7个字符进行判断
    //这里主要是有些报文的请求资源中会带有http://，这里需要对这种情况进行单独处理
    if ((m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
     
    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    //一般的不会带有上述两种符号，直接是单独的/或/后面带访问资源
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    // 当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    //请求行处理完毕，将主状态机转移处理请求头
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求的一个头部信息
HttpConn::HttpCode HttpConn::parseHeaders(char* text) {
    //判断是空行还是请求头
    if (text[0] == '\0') {
        //判断是GET还是POST请求
        if (m_content_length != 0) {
            //POST需要跳转到消息体处理状态
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        //解析请求头部连接字段
        text += 11;
        text += strspn(text, " \t");     //跳过空格和\t字符
        //如果是长连接，则将linger标志设置为true
        if (strcasecmp(text, "keep-alive") == 0) m_linger = true;
    } else if (strncasecmp(text, "Content-length:", 15) == 0) {
        //解析请求头部内容长度字段
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        LOG_INFO("oop! unknow header: %s", text);
    }

    return NO_REQUEST;
}

// 判断http请求是否被完整读入
HttpConn::HttpCode HttpConn::parseContent(char* text) {
    //判断buffer中是否读取了消息体
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        // post请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

HttpConn::HttpCode HttpConn::processRead() {
    //初始化从状态机状态、HTTP请求解析结果
    LineStatus line_status = LINE_OK;
    HttpCode ret = NO_REQUEST;
    char* text = 0;

    // parseLine为从状态机的具体实现
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK)
            || ((line_status = parseLine()) == LINE_OK)) {
        text = getLine();
        //m_start_line是每一个数据行在m_read_buf中的起始位置
        //m_checked_idx表示从状态机在m_read_buf中读取的位置
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        // 主状态机的三种状态转移逻辑
        switch (m_check_state) {
        case CHECK_STATE_REQUESTLINE:
            ret = parseRequestLine(text);
            if (ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }
            break;

        case CHECK_STATE_HEADER:
            ret = parseHeaders(text);
            if (ret == BAD_REQUEST) {
                return BAD_REQUEST;
            } else if (ret == GET_REQUEST) {
                return doRequest(); //完整解析GET请求后，跳转到报文响应函数
            }
            break;

        case CHECK_STATE_CONTENT:
            ret = parseContent(text);
            if (ret == GET_REQUEST) {
                return doRequest();
            }
            //解析完消息体即完成报文解析，避免再次进入循环，更新line_status
            line_status = LIEN_OPEN;
            break;
            
        default:
            return INTERNAL_REQUSET;
        }
    }
    return NO_REQUEST;
}

HttpConn::HttpCode HttpConn::doRequest() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // printf("m_url: %s\n", m_url);
    // 找到m_url右边中/的位置
    const char* p = strrchr(m_url, '/');

    // 处理cgi, 2登录，3注册
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        // 根据标志判断是登录检测还是注册检测
        char flag = m_url[1];
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/");
        strcat(url_real, m_url + 2);    // 忽略“/2”
        strncpy(m_real_file + len, url_real, FILENAME_LEN - len -1);
        free(url_real);

        // 将用户名和密码提取出来
        // 格式：user=123&password=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; i++) {
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; i++, j++) {
            password[j] = m_string[i];
        }   
        password[j] = '\0';

        if (*(p + 1) == '3') {
            // 如果是注册，先检测数据库中是否有重名
            // 没有重名的，进行增加数据
            char* sql_insert = (char*)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            // 同步线程登录校验
            if (users.find(name) == users.end()) {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/log.html");
                else 
                    strcpy(m_url, "/registerError.html");
            } else {
                strcpy(m_url, "/registerError.html");
            }

            free(sql_insert);
        } else if (*(p + 1) == '2') {
        // 如果是登录，直接判断
        // 若浏览器输入的用户名和密码可在表中找到，返回1，否则返回0
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else    
                strcpy(m_url, "/logError.html");
        }
    }

    if (*(p + 1) == '0') {
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/register.html");
        //将网站目录和/register.html进行拼接，更新到m_real_file中
        strncpy(m_real_file + len, url_real, strlen(url_real));
        
        free(url_real);
    } else if (*(p + 1) == '1') {
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/log.html");
        strncpy(m_real_file + len, url_real, strlen(url_real));

        free(url_real);
    } else if (*(p + 1) == '5') {
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/picture.html");
        strncpy(m_real_file + len, url_real, strlen(url_real));

        free(url_real);
    } else if (*(p + 1) == '6') {
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/video.html");
        strncpy(m_real_file + len, url_real, strlen(url_real));

        free(url_real);
    } else if (*(p + 1) == '7') {
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/fans.html");
        strncpy(m_real_file + len, url_real, strlen(url_real));

        free(url_real);
    } else {
        //如果以上均不符合，即不是登录和注册，直接将url与网站目录拼接
        //这里的情况是welcome界面，请求服务器上的一个图片
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);  
    }

    //通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
    //失败返回NO_RESOURCE状态，表示资源不存在
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    //判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    //判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    //以只读方式获取文件描述符，通过mmap将该文件映射到内存中
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    
    //避免文件描述符的浪费和占用
    close(fd);
    return FILE_REQUSET;    
}

void HttpConn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);        
        m_file_address = 0;
    }
}

bool HttpConn::write() {
    int tmp = 0;

    //若要发送的数据长度为0
    //表示响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0) {
        modifyfd(m_epollfd, m_sockfd, EPOLLIN, m_trig_mode);
        init();
        return true;
    }

    while (1) {
        // 将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        tmp = writev(m_sockfd, m_iv, m_iv_count);
        if (tmp < 0) {
            // 判断缓冲区是否满了
            if (errno == EAGAIN) {
                // 重新注册写事件
                modifyfd(m_epollfd, m_sockfd, EPOLLOUT, m_trig_mode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += tmp;
        bytes_to_send -= tmp;
        // 判断头部信息是否发送完了
        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx); // 已经发送的减去m_write_buf中的头部数据不就是文本吗
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        //判断条件，数据已全部发送完
        if (bytes_to_send <= 0) {
            unmap();
            //在epoll树上重置EPOLLONESHOT事件
            modifyfd(m_epollfd, m_sockfd, EPOLLIN, m_trig_mode);

            if (m_linger) {
                init(); //重新初始化HTTP对象
                return true;
            } else {
                return false;
            }
        }
    }
}

bool HttpConn::addResponse(const char* format, ...) {
    //如果写入内容超出m_write_buf大小则报错
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;

    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx,
                        format, arg_list);
    //如果写入的数据长度超过缓冲区剩余空间，则报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("response:%s", m_write_buf);

    return true;
}

// 添加状态行
bool HttpConn::addStatusLine(int status, const char* title) {
    return addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 添加消息报头，具体的添加文本长度、连接状态和空行
bool HttpConn::addHeaders(int content_len) {
    return addContentLength(content_len) && addLinger() &&
           addBlankLine();
}

// 添加Content-Length，表示响应报文的长度
bool HttpConn::addContentLength(int content_len) {
    return addResponse("Content-Length:%d\r\n", content_len);
}

bool HttpConn::addContentType() {
    return addResponse("Content-Type:%s\r\n", "text/html");
}

bool HttpConn::addLinger() {
    return  addResponse("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool HttpConn::addContent(const char* content) {
    return addResponse("%s", content);
}

bool HttpConn::addBlankLine() {
    return addResponse("%s", "\r\n");
}

bool HttpConn::processWrite(HttpCode ret) {
    switch (ret) {
    case INTERNAL_REQUSET:
        addStatusLine(500, error_500_title);
        addHeaders(strlen(error_500_form));
        if (!addContent(error_500_form))
            return false;
        break;

    case BAD_REQUEST:
        addStatusLine(400, error_400_title);
        addHeaders(strlen(error_400_form));
        if (!addContent(error_400_form))
            return false;
        break;

    case FORBIDDEN_REQUEST:
        addStatusLine(403, error_403_title);
        addHeaders(strlen(error_403_form));
        if (!addContent(error_403_form))
            return false;
        break;
    
    case NO_RESOURCE:
        addStatusLine(403, error_403_title);
        addHeaders(strlen(error_403_form));
        if (!addContent(error_403_form))
            return false;
        break;

    case FILE_REQUSET:
        addStatusLine(200, ok_200_title);
        if (m_file_stat.st_size != 0) {
            addHeaders(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;

            return true;
        } else {
            const char* ok_string = "<html><body></body></html>";
            addHeaders(strlen(ok_string));
            if (!addContent(ok_string))
                return false;
        }
    
    default:
        return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void HttpConn::process() {
    HttpCode read_ret = processRead();
    if (read_ret == NO_REQUEST) {
        modifyfd(m_epollfd, m_sockfd, EPOLLIN, m_trig_mode);
        return;  
    }
    bool write_ret = processWrite(read_ret);
    if (!write_ret) {
        closeConn();
    }
    modifyfd(m_epollfd, m_sockfd, EPOLLOUT, m_trig_mode);
}