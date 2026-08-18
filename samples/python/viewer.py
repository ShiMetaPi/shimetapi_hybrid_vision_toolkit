#!/usr/bin/env python3
"""viewer: decode EVT3 bytes and count events."""
import hv_toolkit as hv


def main() -> None:
    enc = hv.Evt3Encoder()
    events = [hv.EventCD() for _ in range(32)]
    data = enc.encode(events)
    dec = hv.Evt3Decoder()
    out = dec.decode(data)
    print("viewer: decoded", len(out), "events")


if __name__ == "__main__":
    main()
