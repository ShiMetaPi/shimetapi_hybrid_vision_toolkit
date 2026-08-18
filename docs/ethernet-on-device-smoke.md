# Ethernet on-device smoke test (manual, camera unmodified)

> P3 manual gate. The autonomous `/goal` gate (MockCameraClient end-to-end) already proves the
> pipeline on x86. This checklist verifies against the **real, unmodified** EVS camera over TCP.
> EVS_Device_App is frozen — the camera runs its existing binaries unchanged.

## Preconditions

- Toolkit built with Ethernet backend (default ON): `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j`.
- PC (toolkit host) and camera on the same LAN; PC IP known, e.g. `192.168.5.10`.
- Camera reachable; port 8888 open on the PC (camera protocol default).

## Steps

1. **PC (toolkit)** — run a toolkit sample/app with `Backend::Ethernet`, `listen_port=8888`,
   `bind_ip=<PC IP>` (empty = INADDR_ANY). The toolkit listens and waits for the camera to connect.
2. **Camera (unmodified)** — on the EVS_Device_App device, run:
   ```
   evs_multithread_sender <pc_ip> 8888
   ```
   The camera connects to the toolkit's listener and pushes DVS1 EVT2 packets.
3. *(Optional, mode-1 timestamps)* — run the camera-side time-sync client:
   ```
   timesync_sync_client_test.exe 1 1 <ntp> <pc_ip> 9999
   ```
   MVP mode-0 (packet-header `ts_sec`/`ts_usec`) needs no time-sync server; this is the future mode-1 path.
4. **Observe** on the PC: the toolkit `accept`s the camera connection, `readEventPacket` receives
   `Evt2Data` packets, `Evt2Decoder` decodes events, timestamps come from the packet header
   `ts_sec`/`ts_usec` (`timestampNs`).
5. **Stop** — verify: no crash; after the camera disconnects the toolkit can re-`accept` a new connection.

## Pass criteria

- EVT2 packets received and decoded into `EventCD` events (non-zero event count).
- Sequence numbers contiguous (no large `seq` gaps — `EthernetDeviceImpl::seqGaps()` stays near 0).
- No checksum failures (`readOnePacket` CRC check passes) under sustained streaming.
- `readImageFrame` stays `ErrUnsupportedFormat` (Ethernet is events-only by design — 1 Gbps is
  insufficient for EVS+APS).

## Notes

- Toolkit is the TCP **server**; the camera is the TCP **client** (push model).
- DVS1 header is 40 bytes (the camera's own comment wrongly says 32). Fields are big-endian on the wire.
- Checksum replicates the camera's frozen quirk: `CRC32(reserved) XOR CRC32(payload)`
  (the header[0..32) CRC is computed then discarded).
