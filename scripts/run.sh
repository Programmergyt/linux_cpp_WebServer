#!/bin/bash
set -e


echo "✅Starting server..."
cd "$PROJECT_ROOT"      # ✅ 回到项目根目录再执行
./web_server -m 1 -s 8 -t 8 -c 0 -a 1 # -c 0 开启日志