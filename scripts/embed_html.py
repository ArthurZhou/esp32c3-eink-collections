"""
PlatformIO extra_scripts: embed the single-file web UI (data/index.html) into
a PROGMEM C header (include/webpage.h) at build time.

So the front-end lives in one normal HTML file (editable in any editor/browser)
and gets baked into flash with @PROGMEM@, served via WebServer::send_P().

Run with:
  platformio:
    [env]
    extra_scripts = pre:scripts/embed_html.py
"""

from pathlib import Path

Import("env")  # noqa: F821  (PlatformIO injects `env`)

base = env.subst("$PROJECT_DIR")
html_path = Path(base) / "data" / "index.html"
out_path = Path(base) / "include" / "webpage.h"

if not html_path.is_file():
    print("[embed_html] WARN: %s not found, skipping" % html_path)
else:
    text = html_path.read_text(encoding="utf-8")

    # Escape into a single-line C string literal.
    # NB: use octal escapes (\ooo) for control / non-ASCII bytes, NOT \xHH —
    # hex escapes greedily swallow following hex digits (e.g. \x97 followed by
    # '2' becomes \x972, out of range). Octal is a fixed 3 digits.
    chunks = []
    for byte in text.encode("utf-8"):
        if byte == 0x22:  # double quote -> \"
            chunks.append('\\"')
        elif byte == 0x5C:  # backslash -> \\
            chunks.append("\\\\")
        elif 0x20 <= byte < 0x7F:  # printable ASCII kept literal
            chunks.append(chr(byte))
        elif byte in (0x0A, 0x0D, 0x09, 0x0C, 0x08):  # common controls
            chunks.append({0x0A: "\\n", 0x0D: "\\r", 0x09: "\\t",
                           0x0C: "\\f", 0x08: "\\b"}[byte])
        else:  # other control / non-ASCII -> 3-digit octal
            chunks.append("\\%03o" % byte)
    body = "".join(chunks)

    header = (
        "// This file is AUTO-GENERATED from data/index.html by scripts/embed_html.py.\n"
        "// Do not edit by hand.\n"
        "// clang-format off\n"
        "#pragma once\n"
        "#include <Arduino.h>\n"
        "const char kPageHtml[] PROGMEM = \"%s\";\n"
        "// clang-format on\n"
    ) % body

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(header, encoding="utf-8")
    print("[embed_html] generated %s (%d bytes -> header)" % (out_path, len(body)))
