#!/usr/bin/env python3
"""Apply audited, idempotent Darwin/build-isolation changes to the pinned source."""
from pathlib import Path
import sys

source = Path(sys.argv[1])

def replace(path, before, after):
    text = path.read_text()
    if after in text:
        return
    if text.count(before) != 1:
        raise SystemExit(f"Unexpected upstream source in {path}: patch cannot apply")
    path.write_text(text.replace(before, after))

for kind in ("RUNTIME", "LIBRARY", "ARCHIVE"):
    leaf = "bin" if kind == "RUNTIME" else "lib"
    replace(source / "CMakeLists.txt",
            f"set(CMAKE_{kind}_OUTPUT_DIRECTORY ${{CMAKE_SOURCE_DIR}}/{leaf})",
            f"set(CMAKE_{kind}_OUTPUT_DIRECTORY ${{CMAKE_BINARY_DIR}}/{leaf})")

# Darwin/Mach-O clang rejects GCC alias attributes. Keep both real entry points.
for action in ("Enable", "Disable"):
    replace(source / "src/gl/directstate.c",
            f"AliasDecl(void,gl4es_gl{action}ClientStatei,(GLenum array, GLuint index),gl4es_gl{action}ClientStateIndexed);",
            f"void APIENTRY_GL4ES gl4es_gl{action}ClientStatei(GLenum array, GLuint index) {{\n"
            f"    gl4es_gl{action}ClientStateIndexed(array, index);\n}}")
