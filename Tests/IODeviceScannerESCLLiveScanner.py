#!/usr/bin/env python3
"""A small eSCL (AirScan) scanner that answers only over HTTPS, for
IODeviceScannerESCLLiveTest to scan from.

There is no reference eSCL scanner to run the way CUPS's ippeveprinter is run
for IPP, so this is one: the four calls a client needs, served with the
self-signed certificate it is given, the way real scanners serve theirs.

  GET    /eSCL/ScannerCapabilities          -> the capability XML
  POST   /eSCL/ScanJobs                     -> 201 + Location: the job's URL
  GET    /eSCL/ScanJobs/<id>/NextDocument   -> 200 + a PNG page, 404 when spent
  DELETE /eSCL/ScanJobs/<id>                -> 200, the job released

The 404 on NextDocument is how a real scanner says the feeder is empty: it ends
a run, it does not fail it.

Every request it receives is appended to the --log file, one line each -
"METHOD path", and for a new job its source and colour mode - so the test can
see what reached the scanner, and in which order.

    IODeviceScannerESCLLiveScanner.py --cert C --key K --ready FILE --log FILE
                                      [--port N] [--pages N]

It listens on 127.0.0.1, on --port, or on a free port when that is 0, and once
it is listening writes the port to --ready.
"""

import argparse
import http.server
import os
import re
import socketserver
import ssl
import struct
import threading
import uuid
import zlib

SCAN_NS = "http://schemas.hp.com/imaging/escl/2011/05/03"
PWG_NS = "http://www.pwg.org/schemas/2010/12/sm"

MAKE_AND_MODEL = "Acme MegaScan 42"
SERIAL_NUMBER = "SN-0001"
PAGE_WIDTH, PAGE_HEIGHT = 64, 88


def setting_profile():
    resolutions = "".join(
        f"<scan:DiscreteResolution><scan:XResolution>{dpi}</scan:XResolution>"
        f"<scan:YResolution>{dpi}</scan:YResolution></scan:DiscreteResolution>"
        for dpi in (75, 150, 300, 600))
    return f"""<scan:SettingProfiles><scan:SettingProfile>
      <scan:ColorModes>
        <scan:ColorMode>BlackAndWhite1</scan:ColorMode>
        <scan:ColorMode>Grayscale8</scan:ColorMode>
        <scan:ColorMode>RGB24</scan:ColorMode>
      </scan:ColorModes>
      <scan:DocumentFormats>
        <pwg:DocumentFormat>image/png</pwg:DocumentFormat>
        <scan:DocumentFormatExt>image/png</scan:DocumentFormatExt>
      </scan:DocumentFormats>
      <scan:SupportedResolutions><scan:DiscreteResolutions>{resolutions}</scan:DiscreteResolutions></scan:SupportedResolutions>
    </scan:SettingProfile></scan:SettingProfiles>"""


def input_caps(max_height):
    return f"""<scan:MinWidth>16</scan:MinWidth><scan:MaxWidth>2550</scan:MaxWidth>
      <scan:MinHeight>16</scan:MinHeight><scan:MaxHeight>{max_height}</scan:MaxHeight>
      {setting_profile()}"""


CAPABILITIES = f"""<?xml version="1.0" encoding="UTF-8"?>
<scan:ScannerCapabilities xmlns:scan="{SCAN_NS}" xmlns:pwg="{PWG_NS}">
  <pwg:Version>2.63</pwg:Version>
  <pwg:MakeAndModel>{MAKE_AND_MODEL}</pwg:MakeAndModel>
  <pwg:SerialNumber>{SERIAL_NUMBER}</pwg:SerialNumber>
  <scan:Platen><scan:PlatenInputCaps>{input_caps(3508)}</scan:PlatenInputCaps></scan:Platen>
  <scan:Adf>
    <scan:AdfSimplexInputCaps>{input_caps(4200)}</scan:AdfSimplexInputCaps>
    <scan:AdfDuplexInputCaps>{input_caps(4200)}</scan:AdfDuplexInputCaps>
    <scan:AdfOptions><scan:AdfOption>Duplex</scan:AdfOption></scan:AdfOptions>
  </scan:Adf>
</scan:ScannerCapabilities>""".encode()


