#!/usr/bin/env python3
"""Include the unmodified current brain interface and bind its real exports.

Keeping the resolver in the same translation unit gives every function its actual
prototype, including the C++ reference types in this otherwise flat C ABI. There
are no fake void(void) declarations and no runtime dynamic-library loading.
"""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).resolve()
output = pathlib.Path(sys.argv[2])
header = pathlib.Path(__file__).with_name("static_brain.h").resolve()
text = source.read_text()
names = list(dict.fromkeys(re.findall(
    r'extern\s+"C"\s+__declspec\(dllexport\)\s+[^;{}]*?\b__cdecl\s+(\w+)\s*\(', text
)))
if not names or "CNC_Init" not in names or "CNC_Start_Custom_Instance" not in names:
    raise SystemExit("The engine export format changed; inspect before generating bindings.")
quote = lambda p: str(p).replace("\\", "\\\\").replace('"', '\\"')
lines = [
    "// Generated from the selected runtime source. Do not edit this build output.",
    '#include "' + quote(source) + '"',
    '#include "' + quote(header) + '"',
    "#include <cstring>",
    "struct IOSBrainExport { const char *name; void *address; };",
    "static const IOSBrainExport iosBrainExports[] = {",
]
lines += ['    {"%s", reinterpret_cast<void *>(&%s)},' % (n, n) for n in names]
lines += [
    "};",
    'extern "C" size_t opencnc3d_ios_brain_symbol_count(void) {',
    "    return sizeof iosBrainExports / sizeof iosBrainExports[0];",
    "}",
    'extern "C" void *opencnc3d_ios_brain_symbol(const char *name) {',
    "    if (!name) return nullptr;",
    "    for (const IOSBrainExport &entry : iosBrainExports)",
    "        if (std::strcmp(name, entry.name) == 0) return entry.address;",
    "    return nullptr;",
    "}",
    'static const char *iosBrainError = nullptr;',
    'extern "C" void *cnc_ios_dlopen(const char *name, int) {',
    '    if (name && (std::strstr(name, "TiberianDawn") || std::strcmp(name, "static:TD") == 0)) {',
    '        iosBrainError = nullptr; return const_cast<IOSBrainExport *>(iosBrainExports);',
    '    }',
    '    iosBrainError = "Only the bundled static Tiberian Dawn brain is available";',
    '    return nullptr;',
    '}',
    'extern "C" void *cnc_ios_dlsym(void *handle, const char *name) {',
    '    if (handle != iosBrainExports) { iosBrainError = "Invalid static brain handle"; return nullptr; }',
    '    void *address = opencnc3d_ios_brain_symbol(name);',
    '    iosBrainError = address ? nullptr : "Export absent from the bundled Tiberian Dawn brain";',
    '    return address;',
    '}',
    'extern "C" int cnc_ios_dlclose(void *) { return 0; }',
    'extern "C" const char *cnc_ios_dlerror(void) {',
    '    const char *error = iosBrainError; iosBrainError = nullptr; return error;',
    '}',
]
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text("\n".join(lines) + "\n")
output.with_suffix(".exports.txt").write_text("\n".join(names) + "\n")
print("Bound %d real exports from %s" % (len(names), source))
