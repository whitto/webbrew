"""Embed the offline dashboard in flash; no filesystem upload is necessary."""
from pathlib import Path

Import("env")
root = Path(env.subst("$PROJECT_DIR"))
out = Path(env.subst("$BUILD_DIR")) / "generated"
out.mkdir(parents=True, exist_ok=True)
page = (root / "web" / "index.html").read_bytes()
header = '#pragma once\n#include <Arduino.h>\nconst char WEB_PAGE[] PROGMEM = {'
header += ",".join(str(b) for b in page) + ',0};\n'
target = out / "web_page.h"
if not target.exists() or target.read_text() != header:
    target.write_text(header)
env.Append(CPPPATH=[str(out)])

