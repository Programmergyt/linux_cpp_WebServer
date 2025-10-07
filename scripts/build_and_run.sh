#!/bin/bash
set -e

PROJECT_ROOT="/opt/my_server"
BUILD_DIR="$PROJECT_ROOT/build"

cd "$BUILD_DIR"
cmake -DDEBUG=ON ..
make -j$(nproc) # 使用所有CPU核心编译项目

echo "✅ Build complete. Starting server..."
cd "$PROJECT_ROOT"      # ✅ 回到项目根目录再执行
./web_server -m 1 -s 8 -t 8 -c 0 -a 1
