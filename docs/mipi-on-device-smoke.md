# MIPI on-device smoke test (manual)

> P2 delivers the MIPI/RDK backend code + ARM cross-compile proof. The autonomous run cannot
> drive the RK3588 board, so real-hardware validation is this manual checklist. Run it after
> deploying the ARM build to a board with an apx003 HVS sensor on the MIPI interface.

## Preconditions

- Board: RK3588-family (aarch64) with the apx003 HVS event sensor attached to the MIPI CSI port
  used by `sensor_index` (default `0`).
- ARM build produced on the host (P2 Phase 5):
  - `build_arm/libshimetapi_core.so`, `libshimetapi_codec.so`, `libshimetapi_io.so`,
    `libshimetapi_hv.so` (contains `MipiDeviceImpl` + the compiled `vp_sensors` source).
  - `build_arm/samples/cpp/get_started/hv_sample_get_started`.
- RDK runtime libs present on the board (`libcam`, `libvpf`, `libhbmem`, `libgdcbin`, `libalog`,
  `libcjson`) — the board's stock RDK image provides them (the dev sysroot only ships headers +
  the main `.so`).

## Deploy

1. Copy the four `.so` files and the sample executable to the board, e.g.
   `scp build_arm/libshimetapi_*.so build_arm/samples/cpp/get_started/hv_sample_get_started board:~/hv/`.
2. On the board:
   ```sh
   cd ~/hv
   export LD_LIBRARY_PATH=$(pwd):$LD_LIBRARY_PATH
   ```

## Run (Backend::Mipi)

3. The default `get_started` sample uses `Backend::Auto`. To exercise MIPI, either:
   - edit the sample's `DeviceConfig` to `cfg.backend = Shimeta::hv::Backend::Mipi;` and rebuild for
     ARM, or
   - write a tiny driver that sets `Backend::Mipi`, then `Camera::Init → StartStream → GetFrame`.
4. Expected on success:
   - `Init` returns Ok (rjgt102 secure-chip `authenticate(i2c_bus)` passes, then the RDK Vflow
     pipeline is created and started).
   - `StartStream` returns Ok.
   - `GetFrame` returns frames whose `evs.size > 0` — that buffer is the raw apx003 RAW8 packet
     (one MIPI packet = 8 frames × 4 subframes × 32768 bytes).

## Decode verification (RAW8 → events)

5. Feed the returned `Frame.evs` bytes through the codec decoder and check the result is sane:
   ```cpp
   Shimeta::codec::MipiRaw8Decoder decoder;
   std::vector<Shimeta::EventCD> events;
   decoder.Decode(frame.evs.data, frame.evs.size, events);
   // expect events.size() > 0; event.t timestamps monotonic / plausible in microseconds
   ```
6. Pass criteria: at least one RAW8 packet captured, `Decode` yields a non-zero event count, and
   timestamps look reasonable (monotonic, microsecond scale).

## Stop / soak

7. `StopStream` / let the sample exit — expect a clean shutdown (no crash, no leak reported by the
   RDK Vflow teardown path).
8. Optional soak: run for several minutes and watch board memory (`top`/`htop`) for growth.

## Pass criteria (overall)

- RAW8 frames arrive via `Backend::Mipi`.
- `MipiRaw8Decoder` decodes them to a non-zero event stream with sensible timestamps.
- Clean start/stop, no crash over a short soak.

If `Init` fails: confirm the rjgt102 I2C bus (`DeviceConfig.i2c_bus`, default 1) matches the board's
secure-chip wiring, and that `sensor_index` selects the connected apx003 HVS config.