def png_page(grey, channels):
    """A small valid PNG, grey or RGB, every pixel the same shade."""
    colour_type = 0 if channels == 1 else 2
    row = b"\x00" + bytes([grey]) * (PAGE_WIDTH * channels)
    raw = row * PAGE_HEIGHT

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", PAGE_WIDTH, PAGE_HEIGHT, 8, colour_type, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw))
            + chunk(b"IEND", b""))


def element(name, xml):
    found = re.search(r"<(?:\w+:)?" + name + r">([^<]*)<", xml)
    return found.group(1) if found else ""


class Scanner:
    def __init__(self, log_path, pages_per_run):
        self.log_path = log_path
        self.pages_per_run = pages_per_run
        self.jobs = {}          # id -> [pages left, channels, pages given]
        self.lock = threading.Lock()

    def log(self, line):
        with self.lock, open(self.log_path, "a", encoding="utf-8") as log:
            log.write(line + "\n")


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    scanner = None

    def log_message(self, *args):
        pass

    def send(self, code, body=b"", content_type="text/xml", headers=None):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        if body and self.command != "HEAD":
            self.wfile.write(body)

    def do_HEAD(self):
        self.scanner.log(f"HEAD {self.path}")
        self.send(404)

    def do_GET(self):
        self.scanner.log(f"GET {self.path}")
        if self.path == "/eSCL/ScannerCapabilities":
            return self.send(200, CAPABILITIES)
        match = re.fullmatch(r"/eSCL/ScanJobs/(\w+)/NextDocument", self.path)
        if match:
            with self.scanner.lock:
                job = self.scanner.jobs.get(match.group(1))
                if job is None or job[0] <= 0:
                    # The feeder is empty. This ends the run; it does not fail it.
                    return self.send(404)
                job[0] -= 1
                job[2] += 1
                grey, channels = 40 + 60 * job[2], job[1]
            return self.send(200, png_page(grey, channels), "image/png")
        return self.send(404)

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode("utf-8", "replace")
        source = element("InputSource", body)
        colour = element("ColorMode", body)
        self.scanner.log(f"POST {self.path} source={source} color={colour}")
        if self.path != "/eSCL/ScanJobs":
            return self.send(404)
        job = uuid.uuid4().hex[:12]
        pages = self.scanner.pages_per_run if source == "Feeder" else 1
        with self.scanner.lock:
            self.scanner.jobs[job] = [pages, 1 if colour.startswith("Grayscale") else 3, 0]
        host = self.headers.get("Host", "127.0.0.1")
        return self.send(201, b"", "text/plain", {"Location": f"https://{host}/eSCL/ScanJobs/{job}"})

    def do_DELETE(self):
        self.scanner.log(f"DELETE {self.path}")
        with self.scanner.lock:
            self.scanner.jobs.pop(self.path.rstrip("/").rsplit("/", 1)[-1], None)
        return self.send(200)


class TlsServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address, context):
        super().__init__(address, Handler)
        self.context = context

    def server_bind(self):
        # HTTPServer's own server_bind asks socket.getfqdn() for the address's
        # name - a reverse DNS lookup that stalled for more than fifteen
        # seconds on a macOS CI runner, which the test took for a scanner that
        # would not start. The name is never used here, so it is not asked.
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address[:2]

    def finish_request(self, request, client_address):
        # The handshake is done here, on the connection's own thread, rather
        # than by wrapping the listening socket, so a client that walks away
        # from it - as one refusing this scanner's key does - holds up no
        # other connection, and is not an error.
        try:
            secured = self.context.wrap_socket(request, server_side=True)
        except (ssl.SSLError, OSError):
            return
        try:
            super().finish_request(secured, client_address)
        finally:
            try:
                secured.close()
            except OSError:
                pass


def main():
    parser = argparse.ArgumentParser(description="An eSCL scanner served over HTTPS only")
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--ready", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--pages", type=int, default=3)
    args = parser.parse_args()

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(args.cert, args.key)
    Handler.scanner = Scanner(args.log, args.pages)

    server = TlsServer(("127.0.0.1", args.port), context)
    port = server.server_address[1]
    # Written whole and then renamed, so the test never reads half a number.
    with open(args.ready + ".tmp", "w", encoding="utf-8") as ready:
        ready.write(str(port))
    os.replace(args.ready + ".tmp", args.ready)
    print(f"listening on 127.0.0.1:{port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
