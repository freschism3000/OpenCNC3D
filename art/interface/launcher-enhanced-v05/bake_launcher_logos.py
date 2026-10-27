#!/usr/bin/env python3
"""Encode the generated transparent emblems as launcher RGBA upload arrays.

Defaults are relative to this archive. Neither source is cropped, recoloured,
retouched, or assigned inferred transparency. Each display image receives one
LANCZOS downfilter of its complete source, with rounded output dimensions.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

import PIL
from PIL import Image


ARCHIVE = Path(__file__).resolve().parent
EXPECTED = {
    "module": "cbb3be33b4f78515592b7531c83b75ad75c7cd536c06b5c964b61a612057cc6e",
    "ra": "72ec9415fdb576d1aaac79093850e8628567e3256c256aa63c91d78816b94fd8",
    "wordmark": "33b4001278f281e7b6c6a351f5931d8b16a755bc6a28d5be86d93ac8de46c704",
}
WORDMARK_RGBA_V02 = "f962f5d8e63c70cbb16382dabba1b50e0976fbd9d1451ff01522258cd1e5e7d0"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def alpha_stats(image):
    alpha = image.getchannel("A")
    histogram = alpha.histogram()
    corners = [alpha.getpixel(point) for point in
               [(0, 0), (image.width - 1, 0), (0, image.height - 1),
                (image.width - 1, image.height - 1)]]
    count = image.width * image.height
    return {"extrema": list(alpha.getextrema()), "corner_values": corners,
            "corners_zero": not any(corners), "transparent_pixels": histogram[0],
            "transparent_fraction": histogram[0] / count,
            "opaque_pixels": histogram[255],
            "partial_alpha_pixels": count - histogram[0] - histogram[255],
            "full_canvas_rectangular_matte": bool(any(corners) or histogram[0] == 0)}


def array(name, width_macro, height_macro, rgba):
    rows = ["static const unsigned char %s[%s * %s * 4] = {" %
            (name, width_macro, height_macro)]
    rows.extend("    " + ",".join("0x%02x" % value for value in rgba[start:start + 16]) + ","
                for start in range(0, len(rgba), 16))
    rows.append("};")
    return "\n".join(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, default=ARCHIVE / "opencnc3d-matching-ring.png")
    parser.add_argument("--ra", type=Path, default=ARCHIVE / "red-alert-reference.png")
    parser.add_argument("--wordmark", type=Path, default=ARCHIVE / "wordmark-original.png")
    parser.add_argument("--out", type=Path, default=ARCHIVE)
    parser.add_argument("--header-copy", type=Path,
                        default=ARCHIVE.parents[2] / "tmp/launcher-ring/source/launcher_logos.h")
    args = parser.parse_args()
    output = args.out.resolve()
    (output / "source").mkdir(parents=True, exist_ok=True)
    records, pixels = {}, {}
    for role, source, source_size, source_mode, target in [
        ("module", args.module, (1254, 1254), "RGBA", (160, 160)),
        ("ra", args.ra, (1254, 1254), "RGBA", (160, 160)),
        ("wordmark", args.wordmark, (1922, 818), "RGBA", (220, 94)),
    ]:
        source = source.resolve()
        raw = source.read_bytes()
        if sha(raw) != EXPECTED[role]:
            raise ValueError("The exact archived %s source hash does not match" % role)
        original = Image.open(source)
        if original.size != source_size or original.mode != source_mode:
            raise ValueError("Unexpected %s source dimensions or mode" % role)
        # Uniform source fitting, with integer-rounded raster dimensions.
        scale = min(target[0] / original.width, target[1] / original.height)
        rounded = tuple(max(1, round(size * scale)) for size in original.size)
        if rounded != target:
            raise ValueError("Requested dimensions do not match uniform full-source fitting")
        display = original.convert("RGBA").resize(target, Image.Resampling.LANCZOS)
        original_alpha, display_alpha = alpha_stats(original), alpha_stats(display)
        if role in ("module", "ra") and not all(
                item["corners_zero"] and item["transparent_fraction"] > 0.05
                and item["extrema"] == [0, 255] for item in (original_alpha, display_alpha)):
            raise ValueError("%s must retain genuine transparent area and zero-alpha corners" % role)
        rgba = display.tobytes()
        if role == "wordmark" and sha(rgba) != WORDMARK_RGBA_V02:
            raise ValueError("Header wordmark must retain the exact v02 display pixels")
        pixels[role] = rgba
        display_name = "%s-display-%dx%d.png" % (role, *target)
        display_path = output / display_name
        display.save(display_path, format="PNG", optimize=False, compress_level=9)
        records[role] = {
            "source": source.name,
            "source_sha256": sha(raw),
            "source_mode": original.mode,
            "source_size": list(original.size),
            "source_decoded_rgba_sha256": sha(original.convert("RGBA").tobytes()),
            "display": display_name,
            "display_size": list(display.size),
            "display_mode": "RGBA",
            "display_png_sha256": sha(display_path.read_bytes()),
            "display_rgba_sha256": sha(rgba),
            "display_rgba_bytes": len(rgba),
            "alpha_extrema": list(display.getchannel("A").getextrema()),
            "source_alpha_statistics": original_alpha,
            "display_alpha_statistics": display_alpha,
            "source_rectangle": [0, 0, original.width, original.height],
            "uniform_fit_scale": scale,
            "filter": "Pillow LANCZOS, one full-source downfilter",
            "source_alpha": "archived alpha preserved by RGBA decode and full-source LANCZOS downfilter; no threshold, crop or inference",
            "role": {"module": "approved generated OpenCNC3D emblem with a shiny ring matching Red Alert",
                     "ra": "generated polished Red Alert battle medallion, derived from the original RA module reference",
                     "wordmark": "unchanged actual OpenCNC3D header wordmark"}[role],
        }
    header = "\n".join([
        "/* Generated by bake_launcher_logos.py; exact source roles/hashes are in manifest.json.",
        " * Straight RGBA display uploads; full source compositions retained. */",
        "#ifndef CNC_LAUNCHER_LOGOS_H",
        "#define CNC_LAUNCHER_LOGOS_H",
        "#define L_MODULE_LOGO_W 160",
        "#define L_MODULE_LOGO_H 160",
        "#define L_RA_LOGO_W 160",
        "#define L_RA_LOGO_H 160",
        "#define L_WORDMARK_W 220",
        "#define L_WORDMARK_H 94",
        "",
        array("launcher_module_rgba", "L_MODULE_LOGO_W", "L_MODULE_LOGO_H", pixels["module"]),
        "",
        array("launcher_ra_rgba", "L_RA_LOGO_W", "L_RA_LOGO_H", pixels["ra"]),
        "",
        array("launcher_wordmark_rgba", "L_WORDMARK_W", "L_WORDMARK_H", pixels["wordmark"]),
        "",
        "#endif",
        "",
    ])
    header_path = output / "source/launcher_logos.h"
    header_path.write_text(header, encoding="ascii")
    manifest = {
        "version": 5,
        "purpose": "approved OpenCNC3D shiny matching ring, with unchanged Red Alert emblem and header wordmark",
        "operations": "decode exact archived generated RGBA emblems and original wordmark; uniformly fit complete images with rounded dimensions; one LANCZOS downfilter; encode PNG witnesses and C arrays",
        "previous_version": "v03 introduced transparent emblems; this approved revision matches the CNC rim to the shiny RA ring",
        "alpha_threshold": False,
        "crop": False,
        "recolour": False,
        "transparency_inference": False,
        "image_generation": False,
        "pillow_version": PIL.__version__,
        "assets": records,
        "wordmark_display_pixels_identical_to_v02": sha(pixels["wordmark"]) == WORDMARK_RGBA_V02,
        "supporting_references": {
            name: {"sha256": sha((ARCHIVE / name).read_bytes()), "role": role}
            for name, role in [
                ("opencnc3d-before.png", "previous exact transparent OpenCNC3D emblem; edit target"),
                ("red-alert-reference.png", "exact approved Red Alert medallion; shiny ring reference"),
            ]
        },
        "header": "source/launcher_logos.h",
        "header_sha256": sha(header_path.read_bytes()),
        "total_rgba_bytes": sum(len(value) for value in pixels.values()),
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    copy = args.header_copy.resolve()
    if copy != header_path:
        copy.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(header_path, copy)
    print(json.dumps({"archive": str(output), "header": str(header_path),
                      "header_copy": str(copy), "total_rgba_bytes": manifest["total_rgba_bytes"],
                      "header_sha256": manifest["header_sha256"]}, sort_keys=True))


if __name__ == "__main__":
    main()
