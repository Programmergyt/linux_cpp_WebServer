# ===============================
# WebServer Makefile
# 只编译 main.cpp + src/*.cpp，最终输出 web_server
# ===============================

CXX = g++
CXXFLAGS = -Wall -std=c++11 -O2
INCLUDES = -I./include
LIBS = -lpthread -lmysqlclient

TARGET = web_server

# ===============================
# 编译目标
# ===============================
server:
	@echo "Compiling and linking $(TARGET)..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) main.cpp src/*.cpp -o $(TARGET) $(LIBS)
	@echo "✅ Build finished: ./$(TARGET)"

# ===============================
# 运行 server
# ===============================
run: 
	@echo "Running server..."
	./$(TARGET) -m 1 -s 8 -t 8 -c 0 -a 1

# ===============================
# 清理
# ===============================
clean:
	@echo "Cleaning..."
	rm -f $(TARGET)

.PHONY: server run clean
