CXX = g++
CXXFLAGS = -Wall -std=c++11

# Add a DEBUG variable. Set to 1 for debug mode, 0 for release mode.
DEBUG = 0
ifeq ($(DEBUG), 1)
   CXXFLAGS += -g -O0
else
   CXXFLAGS += -O2
endif

server: main.cpp ./lock/locker.h ./log/block_queue.h ./log/log.cpp \
./tools/tools.cpp  ./config/config.cpp ./timer/timer.cpp \
./sql/sql_connection_pool.cpp ./thread_pool/thread_pool.cpp \
./http/http_conn.cpp ./webserver/webserver.cpp
	$(CXX) $(CXXFLAGS) -o web_server $^ -lpthread -lmysqlclient

test: test.cpp ./lock/locker.h ./log/block_queue.h ./log/log.cpp \
./tools/tools.cpp  ./config/config.cpp ./timer/timer.cpp \
./sql/sql_connection_pool.cpp ./thread_pool/thread_pool.cpp \
./http/http_conn.cpp 
	$(CXX) $(CXXFLAGS) -o test $^ -lpthread -lmysqlclient

run: web_server
	./web_server -m 1 -s 8 -t 8 -c 0 -a 1

clean:
	rm -f web_server test

.PHONY: all run server clean