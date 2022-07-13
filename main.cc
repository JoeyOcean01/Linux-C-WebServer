#include "config.h"

int main(int argc, char* argv[]) {
    // 需要修改的数据库信息，登录名，密码，库名
    std::string user = "root";
    std::string passwd = "Li13579boss.";
    std::string dbname = "ocean";

    // 命令行解析
    Config config;
    config.parseArg(argc, argv);

    WebServer server;

    // 初始化
    server.init(config.port, user, passwd, dbname, config.log_write, 
                config.opt_linger, config.trigger_mode, config.sql_num,
                config.thread_num, config.close_log, config.actor_model);

    // 日志
    server.logWrite();

    // 数据库
    server.sqlPool();

    // 线程池
    server.threadPool();

    // 触发模式
    server.triggerMode();

    // 监听
    server.eventListen();

    // 运行
    server.eventLoop();

    return 0;
}