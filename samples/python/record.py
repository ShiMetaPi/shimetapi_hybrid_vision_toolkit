#!/usr/bin/env python3
"""record: encode synthetic events to EVT3 bytes and write a RAW file (io passthrough)."""
import hv_toolkit as hv


def main() -> None:
    enc = hv.Evt3Encoder()
    events = [hv.EventCD() for _ in range(16)]
    data = enc.encode(events)
    path = "/tmp/hv_record_py.raw"
    with open(path, "wb") as fh:
        fh.write(b"% format EVT3;width=768;height=608\n% integrator_name Shimeta\n% end\n")
        fh.write(data)
    print("record: wrote", path, "bytes", len(data))


if __name__ == "__main__":
    main()
