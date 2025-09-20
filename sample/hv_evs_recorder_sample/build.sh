#!/bin/bash

# EVS Recorder 构建脚本

set -e  # 遇到错误时退出

echo "========================================"
echo "EVS Recorder 构建脚本"
echo "========================================"

# 检查依赖
echo "检查依赖..."

# 检查cmake
if ! command -v cmake &> /dev/null; then
    echo "错误: 未找到cmake，请先安装cmake"
    exit 1
fi

# 检查pkg-config
if ! command -v pkg-config &> /dev/null; then
    echo "错误: 未找到pkg-config，请先安装pkg-config"
    exit 1
fi

# 检查libusb-1.0
if ! pkg-config --exists libusb-1.0; then
    echo "错误: 未找到libusb-1.0开发库"
    echo "Ubuntu/Debian: sudo apt-get install libusb-1.0-0-dev"
    echo "CentOS/RHEL: sudo yum install libusb1-devel"
    echo "Fedora: sudo dnf install libusb1-devel"
    exit 1
fi

echo "依赖检查完成"

# 创建构建目录
BUILD_DIR="build"
if [ -d "$BUILD_DIR" ]; then
    echo "清理旧的构建目录..."
    rm -rf "$BUILD_DIR"
fi

echo "创建构建目录: $BUILD_DIR"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

# 配置项目
echo "配置项目..."
cmake ..

# 编译
echo "开始编译..."
make -j$(nproc)

echo "========================================"
echo "编译完成!"
echo "可执行文件: $BUILD_DIR/hv_evs_recorder_sample"
echo "========================================"

# 显示使用说明
echo ""
echo "使用方法:"
echo "  ./hv_evs_recorder_sample                    # 录制到默认文件"
echo "  ./hv_evs_recorder_sample output.raw        # 指定输出文件"
echo "  ./hv_evs_recorder_sample output.raw 60     # 录制60秒"
echo ""
echo "注意: 请确保在代码中设置了正确的USB设备ID"
echo "      当前设置: VENDOR_ID=0x04b4, PRODUCT_ID=0x00f1"
echo ""

# 检查是否要立即运行
read -p "是否要立即运行程序? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "启动EVS录制器..."
    ./hv_evs_recorder_sample
fi