#!/usr/bin/env python3
"""get_started_mipi: 最小 MIPI HVS 采集(RDK ARM64，如 S100)。

与 USB 版(get_started.py)同一套 hv_toolkit API，区别仅在 DeviceConfig：
- backend = MipiHvs(双 VC：VC0 出 EVS、VC1 出 APS)
- 用 sensor_index / i2c_bus 而非 VID/PID
- evs_fps 选帧率档(0=默认 240；可选 120/240/300/500/750/1000)，Init 时生效
- Frame.evs 是 apx003 RAW8 子帧流，必须用 MipiRaw8Decoder(不是 Evt2/Evt3)

板卡运行(部署见 README「Python」一节)：
    LD_LIBRARY_PATH=<libshimetapi_*.so 目录> python3 get_started_mipi.py
"""
import argparse

import hv_toolkit as hv


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="HV Toolkit MIPI HVS get-started")
    p.add_argument("-s", "--sensor", type=int, default=0,
                   help="RDK sensor index(VC0 EVS 配置索引，默认 0)")
    p.add_argument("-i", "--i2c", type=int, default=1,
                   help="安全芯片认证 I2C 总线(默认 1)")
    p.add_argument("-n", "--node", default="/dev/video0",
                   help="MIPI 设备节点(默认 /dev/video0)")
    p.add_argument("-c", "--count", type=int, default=10,
                   help="采集帧数(默认 10)")
    p.add_argument("-f", "--fps", type=int, default=0,
                   help="EVS 帧率档(默认 0=240；可选 120/240/300/500/750/1000)")
    return p.parse_args()


def main() -> None:
    args = parse_args()

    cfg = hv.DeviceConfig()
    cfg.backend = hv.Backend.MipiHvs
    cfg.device_node = args.node
    cfg.sensor_index = args.sensor
    cfg.i2c_bus = args.i2c
    cfg.evs_fps = args.fps   # 0=默认 240；档位在 Init 时选传感器配置

    cam = hv.Camera()
    cam.init(cfg)
    if not cam.start_stream():
        print("get_started_mipi: MIPI 设备启动失败(检查 sensor_index / i2c_bus / 载板配置)")
        cam.destroy()
        return

    print(f"get_started_mipi: MIPI-HVS 已启动 sensor={args.sensor} i2c={args.i2c}")

    # MIPI HVS 的 EVS 是 RAW8 子帧流 —— 必须用 MipiRaw8Decoder。
    # decode 不传 subframe_count 即自动档：按帧数据长度解全部子帧(任意帧率档通用)。
    decoder = hv.MipiRaw8Decoder()

    for i in range(args.count):
        f = hv.Frame()
        if not cam.get_frame(f, 1000):
            print(f"frame {i}: 超时")
            continue
        events = decoder.decode(bytes(f.evs))   # f.evs 是零拷贝 numpy uint8 视图
        aps_bytes = int(f.aps.nbytes)           # APS 通常为 NV12 原始字节
        print(f"frame {i}: {len(events)} events, aps={aps_bytes} bytes "
              f"({f.width}x{f.height}), fmt={f.format}")

    cam.stop_stream()
    cam.destroy()
    print("get_started_mipi: done")


if __name__ == "__main__":
    main()
