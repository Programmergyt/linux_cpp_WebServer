#!/bin/bash

# 测试HTTP连接超时功能的脚本

echo "测试HTTP连接超时功能..."

# 启动服务器（在后台）
cd /opt/my_server
./web_server 9999 localhost root 123456 yourdb 1 8 8 0 5 &  # 5秒超时
SERVER_PID=$!

echo "服务器已启动，PID: $SERVER_PID，超时时间设置为5秒"

# 等待服务器启动
sleep 2

echo "建立连接但不发送数据，测试超时..."

# 使用telnet建立连接但不发送数据，应该在5秒后被服务器关闭
timeout 10 telnet localhost 9999 << 'EOF'
# 这里不发送任何HTTP请求，让连接空闲
EOF

echo "测试完成"

# 关闭服务器
kill $SERVER_PID
wait $SERVER_PID 2>/dev/null

echo "服务器已关闭"