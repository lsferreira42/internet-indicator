#!/usr/bin/env python3
"""Simple HTTP server that prints all request details to stdout."""

from http.server import HTTPServer, BaseHTTPRequestHandler
import sys
from datetime import datetime


class DebugHandler(BaseHTTPRequestHandler):
    def _handle(self):
        print(f"\n{'='*60}")
        print(f"  {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"{'='*60}")
        print(f"Method:  {self.command}")
        print(f"Path:    {self.path}")
        print(f"Version: {self.request_version}")
        print(f"Client:  {self.client_address[0]}:{self.client_address[1]}")
        print(f"\n--- Headers ---")
        for name, value in self.headers.items():
            print(f"  {name}: {value}")

        # Read body if Content-Length is present
        content_length = self.headers.get("Content-Length")
        if content_length:
            body = self.rfile.read(int(content_length))
            print(f"\n--- Body ({content_length} bytes) ---")
            try:
                print(f"  {body.decode('utf-8')}")
            except UnicodeDecodeError:
                print(f"  (binary data: {body.hex()})")

        print(f"{'='*60}\n")
        sys.stdout.flush()

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"OK\n")

    # Accept any HTTP method
    do_GET = _handle
    do_POST = _handle
    do_PUT = _handle
    do_DELETE = _handle
    do_PATCH = _handle
    do_HEAD = _handle
    do_OPTIONS = _handle


if __name__ == "__main__":
    host, port = "0.0.0.0", 8090
    server = HTTPServer((host, port), DebugHandler)
    print(f"Listening on {host}:{port} — press Ctrl+C to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
        server.server_close()
