# Ubuntu 桌面 RAW + AVI 播放示例

原 SDK 内的 `hv_sample_recording_player` 使用 SDK 编解码器、AVI reader 和 OpenCV，
无需相机、Qt 运行库或板卡 HDMI。在 Ubuntu 主机运行，openEuler ARM64 交叉构建跳过此桌面程序。
板卡录制使用 `record_openEuler`，无需单独 SDK 适配副本。

## 在原 SDK 根目录构建

```sh
cmake -S . -B build_release_x86 -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI_SAMPLES=ON -DENABLE_MIPI_BACKEND=OFF
cmake --build build_release_x86 --target hv_sample_recording_player -j4
export LD_LIBRARY_PATH="$PWD/build_release_x86${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
./build_release_x86/samples/cpp/recording_player/hv_sample_recording_player /path/to/hv_record_openEuler.raw /path/to/hv_record_openEuler.avi
```

需要本机 OpenCV core/imgproc/highgui/imgcodecs/videoio。GUI 请在主机桌面终端或有效 SSH X11 会话中运行。
X11 窗口未映射时不会立即误判退出；显示过后支持关闭窗口退出。

## 回放与时间戳

- 支持当前 APX014 DVS1 40 字节包头 + EVT2 载荷；文本头可能写 EVT3，以实际载荷为准。
- 另支持 Qt 标准 EVT2/TIME_HIGH。原生坐标绘制，OFF 蓝、ON 红。
- APS 支持 SDK Gray8/NV12 AVI；有 `images.avi.timestamps.csv` 时优先使用 APS 原始相对间隔，否则使用 AVI 头帧率。
  三列格式必须为 `frame,aps_ts_ns,fnv1a64`，帧数匹配、时间戳严格递增。
- CSV 内哈希供离线像素检查使用；播放器 `--check` 不逐帧验证哈希。
- EVS 按包时间戳，APS 按侧车/AVI 时间；默认各自起点近似对齐，不提供硬件同步。
  `--aps-offset SECONDS` 可手动平移 APS，正值延后。
- 不缓存所有 APS 图像；顺序读取，向后定位时重开文件。EVS 保存包索引、按需读取解码。

旧 APX014 RAW8 子帧和标准 EVT3 事件流不在本 sample 兼容范围内。
原 `samples/cpp/player` 继续保留，多列时间桥 CSV 与本 sample 的三列 CSV 不能混用。
播放器不丢前 10 帧；初始化丢帧属于录制端策略，不能修复旧文件已有的断层。

## 操作及无窗口验证

空格暂停/继续，a/d 按显示帧步进，r 重播，+/- 调速，m 切换并排/叠加/单路，q/Esc 退出。
拖动进度条定位；到结尾自动暂停。较短一路保持末帧，事件余辉窗口结束后为空。

```sh
hv_sample_recording_player events.raw images.avi --check
hv_sample_recording_player events.raw images.avi --snapshot preview.png --at 3
hv_sample_recording_player events.raw images.avi --export preview.avi
```

以上三种模式不需要 DISPLAY。导出为 MJPEG AVI，会覆盖指定输出。
`--fps` 默认 30，`--speed` 默认 1，`--trail-ms` 默认 100。
