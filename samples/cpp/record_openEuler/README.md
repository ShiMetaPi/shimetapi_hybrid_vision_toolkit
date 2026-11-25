# openEuler APX014BA USB 录制示例

这是原 SDK 内的独立录制 sample，程序名 `hv_sample_record_openEuler`。
它运行在 openEuler RK3588 ARM64 板卡上；不依赖 Windows。
原 `samples/cpp/record` 保留自己的录制策略，两者共用 SDK。

## 与 record 的区别

- USB 初始化时丢弃最先观察到的 10 张不同 APS，打印 `initializing APS n/10`。
  重复派发不重复计数；期间 EVS 和 APS 都不保存。
- 第 11 张新 APS 就绪、输出打开后才开始 `--duration` 计时。
- RAW/AVI 共用主机 monotonic 截止点；阻塞取帧返回后再次检查，超时数据不写。
- 收尾按实际帧数/录制窗口更新 AVI 平均帧率；同时保留 APS 时间戳 CSV。
- 初始化等待最长 15 秒。当前默认录制 3 秒，非法时长失败退出。

RAW 仍写原始完整事件包，APS 仍写 Gray8/NV12 原始像素。没有逐事件精确裁切，
没有硬件同步保证；“丢 10 帧”是现有 APX014BA 样本的启动规避，不是设备根因修复。
目前只在 APX014BA USB 上做过此策略的板卡测试；MIPI 和其他型号不视为硬件验证通过。

## 编译及板卡运行

在 SDK 根目录按 `toolchains/openEuler/README.md` 设置编译器和 sysroot 后：

```sh
./run.sh build openEuler
```

生成 `build_release_openEuler/samples/cpp/record_openEuler/hv_sample_record_openEuler`。
部署 ARM64 程序及同一构建的四个 `libshimetapi_*.so*` 库后，在板卡执行：

```sh
cd /root/evs-sdk
export LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
./hv_sample_record_openEuler --duration 10
```

输出固定为 `/tmp/hv_record_openEuler.raw`、`/tmp/hv_record_openEuler.avi`、
`/tmp/hv_record_openEuler.avi.timestamps.csv`，与原 record 的输出名分开。
再次运行会覆盖本 sample 同名输出，先取回需要保留的文件。
关闭其他相机采集程序；10 秒 APS 约 400MB，另加 RAW，注意 `/tmp` 容量。
文件收尾前不要强制终止程序，否则 AVI 索引/时长可能尚未补齐。

## 在 Ubuntu 主机回放

在原 SDK 内本机构建，不需要再使用适配副本：

```sh
cmake -S . -B build_release_x86 -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_MIPI_BACKEND=OFF -DBUILD_GUI_SAMPLES=ON
cmake --build build_release_x86 --target hv_sample_recording_player -j4
export LD_LIBRARY_PATH="$PWD/build_release_x86${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
./build_release_x86/samples/cpp/recording_player/hv_sample_recording_player \
    /path/to/hv_record_openEuler.raw /path/to/hv_record_openEuler.avi
```

保留同目录的 APS CSV，播放器会自动读取。`--check` 可无窗口验证。
新三列 CSV 不能替代旧 player 使用的多列时间桥 CSV；旧 player 继续保留。
