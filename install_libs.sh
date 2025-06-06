#!/bin/bash

# HV Toolkit 库文件安装脚本
# 安装预编译的库文件、头文件和CMake配置文件
# 安装后可以使用 find_package(HVToolkit) 调用

set -e  # 遇到错误立即退出

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$SCRIPT_DIR"
LIB_DIR="$SOURCE_DIR/lib"
BUILD_DIR="$SOURCE_DIR/build_install"
INSTALL_PREFIX="${1:-/usr/local}"

echo "=== HV Toolkit 库文件安装 ==="
echo "源码目录: $SOURCE_DIR"
echo "库文件目录: $LIB_DIR"
echo "安装前缀: $INSTALL_PREFIX"
echo

# 检查库文件是否存在
REQUIRED_LIBS=(
    "$LIB_DIR/libhv_camera.so"
    "$LIB_DIR/libhv_evt2_codec.so"
    "$LIB_DIR/libhv_event_writer.so"
    "$LIB_DIR/libhv_event_reader.so"
)

echo "检查预编译库文件..."
for lib in "${REQUIRED_LIBS[@]}"; do
    if [ ! -f "$lib" ]; then
        echo "错误: 找不到预编译库文件: $lib"
        echo "请先运行 build_libs.sh 编译库文件"
        exit 1
    fi
    echo "✓ 找到: $(basename "$lib")"
done

# 检查头文件是否存在
echo "检查头文件..."
if [ ! -d "$SOURCE_DIR/include" ]; then
    echo "错误: 找不到头文件目录: $SOURCE_DIR/include"
    exit 1
fi
echo "✓ 头文件目录存在"

# 清理构建目录
echo "清理构建目录..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# 使用预编译库版本的CMakeLists.txt进行安装
echo "配置安装..."
cd "$BUILD_DIR"

# 检查预编译版本的CMakeLists.txt是否存在
if [ ! -f "$SOURCE_DIR/CMakeLists_prebuilt.txt" ]; then
    echo "错误: 找不到预编译版本的CMakeLists.txt文件"
    echo "请确保 CMakeLists_prebuilt.txt 文件存在"
    exit 1
fi

# 使用预编译版本的CMakeLists.txt
cp "$SOURCE_DIR/CMakeLists_prebuilt.txt" "$BUILD_DIR/CMakeLists.txt"

# 复制配置文件模板到构建目录
cp "$SOURCE_DIR/cmake/HVToolkitConfig_prebuilt.cmake.in" "$BUILD_DIR/" || {
    echo "错误: 无法复制 HVToolkitConfig_prebuilt.cmake.in"
    exit 1
}

cp "$SOURCE_DIR/cmake/hv_toolkit_prebuilt.pc.in" "$BUILD_DIR/" || {
    echo "错误: 无法复制 hv_toolkit_prebuilt.pc.in"
    exit 1
}
cmake "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"

echo "开始安装..."
make install

# 设置库文件的执行权限
echo "设置库文件权限..."
chmod 755 "$INSTALL_PREFIX/lib/libhv_camera.so" 2>/dev/null || true
chmod 755 "$INSTALL_PREFIX/lib/libhv_evt2_codec.so" 2>/dev/null || true
chmod 755 "$INSTALL_PREFIX/lib/libhv_event_writer.so" 2>/dev/null || true
chmod 755 "$INSTALL_PREFIX/lib/libhv_event_reader.so" 2>/dev/null || true

# 更新动态链接库缓存（如果有权限）
echo "更新动态链接库缓存..."
if command -v ldconfig >/dev/null 2>&1; then
    if [ "$EUID" -eq 0 ]; then
        ldconfig
        echo "✓ 动态链接库缓存已更新"
    else
        echo "⚠ 需要root权限更新动态链接库缓存，请手动运行: sudo ldconfig"
    fi
fi

echo
echo "=== 安装完成 ==="
echo "HV Toolkit 已安装到: $INSTALL_PREFIX"
echo "库文件位置: $INSTALL_PREFIX/lib"
echo "头文件位置: $INSTALL_PREFIX/include/hv_toolkit"
echo "CMake配置: $INSTALL_PREFIX/lib/cmake/HVToolkit"
echo "pkg-config: $INSTALL_PREFIX/lib/pkgconfig/hv_toolkit.pc"
echo
echo "使用方法:"
echo "在你的CMakeLists.txt中添加:"
echo "  find_package(HVToolkit REQUIRED)"
echo "  target_link_libraries(your_target HVToolkit::hv_camera)"
echo
echo "可用的库目标:"
echo "  - HVToolkit::hv_camera"
echo "  - HVToolkit::hv_evt2_codec"
echo "  - HVToolkit::hv_event_writer"
echo "  - HVToolkit::hv_event_reader"
echo

# 清理构建目录
rm -rf "$BUILD_DIR"

echo "安装完成！"