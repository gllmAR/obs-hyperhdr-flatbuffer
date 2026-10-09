#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Mock HyperHDR FlatBuffers server for live testing.

Accepts TCP (and optionally a Unix domain socket) connections, parses the
length-prefixed frames, and logs one line per message. Register frames show
the origin string, Image frames their pixel payload size, Clear frames stand
out as tiny frames. Raw parsing only: the FlatBuffers payloads are not
decoded, which is enough to verify framing, order and teardown behaviour.

Usage:
    python3 tools/mock_hyperhdr.py [--port 19400] [--unix /path.sock] [--max-frames N]
"""
import argparse
import os
import selectors
import socket
import sys
import time


def hexdump(data, limit=24):
    return " ".join(f"{b:02x}" for b in data[:limit])


def first_string(data):
    """Longest printable-ASCII run of 3+ chars, or empty. Catches Register origins."""
    best = cur = b""
    for b in data:
        if 32 <= b < 127:
            cur += bytes([b])
            best = max(best, cur, key=len)
        else:
            cur = b""
    return best.decode() if len(best) >= 3 else ""


class Conn:
    def __init__(self, sock, tag):
        self.sock = sock
        self.tag = tag
        self.buf = bytearray()
        self.frames = 0
        self.image_bytes = 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=19400)
    ap.add_argument("--unix", default="")
    ap.add_argument("--max-frames", type=int, default=0, help="exit after N frames (0 = never)")
    args = ap.parse_args()

    sel = selectors.DefaultSelector()
    listeners = []

    tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    tcp.bind(("127.0.0.1", args.port))
    tcp.listen(4)
    tcp.setblocking(False)
    sel.register(tcp, selectors.EVENT_READ, ("listen", None))
    listeners.append(tcp)
    print(f"listening on 127.0.0.1:{args.port}", flush=True)

    if args.unix:
        if os.path.exists(args.unix):
            os.unlink(args.unix)
        us = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        us.bind(args.unix)
        us.listen(4)
        us.setblocking(False)
        sel.register(us, selectors.EVENT_READ, ("listen", None))
        listeners.append(us)
        print(f"listening on unix:{args.unix}", flush=True)

    total = 0
    try:
        while True:
            for key, _ in sel.select(timeout=1.0):
                kind, conn = key.data
                if kind == "listen":
                    client, _ = key.fileobj.accept()
                    client.setblocking(False)
                    conn = Conn(client, f"#{id(client) & 0xffff:04x}")
                    sel.register(client, selectors.EVENT_READ, ("conn", conn))
                    print(f"[{time.strftime('%H:%M:%S')}] {conn.tag} connect", flush=True)
                    continue

                try:
                    chunk = key.fileobj.recv(65536)
                except OSError:
                    chunk = b""
                if not chunk:
                    print(f"[{time.strftime('%H:%M:%S')}] {conn.tag} disconnect "
                          f"({conn.frames} frames, {conn.image_bytes} image bytes)",
                          flush=True)
                    sel.unregister(key.fileobj)
                    key.fileobj.close()
                    continue
                conn.buf += chunk

                while len(conn.buf) >= 4:
                    n = int.from_bytes(conn.buf[:4], "big")
                    if n < 1 or n > 10_000_000:
                        print(f"{conn.tag} BAD LENGTH {n}, closing", flush=True)
                        conn.buf.clear()
                        break
                    if len(conn.buf) < 4 + n:
                        break
                    payload = bytes(conn.buf[4:4 + n])
                    del conn.buf[:4 + n]
                    conn.frames += 1
                    total += 1

                    if n > 1000:
                        kind = "Image?"
                        conn.image_bytes += n
                    elif n < 48:
                        kind = "Clear/Color?"
                    else:
                        origin = first_string(payload)
                        kind = f"Register? origin='{origin}'" if origin else "frame"
                    print(f"{conn.tag} {kind} len={n} head={hexdump(payload)}", flush=True)
                    if args.max_frames and total >= args.max_frames:
                        print("max frames reached", flush=True)
                        return
    except KeyboardInterrupt:
        pass
    finally:
        for ls in listeners:
            ls.close()
        if args.unix and os.path.exists(args.unix):
            os.unlink(args.unix)


if __name__ == "__main__":
    sys.exit(main())
