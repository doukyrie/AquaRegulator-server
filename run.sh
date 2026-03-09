#!/bin/bash
# AquaRegulator 启动脚本

# 获取脚本所在目录（项目根目录）
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 设置动态库搜索路径
export LD_LIBRARY_PATH="${SCRIPT_DIR}/build:${SCRIPT_DIR}/src:${LD_LIBRARY_PATH}"

# 切换到项目根目录（确保配置文件路径正确）
cd "${SCRIPT_DIR}"

# 运行程序
echo "Starting AquaRegulator..."
echo "LD_LIBRARY_PATH: ${LD_LIBRARY_PATH}"
./build/src/AquaRegS "$@"
