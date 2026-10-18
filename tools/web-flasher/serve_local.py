#!/usr/bin/env python3
"""
serve_local.py — Local development server for the Homepane Web Flasher.

Copies the latest ESP-IDF build artefacts into a 'firmware/' sub-directory
next to index.html, generates a self-signed TLS certificate (required by
the Web Serial API), and starts a local HTTPS server.

Usage
-----
    python3 tools/web-flasher/serve_local.py          # default port 8443
    python3 tools/web-flasher/serve_local.py --port 9000
    python3 tools/web-flasher/serve_local.py --build-dir /path/to/build

The script should be run from the project root.
"""

from __future__ import annotations

import argparse
import http.server
import os
import platform
import ssl
import subprocess
import sys
import textwrap
from pathlib import Path

# ── Paths ────────────────────────────────────────────────────────────────

SCRIPT_DIR   = Path(__file__).resolve().parent          # tools/web-flasher/
PROJECT_ROOT = SCRIPT_DIR.parents[1]                    # repo root
ESP_BUILD    = PROJECT_ROOT / "platforms" / "esp32p4" / "build"

# Self-signed cert files (generated once, reused)
CERT_DIR  = SCRIPT_DIR / ".certs"
CERT_FILE = CERT_DIR / "localhost.pem"
KEY_FILE  = CERT_DIR / "localhost-key.pem"


# ── Helpers ──────────────────────────────────────────────────────────────

def copy_artefacts(build_dir: Path) -> None:
    """Firmware parts and manifests next to index.html (make_manifests.py)."""
    subprocess.run(
        [sys.executable, str(SCRIPT_DIR / "make_manifests.py"),
         "--build-dir", str(build_dir), "--out", str(SCRIPT_DIR)],
        check=True,
    )


def generate_self_signed_cert() -> None:
    """Create a self-signed TLS certificate for localhost using openssl."""
    if CERT_FILE.exists() and KEY_FILE.exists():
        print(f"  Reusing existing certificate in {CERT_DIR}")
        return

    CERT_DIR.mkdir(parents=True, exist_ok=True)

    # Use openssl to generate a self-signed cert valid for 365 days
    cmd = [
        "openssl", "req",
        "-x509",
        "-newkey", "rsa:2048",
        "-keyout", str(KEY_FILE),
        "-out",    str(CERT_FILE),
        "-days",   "365",
        "-nodes",
        "-subj",   "/CN=localhost",
        "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
    ]

    try:
        subprocess.run(cmd, check=True, capture_output=True)
        print(f"  Generated self-signed certificate in {CERT_DIR}")
    except FileNotFoundError:
        print("ERROR: 'openssl' not found on PATH.", file=sys.stderr)
        print("Install OpenSSL or use mkcert for trusted local certs.", file=sys.stderr)
        sys.exit(1)
    except subprocess.CalledProcessError as exc:
        # Older OpenSSL versions may not support -addext; retry without it
        cmd_fallback = [c for c in cmd if c != "-addext" and c != "subjectAltName=DNS:localhost,IP:127.0.0.1"]
        try:
            subprocess.run(cmd_fallback, check=True, capture_output=True)
            print(f"  Generated self-signed certificate in {CERT_DIR} (no SAN)")
        except subprocess.CalledProcessError:
            print(f"ERROR: openssl failed:\n{exc.stderr.decode()}", file=sys.stderr)
            sys.exit(1)


def open_browser(url: str) -> None:
    """Try to open the URL in the default browser."""
    system = platform.system()
    try:
        if system == "Darwin":
            subprocess.Popen(["open", url])
        elif system == "Linux":
            subprocess.Popen(["xdg-open", url])
        elif system == "Windows":
            os.startfile(url)  # type: ignore[attr-defined]
    except Exception:
        pass  # Non-critical


# ── HTTPS Server ─────────────────────────────────────────────────────────

class QuietHandler(http.server.SimpleHTTPRequestHandler):
    """Serve files from the web-flasher directory with CORS headers."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(SCRIPT_DIR), **kwargs)

    def end_headers(self):
        # CORS headers so the page can load the manifest and firmware
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()

    def log_message(self, fmt, *args):
        # Quieter logging: skip 200 OK noise
        if len(args) >= 2 and str(args[1]) == "200":
            return
        super().log_message(fmt, *args)


def serve(port: int) -> None:
    """Start the HTTPS server."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(str(CERT_FILE), str(KEY_FILE))

    with http.server.HTTPServer(("0.0.0.0", port), QuietHandler) as httpd:
        httpd.socket = context.wrap_socket(httpd.socket, server_side=True)

        url = f"https://localhost:{port}"
        print(textwrap.dedent(f"""
        ╔══════════════════════════════════════════════════════╗
        ║  Homepane Web Flasher — Local Development Server  ║
        ╚══════════════════════════════════════════════════════╝

          URL:  {url}

          Your browser will warn about the self-signed certificate.
          Click "Advanced" → "Proceed to localhost" to continue.

          Press Ctrl+C to stop.
        """))

        open_browser(url)

        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped.")


# ── Main ─────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Local HTTPS dev server for the Homepane Web Flasher."
    )
    parser.add_argument(
        "--port", type=int, default=8443,
        help="Port to listen on (default: 8443)",
    )
    parser.add_argument(
        "--build-dir", type=Path, default=ESP_BUILD,
        help=f"Path to ESP-IDF build directory (default: {ESP_BUILD})",
    )
    parser.add_argument(
        "--skip-copy", action="store_true",
        help="Skip copying firmware artefacts (use existing firmware/ dir)",
    )
    args = parser.parse_args()

    print("\n── Homepane Web Flasher: Local Server ──\n")

    if not args.skip_copy:
        print("Copying firmware artefacts …")
        copy_artefacts(args.build_dir)
        print()

    print("Preparing TLS certificate …")
    generate_self_signed_cert()

    serve(args.port)


if __name__ == "__main__":
    main()
