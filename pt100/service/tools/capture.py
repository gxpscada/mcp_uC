"""Capture stage-2 four-column raw ADC stream; requires pyserial.

python tools/capture.py COM5 capture.txt --seconds 60
Host timing is only for capture duration, not an ADC measurement timestamp.
"""
import argparse
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("output", type=Path)
    parser.add_argument("--seconds", type=float, default=0, help="0 = until Ctrl+C")
    args = parser.parse_args()
    if args.seconds < 0:
        parser.error("--seconds must be nonnegative")
    import serial

    count = rejected = 0
    started = time.monotonic()
    try:
        # Exclusive creation prevents overwriting earlier measurements.
        with args.output.open("x", encoding="ascii", newline="") as dest:
            with serial.Serial(args.port, 921600, timeout=0.5) as port:
                pending = bytearray()
                while not args.seconds or time.monotonic() - started < args.seconds:
                    pending.extend(port.read(max(1, min(port.in_waiting, 4096))))
                    while b"\n" in pending:
                        raw_line, _, pending = pending.partition(b"\n")
                        try:
                            values = [int(x) for x in raw_line.split()]
                            if len(values) != 4 or any(v < -1 or v > 32767 for v in values):
                                raise ValueError("invalid frame")
                        except ValueError:
                            rejected += 1
                            continue
                        dest.write(" ".join(map(str, values)) + "\r\n")
                        dest.flush()
                        count += 1
                    if len(pending) > 4096:
                        pending.clear()
                        rejected += 1
    except KeyboardInterrupt:
        pass
    print(f"Saved {count} rows; rejected {rejected}; elapsed {time.monotonic() - started:.1f}s")


if __name__ == "__main__":
    main()
