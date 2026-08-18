#!/bin/bash
#
# run.sh — HV Toolkit 预编译版一站式入口
#
# 本仓库以预编译 .so 交付（lib/x86_64、lib/s100），run.sh 只做三件事：
# 编样例（链接预编译库）、安装（头文件+库到系统）、部署 Python 模块。
# MIPI 帧率档由 DeviceConfig.evs_fps 运行时选择（无需重编 .so）。
#
# 用法:
#   ./run.sh build   [arch] [cmake args...]    编译 7 个样例（默认本机架构）
#   ./run.sh install [arch] [prefix] [args...] 编样例 + 安装头文件与库
#   ./run.sh samples [arch]                    编样例 + 列出可执行文件
#   ./run.sh pydeploy                          部署 hv_toolkit 到 site-packages（x86_64）
#   ./run.sh --list                            列出预编译架构与就绪状态
#   ./run.sh help
#
# arch ∈ {x86_64, s100}；缺省 = 本机架构（aarch64 → s100，其余 → x86_64）
# 交叉编样例（如在 x86 上为 s100 编）由 cmake 工具链参数实现：
#   ./run.sh build s100 -DCMAKE_TOOLCHAIN_FILE=<你的-toolchain.cmake>

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
SAMPLE_NAMES="get_started callback record viewer bench_hw live_record_display player"

# ---- 架构解析 ----
resolve_arch() {
    case "$1" in
        "")   # 缺省：按本机架构
            case "$(uname -m)" in
                aarch64|arm64) printf 's100' ;;
                *)             printf 'x86_64' ;;
            esac
            ;;
        x86_64|x86|amd64) printf 'x86_64' ;;
        s100|S100|aarch64|arm64) printf 's100' ;;
        *)
            echo "ERROR: unknown arch '$1' (try: x86_64|s100)" >&2
            return 1
            ;;
    esac
}

# 预编译库目录（CMakeLists 按目标架构自动选择；这里仅校验存在）
arch_lib_dir() {
    printf '%s/lib/%s' "$SCRIPT_DIR" "$1"
}

arch_status() {
    local dir
    dir="$(arch_lib_dir "$1")"
    if [ -f "$dir/libshimetapi_hv.so" ]; then
        printf 'ready'
    else
        printf 'missing libs'
    fi
}

do_list() {
    printf 'ARCH    STATUS        PREBUILT LIBS\n'
    for a in x86_64 s100; do
        printf '%-8s %-13s %s\n' "$a" "$(arch_status "$a")" "$(arch_lib_dir "$a")"
    done
}

# ---- build：配置 + 编样例（链接预编译库）----
do_build() {
    local arch="$1"; shift || true
    local arch_input="$1"   # 原始输入（空=本机推断）
    shift || true

    echo "=== HV Toolkit prebuilt samples ==="
    echo "Arch:      $arch${arch_input:+ ($arch_input)}"
    echo "Prebuilt:  $(arch_lib_dir "$arch")"
    echo "Build:     $BUILD_DIR"
    echo

    # 本机构建（arch 输入为空）：不传 HV_TOOLKIT_ARCH，CMake 按目标架构自行推断。
    # 交叉（如 x86 上为 s100 编）：显式 arch + 交叉工具链文件，例如
    #   ./run.sh build s100 -DCMAKE_TOOLCHAIN_FILE=<toolchain.cmake>
    if [ -n "$arch_input" ]; then
        cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DHV_TOOLKIT_ARCH="$arch" "$@"
    else
        cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" "$@"
    fi
    cmake --build "$BUILD_DIR" --parallel "$(nproc)"
}

do_install() {
    local arch="$1"
    local prefix="${2:-/usr/local}"
    shift 2 2>/dev/null || true
    if [ "$prefix" = "/usr/local" ] && [ "$(id -u)" -ne 0 ]; then
        echo "ERROR: installing to /usr/local requires sudo; re-run with sudo," >&2
        echo "       or pass an explicit prefix (e.g. ./run.sh install x86_64 /tmp/hv_install)" >&2
        return 1
    fi
    cmake --install "$BUILD_DIR" --prefix "$prefix" "$@"
    echo "Installed headers + $arch libs to $prefix"
}

do_pydeploy() {
    local mod
    mod=$(ls -1 "$SCRIPT_DIR"/lib/x86_64/python/hv_toolkit*.so 2>/dev/null | head -1 || true)
    if [ -z "$mod" ]; then
        echo "ERROR: python module not found in lib/x86_64/python/" >&2
        return 1
    fi
    local sp
    sp=$(python3 -c "import site; print(site.getsitepackages()[0])" 2>/dev/null || echo "")
    if [ -z "$sp" ]; then
        echo "ERROR: cannot determine site-packages (need python3)" >&2
        return 1
    fi
    # 模块 rpath=$ORIGIN/.. —— 拷进 site-packages 后找不到同级的 libshimetapi_*.so，
    # 需先 install（或手动）把库放进系统路径，模块才能 import。
    if ! ldconfig -p 2>/dev/null | grep -q libshimetapi_hv; then
        echo "NOTE: libshimetapi_*.so not in system library path." >&2
        echo "      Run './run.sh install x86_64' (sudo) first, or set LD_LIBRARY_PATH." >&2
    fi
    cp "$mod" "$sp/"
    echo "pydeploy: $(basename "$mod") -> $sp/"
}

do_samples_list() {
    local s bin
    for s in $SAMPLE_NAMES; do
        bin="$BUILD_DIR/samples/cpp/$s/hv_sample_${s}"
        if [ -x "$bin" ]; then
            echo "OK   $bin"
        else
            echo "MISS $s"
        fi
    done
}

usage() {
    sed -n '3,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

cmd="${1:-help}"
shift || true

case "$cmd" in
    --list)
        do_list
        ;;

    build|samples)
        arch_input=""
        if [ "$#" -ge 1 ] && [[ "$1" != -* ]]; then
            arch_input="$1"; shift
        fi
        arch=$(resolve_arch "$arch_input") || exit 1
        do_build "$arch" "$arch_input" "$@"
        if [ "$cmd" = "samples" ]; then
            echo "=== sample binaries ==="
            do_samples_list
        fi
        ;;

    install)
        arch_input=""
        if [ "$#" -ge 1 ] && [[ "$1" != -* ]]; then
            arch_input="$1"; shift
        fi
        arch=$(resolve_arch "$arch_input") || exit 1
        prefix="/usr/local"
        if [ "$#" -ge 1 ] && [[ "$1" != -* ]]; then
            prefix="$1"; shift
        fi
        do_build "$arch" "$arch_input" "$@"
        do_install "$arch" "$prefix"
        ;;

    pydeploy)
        do_pydeploy
        ;;

    help|-h|--help)
        usage
        ;;

    *)
        echo "Unknown command: $cmd" >&2
        usage
        exit 1
        ;;
esac
