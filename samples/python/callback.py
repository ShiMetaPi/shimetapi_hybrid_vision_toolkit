#!/usr/bin/env python3
"""callback: 事件 / 图像计数演示。

Python 绑定暂未导出异步回调（set_frame_callback / set_event_callback /
set_image_callback —— 这些在 C++ API 中提供，见 samples/cpp/callback）。
本样例改用 get_frame 同步轮询，演示等价的“逐帧统计事件数 + APS 帧数”处理模式：
每取到一帧就解码 Frame.evs、累计事件数，并对 APS 帧计数。
"""
import hv_toolkit as hv


def main() -> None:
    cam = hv.Camera()
    cfg = hv.DeviceConfig()
    cfg.backend = hv.Backend.Usb
    cam.init(cfg)
    if not cam.start_stream():
        print("callback: no device - start failed (expected w/o camera)")
        return

    decoder = hv.Evt2Decoder()
    frame = hv.Frame()
    total_events = 0
    aps_frames = 0

    for _ in range(10):
        if not cam.get_frame(frame, 1000):
            continue
        events = decoder.decode(bytes(frame.evs))   # → numpy 结构数组
        total_events += len(events)
        if frame.aps.nbytes > 0:
            aps_frames += 1
        print(f"callback: frame {frame.frame_id} {len(events)} events "
              f"(total {total_events}), aps {frame.aps.nbytes} bytes")

    cam.stop_stream()
    cam.destroy()
    print(f"callback: done — {total_events} events, {aps_frames} APS frames")


if __name__ == "__main__":
    main()
