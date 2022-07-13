#include "config.h"

Config::Config() {
    // 端口号默认9006
    port = 9006;

    // 日志写入方式，默认同步
    log_write = 0;

    // 触发组合模式，默认listenfd LT + connfd LT
    trigger_mode = 0;

    // listen触发模式，默认LT
    listen_trigmode = 0;

    // connfd触发模式，默认LT
    conn_trigmode = 0;

    // 优雅关闭连接
    opt_linger = 0;

    // 数据库连接池数量，默认8
    sql_num = 8;

    // 线程池内线程数量，默认8
    thread_num = 8;

    // 并发模型，默认是proactor
    actor_model = 0;
}

void Config::parseArg(int argc, char* argv[]) {
    int opt;
    const char* str = "p:l:m:o:s:t:c:a:";

    while ((opt = getopt(argc, argv, str)) != -1) {
        switch (opt) {
        case 'p':
            port = atoi(optarg);
            break;
        
        case 'l':
            log_write = atoi(optarg);
            break;

        case 'm':
            trigger_mode = atoi(optarg);
            break;
        
        case 'o':
            opt_linger = atoi(optarg);
            break;
        
        case 's':
            sql_num = atoi(optarg);
            break;
        
        case 't':
            thread_num = atoi(optarg);
            break;

        case 'c':
            close_log = atoi(optarg);
            break;
        
        case 'a':
            actor_model = atoi(optarg);
            break;
        
        default:
            break;
        }
    }
}