"""C&C3D sidebar HUD for 640x480 — 8 build slots, full-size radar.

Composed from the delivered pieces rather than the flattened chassis, because the
chassis baked in a 116x97 radar opening that can only show the big missions at 1px per
cell. The standalone bezel's opening is 139x123, which covers every shipped mission
(largest is 61x59) at a crisp 2px per cell."""
from PIL import Image
import os

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 160, 480
SCREEN_POS = (480, 0)                  # sidebar origin on the 640x480 screen

HEADER_Y  = 0                          # panel_header 160x18
BEZEL_Y   = 18                         # panel_radar_bezel 160x160
RADAR     = (9, BEZEL_Y + 9, 139, 123) # x, y, w, h  -> the minimap surface
BTN_Y     = 184
BUTTONS   = {"repair": (6, BTN_Y), "sell": (74, BTN_Y), "map": (117, BTN_Y)}
METER_POS = (2, 208)
METER_CHANNEL = (8, 231)
METER_SEG     = (96, 225, 12)
CELL_X    = [24, 92]
CELL_Y    = [212, 268, 324, 380]       # 4 rows, 56 pitch, 64x48 pieces
CELL_W, CELL_H = 64, 48
ARROW_Y   = 450
ARROWS    = [(10, "up"), (46, "down"), (86, "up"), (122, "down")]

def _p(n): return Image.open(os.path.join(HERE, "incoming", f"{n}.png")).convert("RGBA")

def radar_zoom(map_w, map_h):
    """Largest integer zoom whose result still fits the radar surface."""
    return max(1, min(RADAR[2] // max(1, map_w), RADAR[3] // max(1, map_h)))

def render(cameos=None, power=0.55, radar_img=None,
           pressed_button=None, pressed_arrow=None):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    im.alpha_composite(_p("panel_body"), (0, 0))
    im.alpha_composite(_p("panel_header"), (0, HEADER_Y))

    # bezel first: its opening is opaque dark, not transparent, so the minimap has to
    # go on top of it rather than under it
    im.alpha_composite(_p("panel_radar_bezel"), (0, BEZEL_Y))
    if radar_img is not None:
        x, y, w, h = RADAR
        r = radar_img.convert("RGBA")
        if r.size != (w, h):                       # centre, never stretch
            pane = Image.new("RGBA", (w, h), (0, 0, 0, 255))
            pane.alpha_composite(r, ((w - r.width) // 2, (h - r.height) // 2))
            r = pane
        im.alpha_composite(r, (x, y))

    for n, pos in BUTTONS.items():
        im.alpha_composite(_p(f"button_{n}"), pos)

    mx, my = METER_POS
    c0, c1 = METER_CHANNEL
    s0, s1, pitch = METER_SEG
    im.alpha_composite(_p("meter_empty"), (mx, my))
    lvl = max(0.0, min(1.0, power))
    for k in range(int(round((c1 - c0 + 1) / pitch * lvl))):
        y = c1 + 1 - (k + 1) * pitch
        if y < c0: break
        full = _p("meter_assembled")
        im.alpha_composite(full.crop((0, s1 + 1 - pitch, full.width, s1 + 1)), (mx, my + y))

    well, frame = _p("cell_well"), _p("cell_frame")
    i = 0
    for y in CELL_Y:
        for x in CELL_X:
            im.alpha_composite(well, (x, y))
            if cameos and i < len(cameos) and cameos[i] is not None:
                c = cameos[i].convert("RGBA")
                if c.size != (CELL_W, CELL_H):     # centre, never stretch
                    pane = Image.new("RGBA", (CELL_W, CELL_H), (0, 0, 0, 0))
                    pane.alpha_composite(c, ((CELL_W - c.width) // 2, (CELL_H - c.height) // 2))
                    c = pane
                im.alpha_composite(c, (x, y))
                im.alpha_composite(frame, (x, y))
            i += 1

    for j, (x, d) in enumerate(ARROWS):
        n = f"arrow_{d}_pressed" if pressed_arrow == j else f"arrow_{d}"
        im.alpha_composite(_p(n), (x, ARROW_Y))
    if pressed_button in BUTTONS:
        im.alpha_composite(_p(f"button_{pressed_button}"), BUTTONS[pressed_button])
    return im

if __name__ == "__main__":
    names = ["FACT","E1","NUKE","E2","PROC","JEEP","WEAP","MTNK"]
    cams = [Image.open(os.path.join(HERE,"art",f"{n}.png")) for n in names
            if os.path.exists(os.path.join(HERE,"art",f"{n}.png"))]
    a = render(); a.save("hud2_empty.png"); a.resize((W*3,H*3),Image.NEAREST).save("hud2_empty@3x.png")
    b = render(cameos=cams, power=0.62)
    b.save("hud2_live.png"); b.resize((W*3,H*3),Image.NEAREST).save("hud2_live@3x.png")
    print("8 slots, radar surface %dx%d at (%d,%d)" % (RADAR[2],RADAR[3],RADAR[0],RADAR[1]))
