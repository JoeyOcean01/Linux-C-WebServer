CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
	CXXFLAGS += -g
else
	CXXFLAGS += -O2
endif

server: main.cc ./timer/lst_timer.cc ./http/http_conn.cc ./log/log.cc ./CGImysql/sql_connection_pool.cc webserver.cc config.cc
	$(CXX) -lpthread -lmysqlclient $(CXXFLAGS) $^ -o server

clean:
	rm -r server