#!/bin/bash

# HV Camera Frequency Demo 构建脚本

echo "=== HV Camera Frequency Demo 构建脚本 ==="

# 检查是否在正确的目录
if [ ! -f "hv_camera_frequency.cpp" ]; then
    echo "错误: 请在包含 hv_camera_frequency.cpp 的目录中运行此脚本"
    exit 1
fi

# 创建构建目录
echo "创建构建目录..."
if [ ! -d "build" ]; then
    mkdir build
fi

cd build

# 清理之前的构建文件（可选）
if [ "$1" = "clean" ]; then
    echo "清理之前的构建文件..."
    rm -rf *
fi

# 运行CMake配置
echo "配置项目..."
cmake .. -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo "错误: CMake配置失败"
    exit 1
fi

# 编译项目
echo "编译项目..."
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo "错误: 编译失败"
    exit 1
fi

echo "构建成功！"
echo "可执行文件位置: $(pwd)/hv_camera_frequency"
echo ""
echo "运行方法:"
echo "  cd build"
echo "  ./hv_camera_frequency"
echo ""
echo "或者指定USB设备ID:"
echo "  ./hv_camera_frequency 1d6b 0105"
echo ""
echo "操作说明:"
echo "  R键: 开始/停止频率计算"
echo "  ESC/Q键: 退出程序"