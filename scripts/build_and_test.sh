#!/bin/bash
set -e

PROJECT_ROOT="/opt/my_server"
BUILD_DIR="$PROJECT_ROOT/build"

cd "$BUILD_DIR"
cmake ..
make -j$(nproc) # 使用所有CPU核心编译项目

echo "✅ Build complete. Starting TEST..."
cd "$PROJECT_ROOT"      # ✅ 回到项目根目录再执行
./test_server -s 8 -t 8 -c 0 -p 8080
