# S100 板端验证步骤（预编译版 v2 + evs_fps 六档）

宿主机产物：`out/s100/build/`（7 个样例）+ `lib/s100/`（4 个 .so）。
目标：确认 (1) 新 `.so` 在板端可加载可运行；(2) `evs_fps` 运行时选档生效；
(3) 240 档行为与旧版一致（回归）。

## 0. 拷贝（宿主机）

```bash
# 假设板卡经 ssh 可达，用户 root，部署到 /app/hv/
scp -r lib/s100 root@<板卡IP>:/app/hv/lib
scp -r out/s100/build/samples root@<板卡IP>:/app/hv/
# 或整体打包：
# tar czf hv_s100.tar.gz lib/s100 out/s100/build/samples && scp hv_s100.tar.gz root@<板卡IP>:/app/hv/
```

注意：样例可执行文件内嵌的 RUNPATH 指向宿主机路径
（`/home/zsl/.../lib/s100`），**在板卡上不可用**，必须用 LD_LIBRARY_PATH
指向库目录（下面步骤全部带）。

## 1. 库加载验证（不碰硬件）

```bash
# 板卡上
export LD_LIBRARY_PATH=/app/hv/lib:$LD_LIBRARY_PATH
ldd /app/hv/samples/cpp/get_started/hv_sample_get_started
```

**目标结果**：`libshimetapi_*.so.2 => /app/hv/lib/...` 全部找到；
`libcam.so.1 / libvpf.so.1 / libhbmem.so.1 / libalog.so.1`（RDK 平台库）
也全部找到（板子系统自带）。**不能出现 `not found`**。
若 RDK 库缺失，`libshimetapi_hv.so` 会加载失败——那说明板子系统不带
multimedia 栈，需先装（正常 S100 出厂带）。

## 2. 六档 sensor 配置验证（不启动流）

```bash
strings /app/hv/lib/libshimetapi_hv.so.2 | grep -c "hvs_aps_binning_evs_.*fps_4lane"
```

**目标结果**：`12`（6 档 × VC0/VC1 两文件名）。旧版 .so 这里只有 6
（3 档）——这一步证明新库带全六档。

## 3. 默认档（240）回归 — MIPI-HVS 采集

```bash
cd /app/hv
export LD_LIBRARY_PATH=/app/hv/lib:$LD_LIBRARY_PATH
./samples/cpp/record/hv_sample_record --mipi-hvs
```

**目标结果**：
- stderr 打出 `MIPI-HVS: VC0 started: fps=240, sensor=..., config_file=hvs_aps_binning_evs_240fps_4lane_evs_vc0.c`
- stderr 打出 `MIPI-HVS: VC1 APS started: ... hvs_aps_binning_evs_240fps_4lane.c`
- 程序采 10 帧后正常退出，`/tmp/hv_record.raw` 有内容（约 >1MB，240 档整包 1MB × 10）

## 4. 非 240 档（500）— evs_fps 运行时选档验证

record 样例没暴露 evs_fps 参数，用一行补丁样例最直接（宿主机编译后拷过去）：

```cpp
// docs/fps_check.cpp（源码已放仓库 docs/，编译命令见下）
#include <cstdio>
#include <shimetapi/hv/camera.h>
#include <shimetapi/hv/device_config.h>
int main(int argc, char** argv) {
    Shimeta::hv::DeviceConfig cfg;
    cfg.backend = Shimeta::hv::Backend::MipiHvs;
    cfg.evs_fps = (argc > 1) ? unsigned(atoi(argv[1])) : 500;   // 档位参数
    printf("fps_check: evs_fps=%u\n", cfg.evs_fps);
    Shimeta::hv::Camera cam;
    cam.Init(cfg);
    if (!cam.StartStream()) { printf("fps_check: start FAILED\n"); return 1; }
    for (int i = 0; i < 5; ++i) {
        Shimeta::Frame f;
        if (cam.GetFrame(f, 3000))
            printf("fps_check: frame %d evs=%zu bytes aps=%zu bytes\n", i, f.evs.size, f.aps.size);
    }
    cam.StopStream(); cam.Destroy();
    return 0;
}
```

宿主机编译（进 out/s100/build 里借 CMake 环境最省事）：

```bash
cd shimetapi_Hybrid_vision_toolkit_release
aarch64-linux-gnu-g++ -std=c++17 -I include docs/fps_check.cpp \
  -L lib/s100 -lshimetapi_hv -lshimetapi_codec -lshimetapi_core \
  -Wl,--allow-shlib-undefined -Wl,-rpath,/app/hv/lib -o /tmp/fps_check
scp /tmp/fps_check root@<板卡IP>:/app/hv/
```

板卡依次跑三个档位：

```bash
export LD_LIBRARY_PATH=/app/hv/lib:$LD_LIBRARY_PATH
/app/hv/fps_check 500
/app/hv/fps_check 120
/app/hv/fps_check 1000
```

**目标结果**（以 500 为例）：
- stderr：`MIPI-HVS: VC0 started: fps=500, ..., config_file=hvs_aps_binning_evs_500fps_4lane_evs_vc0.c`
- `fps_check: frame N evs=2097152 bytes`（500 档整包 = 4096×512 = **2 MiB**，不是旧版截断的 1 MiB——这是本次修复的核心验证点）
- 各档 evs 字节数对照：120→524288、240→1048576、300→1310720、500→2097152、750→3276800、1000→4194304
- aps 字节非零（NV12 = 768×608×3/2 = 703488，经 PYM 输出可能不同尺寸但 >0）

**失败档位对照**：`fps_check 999`（非法档）应直接打印
`MIPI-HVS: invalid evs_fps=999 (supported: 120/240/300/500/750/1000)` 并
StartStream 失败退出——这是 fail-fast 防护，属预期行为。

## 5. 解码验证（可选，回主机做）

把板卡上录的 `/tmp/hv_record.raw` 拷回宿主机，用 viewer 回放：

```bash
# 宿主机
scp root@<板卡IP>:/tmp/hv_record.raw .
./out/x86_64/build/samples/cpp/viewer/hv_sample_viewer hv_record.raw
```

**目标结果**：OpenCV 窗口弹出事件累积图像（有画面、无崩溃）。

## 结果汇总表

| # | 验证项 | 命令 | 通过标准 |
|---|---|---|---|
| 1 | 库加载 | `ldd hv_sample_get_started` | 无 not found |
| 2 | 六档齐备 | `strings libshimetapi_hv.so.2 \| grep -c fps_4lane` | = 12 |
| 3 | 240 回归 | `hv_sample_record --mipi-hvs` | VC0 started fps=240，10 帧录制成功 |
| 4 | 500/120/1000 档 | `/app/hv/fps_check <fps>` | config_file 匹配档位；evs 字节=表中值 |
| 4b | 非法档 fail-fast | `/app/hv/fps_check 999` | invalid evs_fps 报错退出 |
| 5 | 解码回放（可选） | viewer 回放 raw | 窗口出图 |

任何一步不符，把该步完整 stderr 发我。
