"""Local-only static UI demo. Open /?demo=1; never controls an ESP32."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", type=int, default=8080)
args = parser.parse_args()
web = Path(__file__).resolve().parents[1] / "web"
handler = partial(SimpleHTTPRequestHandler, directory=str(web))
print(f"Interface simulation on loopback port {args.port}; append /?demo=1", flush=True)
ThreadingHTTPServer(("127.0.0.1", args.port), handler).serve_forever()

