# 最小 aarch64 交叉工具链（系统交叉编译器版）。
# 适用：Ubuntu/Debian `apt install g++-aarch64-linux-gnu` 装的系统交叉工具链。
# 用法：./run.sh build s100 -DCMAKE_TOOLCHAIN_FILE=toolchains/toolchain-aarch64-linux-gnu.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
