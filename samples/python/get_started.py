#!/usr/bin/env python3
"""get_started: minimal Camera flow (init -> start_stream -> get_frame)."""
import hv_toolkit as hv


def main() -> None:
    cam = hv.Camera()
    cfg = hv.DeviceConfig()
    cfg.backend = hv.Backend.Usb
    cam.init(cfg)
    if not cam.start_stream():
        print("get_started: no device - start failed (expected w/o camera)")
        return
    for _ in range(10):
        f = hv.Frame()
        cam.get_frame(f, 1000)
        print("get_started: frame width", f.width)
    cam.stop_stream()
    cam.destroy()


if __name__ == "__main__":
    main()
