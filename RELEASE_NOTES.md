# 2026-11-16 APX014 USB 协议更新 / APX014 USB protocol update

内部源码基线 / Internal source baseline: `471e3663`.

## 发布内容 / Contents

- 核心继续以闭源共享库交付；仅发布公共头文件和示例源码。不包含 `src/`、`tests/`、`legacy/` 或内部 Mock `bench`。
- APX014 新协议：EVS 为 40B DVS1 包头 + EVT2 负载；APS 为 80B 帧头 + Gray8 640×1024。事件按帧号聚合，收尾需 `Flush()`；时间戳直接取包头，无自校准。
- 更新采集解码、吞吐、帧率切换、时间戳配对、录制与回放示例。录制支持 Gray8 APS，并去除重复 APS 快照。
- x86_64：更新 core/codec/io/hv 库及 Python 3.10 预编译模块。
- X5：更新 core/codec/io/hv 库；沿用 MIPI + Ethernet 后端配置。
- S100：更新 core/codec/io 库，保留原有相机/HAL 库。原相机库引用的 APX003 `extractEvsTimestamp` 接口保持不变，已检查 SDK 符号闭合；本机外部 S100 SDK 与内部相机适配层版本不匹配，因此未替换该平台相机库。

The core remains closed-source. Public headers, sample sources and matching binaries are distributed. The APX014 format changes to DVS1 EVS packets and Gray8 APS frames. x86_64 and X5 libraries are rebuilt; S100 receives updated core/codec/io libraries while retaining its existing camera/HAL binary. The S100 APX003 codec import remains compatible at the symbol level.

## 升级要求 / Migration

- 头文件、库和应用必须配套更新并重新编译。`HybridWriter::open()` 参数及公开类布局发生变化；沿用 `.so.2` 不表示二进制兼容。不要把旧应用或旧 Python 模块与新库混用。
- 旧 APX014 子帧固件及旧 1280×1024 录像不受当前解码器支持；APX003 RAW8 路径保留。
- Python 模块仅提供 x86_64 / CPython 3.10。可采集 APX014 原始数据，但尚未导出 `Evs014PacketDecoder`，不要将完整 DVS1 包直接送给 EVT2/RAW8 解码器。

Rebuild applications against the matching headers and libraries; the unchanged SONAME does not imply ABI compatibility. Legacy APX014 subframe firmware/recordings are unsupported. APX003 RAW8 remains supported. The Python module targets x86_64 / CPython 3.10 and does not export the new APX014 packet decoder.

## 验证 / Validation

- 26 项内部回归测试启用断言，并链接本次发布的 x86_64 库，全部通过；包括 EVS 包校验/解码、Gray8 录制读取、APX003 与 Ethernet 回环。
- 发布仓库独立构建通过：x86_64 9 个示例、S100 7 个示例、X5 7 个示例。
- 三个平台共享库无缺失的 SDK 内部导入符号；新构建的库使用 `$ORIGIN` 相对运行路径。
- Python 导入和离线 EVT3 编解码示例通过。
- 本次本机验证不包含连接相机的采集、GUI 目测或 ARM 板端运行；需在目标设备复测。

All 26 assertion-enabled internal regression tests pass against the release x86_64 binaries. Independent release builds succeed for 9 x86_64 samples and 7 samples each for S100/X5. SDK symbol closure and Python offline examples pass. Camera capture, GUI inspection and ARM board execution require target-device verification.

## x86_64 快速复测 / Quick verification

```bash
bash run.sh samples x86_64
./out/x86_64/build/samples/cpp/get_started/hv_sample_get_started
./out/x86_64/build/samples/cpp/callback/hv_sample_callback
./out/x86_64/build/samples/cpp/viewer/hv_sample_viewer
./out/x86_64/build/samples/cpp/bench_hw/hv_sample_bench_hw 0x1d6b 0x0105 10
./out/x86_64/build/samples/cpp/record/hv_sample_record --duration 5
# 在图形桌面运行 / Run on a graphical desktop:
./out/x86_64/build/samples/cpp/player/hv_sample_player /tmp/hv_record.raw /tmp/hv_record.avi
```

`record` 覆盖 `/tmp/hv_record.raw` 和 `/tmp/hv_record.avi`。硬件示例逐个运行；没有设备时的正常退出不代表采集通过。
`record` overwrites its two output files. Run hardware samples one at a time; a clean exit without a device is not a capture success.
