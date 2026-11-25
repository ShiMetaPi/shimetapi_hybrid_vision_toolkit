# openEuler RK3588 USB — prebuilt SDK

This distribution contains public headers, sample source and prebuilt SDK libraries.
The core implementation and internal tests are not included. `run.sh` compiles samples
against `lib/openEuler/`; it does not rebuild the SDK libraries.

## Cross-build on Ubuntu

Provide an ARM64 compiler and an openEuler development sysroot containing libc,
libstdc++, libusb headers/libraries and pkg-config metadata:

```sh
export OPENEULER_TOOLCHAIN_PREFIX=/path/to/bin/aarch64-none-linux-gnu
export OPENEULER_SYSROOT=/path/to/openeuler-development-sysroot
BUILD_JOBS=4 ./run.sh build openEuler
./run.sh samples openEuler
```

Output: `build_release_openEuler/`, including bundled libraries and samples.
If changing the compiler or sysroot, rename the previous build directory first
so cached pkg-config paths are not reused. No MIPI/RDK libraries are required.
On native ARM64 openEuler with development tools and libusb installed, run
`./run.sh build openEuler` directly. Specify this platform explicitly; generic
ARM64 still defaults to S100 for compatibility.

## Deploy

Copy `build_release_openEuler` to the board, or copy its four SDK libraries and
selected sample binaries together. Execute from an executable filesystem such
as `/root`; recordings go to `/tmp`:

```sh
cd /root/build_release_openEuler
export LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
./samples/cpp/get_started/hv_sample_get_started 0x1d6b 0x0105
./samples/cpp/bench_hw/hv_sample_bench_hw 0x1d6b 0x0105 10
./samples/cpp/record_openEuler/hv_sample_record_openEuler --duration 10
```

The board requires libusb-1.0.so.0 and compatible ARM64 runtime libraries.
Do not deploy host x86 libraries to the board. The library configuration is USB +
Ethernet, not S100/X5 MIPI. `fps_control` is excluded for openEuler; GUI examples
are disabled by default, and the Mock-based internal bench is not distributed.

See `samples/cpp/record_openEuler/README.md` for warmup, recording-window and
output details. Playback uses the Ubuntu host `recording_player`, not a board GUI.
Preserve the AVI timestamp CSV. There is no hardware synchronization guarantee.
