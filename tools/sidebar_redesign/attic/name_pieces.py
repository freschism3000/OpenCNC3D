from PIL import Image, ImageDraw, ImageFont
import os, shutil, json
SRC = "incoming_cut"; DST = "art_new"
if os.path.isdir(DST): shutil.rmtree(DST)
os.makedirs(DST)
m = json.load(open(f"{SRC}/manifest.json"))
by = {r["name"]: r for r in m}
def stem(s):
    for r in m:
        if r["name"].endswith(s) and r["src"].startswith(s.split("|")[0]): pass
    return None
# map by source file + index, using the sheet each came from
MAP = {}
for r in m:
    src, i = r["src"], int(r["name"].split("_")[-1])
    if r["w"] == 681:                       MAP[r["name"]] = "panel_body"
    elif src.endswith("(4).png"):           MAP[r["name"]] = ["arrow_up","arrow_up_pressed",
                                                              "arrow_down","arrow_down_pressed"][i]
    elif "11.58.41" in src:                 MAP[r["name"]] = ["panel_header","meter_empty",
                                                              "meter_full","panel_radar_bezel",
                                                              "cell_frame","cell_well"][i]
    elif "11.58.47" in src:                 MAP[r["name"]] = ["button_repair","button_sell","button_map",
                                                              "text_sidebar","text_repair",
                                                              "text_sell","text_map"][i]
for old, new in MAP.items():
    Image.open(f"{SRC}/{old}.png").save(f"{DST}/{new}.png")
    r = by[old]
    print("%-22s <- %-42s %5dx%-5d aspect %.3f" % (new, old, r["w"], r["h"], r["aspect"]))
json.dump({v: dict(w=by[k]["w"], h=by[k]["h"]) for k, v in MAP.items()},
          open(f"{DST}/sizes.json","w"), indent=1)
