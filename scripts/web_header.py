"""Shared dashboard embedding for PlatformIO and the Arduino IDE export."""
def make_header(page):
    text = page.decode("utf-8")
    delimiter = "WEBBREW_HTML"
    if f'){delimiter}"' in text:
        raise ValueError("Dashboard contains the reserved raw-string delimiter")
    return (
        '#pragma once\n#include <Arduino.h>\n'
        f'const char WEB_PAGE[] PROGMEM = R"{delimiter}({text}){delimiter}";\n'
    )
