#!/bin/bash
set -e


echo "✅Starting server..."
cd "$PROJECT_ROOT"      # ✅ 回到项目根目录再执行
./build/web_server -m 1 -s 8 -t 8 -c 1 -a 1 # -c 1 关闭日志