#!/usr/bin/env python3
"""Serve a built Asuka web bundle locally with the headers threads need.

WASM threads (and therefore the emulator's CD read-ahead thread) need
SharedArrayBuffer, which browsers only expose to *cross-origin isolated*
documents. That requires two response headers:

    Cross-Origin-Opener-Policy: same-origin
    Cross-Origin-Embedder-Policy: require-corp

Python's stdlib http.server does not send them, so this tiny wrapper does.
Cloudflare Pages gets the same headers from web/_headers.

Usage:
    python3 scripts/serve_web.py [DIR] [--port 8000]

DIR defaults to web/dist (produced by scripts/build_web.sh).
"""

from __future__ import annotations

import argparse
import functools
import http.server
import os
import socketserver
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DIR = os.path.join(ROOT, "web", "dist")


class CrossOriginIsolatedHandler(http.server.SimpleHTTPRequestHandler):
    """SimpleHTTPRequestHandler that adds the COOP/COEP isolation headers."""

    def end_headers(self) -> None:  # noqa: D102 - inherited docstring
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # Correct MIME type so the browser instantiates the module directly.
        super().end_headers()

    def guess_type(self, path: str) -> str:
        if path.endswith(".wasm"):
            return "application/wasm"
        return super().guess_type(path)

    def log_message(self, fmt: str, *args: object) -> None:  # quieter logs
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", default=DEFAULT_DIR,
                        help="directory to serve (default: web/dist)")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--bind", default="127.0.0.1")
    args = parser.parse_args()

    directory = os.path.abspath(args.directory)
    if not os.path.isdir(directory):
        sys.stderr.write(
            f"error: {directory} does not exist.\n"
            "Build it first:  scripts/build_web.sh\n")
        return 1
    if not os.path.isfile(os.path.join(directory, "asuka_web.wasm")):
        sys.stderr.write(
            "warning: no asuka_web.wasm in the directory; build it with "
            "scripts/build_web.sh\n")

    handler = functools.partial(CrossOriginIsolatedHandler, directory=directory)
    with socketserver.TCPServer((args.bind, args.port), handler) as httpd:
        url = f"http://{args.bind}:{args.port}/"
        print(f"Serving {directory} with COOP/COEP at {url}")
        print("Press Ctrl+C to stop.")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nstopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
