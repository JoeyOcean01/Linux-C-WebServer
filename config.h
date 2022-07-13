#ifndef CONFIG_H
#define CONFIG_H

#include "webserver.h"

class Config {
public:
    Config();
    ~Config() {}

    void parseArg(int argc, char* argv[]);

    int port;
    int log_write;
    int trigger_mode;
    int listen_trigmode;
    int conn_trigmode;
    int opt_linger;
    int sql_num;
    int thread_num;
    int close_log;
    int actor_model;
};

#endif