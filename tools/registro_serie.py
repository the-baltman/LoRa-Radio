#!/usr/bin/env python3
"""Serial logger and summary for LoRa-Radio range tests.

Reads the firmware's `LOG,...` lines (docs/protocolo.md sec. 9) from a serial port or from a file,
stores them as CSV and prints, per node, the figures the field-test plan asks for:
PRUEBA sent / ACKed (round-trip delivery ratio), RSSI and SNR (mean, std) in both directions,
message-id gaps, and clip results with end-to-end latency.

  Live:    python registro_serie.py --port COM5 --out logs/punto1.csv [--point "1.0 km, 1.5 m, plano"]
  Replay:  python registro_serie.py --replay logs/punto1.log
  Tip:     send `prueba 100 3000` over the same port to fire 100 PRUEBA, one every 3 s.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import csv
import statistics
import sys
import time

FIELDS = ["ms", "ev", "self", "peer", "type", "id", "rssi", "snr", "a", "b", "c", "d"]
T_PRUEBA = 3
CLIPRX_CODES = {0: "complete", 1: "partial", 2: "duplicate"}
CLIPTX_CODES = {0: "ok", 1: "partial", 2: "no_response", 3: "incomplete", 4: "aborted_sos",
                5: "remote_sos", 6: "cancelled", 7: "radio_error"}


def parse_line(line):
    """Return a dict for a LOG row, or None for anything else (boot text, STATUS, noise)."""
    line = line.strip()
    if not line.startswith("LOG,"):
        return None
    parts = line.split(",")[1:]
    if len(parts) != len(FIELDS):
        return None
    row = dict(zip(FIELDS, parts))
    try:
        for k in ("ms", "self", "peer", "type", "id", "a", "b", "c", "d"):
            row[k] = int(row[k])
        row["rssi"] = float(row["rssi"])
        row["snr"] = float(row["snr"])
    except ValueError:
        return None
    return row


def _stats(values):
    if not values:
        return "n/a"
    sd = statistics.pstdev(values) if len(values) > 1 else 0.0
    return "%.1f +- %.1f (n=%d, min %.1f, max %.1f)" % (statistics.mean(values), sd, len(values), min(values), max(values))


def summarize(rows):
    """Return the summary as a list of text lines. Pure function: unit-tested."""
    out = []
    nodes = sorted({r["self"] for r in rows})
    for n in nodes:
        mine = [r for r in rows if r["self"] == n]
        tx = [r for r in mine if r["ev"] == "TX" and r["type"] == T_PRUEBA]
        ack = [r for r in mine if r["ev"] == "ACK" and r["type"] == T_PRUEBA]
        fail = [r for r in mine if r["ev"] == "FAIL" and r["type"] == T_PRUEBA]
        rx = [r for r in mine if r["ev"] == "RX" and r["type"] == T_PRUEBA]
        out.append("== node %d ==" % n)
        if tx:
            out.append("PRUEBA sent %d, ACKed %d, no ACK %d -> round-trip delivery %.1f %%" %
                       (len(tx), len(ack), len(fail), 100.0 * len(ack) / len(tx)))
            out.append("  ACK as heard here  RSSI dBm: %s" % _stats([r["rssi"] for r in ack]))
            out.append("                     SNR  dB : %s" % _stats([r["snr"] for r in ack]))
            out.append("  as heard remotely  RSSI dBm: %s" % _stats([float(r["a"]) for r in ack]))
            out.append("                     SNR  dB : %s" % _stats([r["b"] / 100.0 for r in ack]))
        if rx:
            out.append("PRUEBA received %d" % len(rx))
            out.append("  RSSI dBm: %s" % _stats([r["rssi"] for r in rx]))
            out.append("  SNR  dB : %s" % _stats([r["snr"] for r in rx]))
            by_src = {}
            for r in rx:
                by_src.setdefault(r["peer"], []).append(r["id"])
            for src, ids in sorted(by_src.items()):
                lost = 0
                for prev, cur in zip(ids, ids[1:]):
                    d = (cur - prev) & 0xFFFF
                    if 0 < d < 0x8000:
                        lost += d - 1
                span = len(ids) + lost
                out.append("  from node %d: %d received, %d ids skipped -> one-way delivery %.1f %%" %
                           (src, len(ids), lost, 100.0 * len(ids) / span))
        ctx = [r for r in mine if r["ev"] == "CLIPTX"]
        for code in sorted({r["a"] for r in ctx}):
            sel = [r for r in ctx if r["a"] == code]
            out.append("clips sent -> %s: %d, latency PTT release -> ACK ms: %s" %
                       (CLIPTX_CODES.get(code, str(code)), len(sel), _stats([float(r["d"]) for r in sel])))
        crx = [r for r in mine if r["ev"] == "CLIPRX"]
        for code in sorted({r["a"] for r in crx}):
            sel = [r for r in crx if r["a"] == code]
            out.append("clips received %s: %d, announce -> complete ms: %s, retry rounds: %s" %
                       (CLIPRX_CODES.get(code, str(code)), len(sel), _stats([float(r["d"]) for r in sel]),
                        _stats([float(r["c"]) for r in sel])))
    if not nodes:
        out.append("(no LOG rows)")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="serial port, e.g. COM5 or /dev/ttyUSB0")
    src.add_argument("--replay", help="text file with captured serial output")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", help="CSV file to write (live or replay)")
    ap.add_argument("--point", default="", help="free text stored as a comment: distance, antenna heights, terrain")
    args = ap.parse_args(argv)

    rows = []
    sink = None
    writer = None
    if args.out:
        sink = open(args.out, "w", newline="")
        if args.point:
            sink.write("# point: %s\n" % args.point)
        sink.write("# host_time,%s\n" % ",".join(FIELDS))
        writer = csv.writer(sink)

    def handle(line):
        row = parse_line(line)
        if row is None:
            return
        rows.append(row)
        if writer:
            writer.writerow([time.strftime("%Y-%m-%dT%H:%M:%S")] + [row[k] for k in FIELDS])
            sink.flush()

    try:
        if args.replay:
            with open(args.replay, "r", errors="replace") as f:
                for line in f:
                    handle(line)
        else:
            try:
                import serial  # pyserial
            except ImportError:
                sys.exit("pyserial is required for --port: pip install pyserial")
            with serial.Serial(args.port, args.baud, timeout=1) as ser:
                print("logging %s @ %d, Ctrl+C to stop" % (args.port, args.baud))
                while True:
                    raw = ser.readline()
                    if raw:
                        handle(raw.decode("utf-8", errors="replace"))
    except KeyboardInterrupt:
        pass
    finally:
        if sink:
            sink.close()
    if args.point:
        print("point:", args.point)
    print("\n".join(summarize(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
