"""Embed the offline dashboard in flash; no filesystem upload is necessary."""
from pathlib import Path
import sys

Import("env")
root = Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(root / "scripts"))
from web_header import make_header
out = Path(env.subst("$BUILD_DIR")) / "generated"
out.mkdir(parents=True, exist_ok=True)
page = (root / "web" / "index.html").read_bytes()
header = make_header(page)
target = out / "web_page.h"
if not target.exists() or target.read_text() != header:
    target.write_text(header)
env.Append(CPPPATH=[str(out)])

