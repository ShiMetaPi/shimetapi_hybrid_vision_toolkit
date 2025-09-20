#!/bin/bash

# Raw数据处理器编译脚本

echo "Raw数据处理器编译脚本"
echo "========================"

# 检查是否存在源文件
if [ ! -f "hv_raw_data_processor.cpp" ]; then
    echo "错误: 找不到 hv_raw_data_processor.cpp 文件"
    exit 1
fi



if [ ! -f "CMakeLists.txt" ]; then
    echo "错误: 找不到 CMakeLists.txt 文件"
    exit 1
fi

# 清理之前的构建文件
echo "清理之前的构建文件..."
if [ -d "build" ]; then
    echo "删除现有的 build 目录"
    rm -rf build
fi

# 清理可能存在的其他构建文件
echo "清理其他构建文件..."
rm -f hv_raw_data_processor

rm -f *.o
rm -f CMakeCache.txt
rm -f Makefile
rm -rf CMakeFiles

# 创建构建目录
echo "创建新的构建目录..."
mkdir -p build
cd build

# 复制源文件
cp ../CMakeLists.txt ./CMakeLists.txt
cp ../hv_raw_data_processor.cpp ./


echo "配置项目..."
# 运行CMake配置
cmake . -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo "错误: CMake配置失败"
    echo "请确保已安装以下依赖:"
    echo "  - OpenCV"
    echo "  - Metavision SDK (可选，如果没有会尝试使用头文件)"
    exit 1
fi

echo "编译项目..."
# 编译
make -j$(nproc)

if [ $? -eq 0 ]; then
    echo "编译成功!"
    echo "可执行文件位置:"
    echo "  - $(pwd)/hv_raw_data_processor"

    echo ""
    echo "使用方法:"
    echo "  # 原始数据处理器:"
    echo "  ./hv_raw_data_processor <raw_file_path> [output_csv_file]"
    echo ""

    echo ""
    echo "示例:"
    echo "  ./hv_raw_data_processor ../data.raw events.csv"

else
    echo "编译失败!"
    exit 1
fi

