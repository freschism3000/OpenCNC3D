"""C&C3D sidebar HUD, 640x480.

The delivered reference IS the chassis: one static background, drawn once. Only the
genuinely dynamic things composite on top, into slots measured off that reference.
This guarantees the result matches the reference exactly rather than approximately."""
from PIL import Image
import os

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 160, 480
SCREEN_POS = (480, 0)                       # sidebar origin on a 640x480 screen

# --- slots, measured off newhud_reference.png scaled to 160x480 -------------
RADAR   = (22, 33, 116, 97)                 # x, y, w, h
CELL_W, CELL_H = 46, 41                     # visible interior of one build slot
CELL_X  = [33, 89]
CELL_Y  = [191, 239, 286, 334, 380]         # pitch 47.25, rounded per row
METER_POS = (11, 191)                       # where the 18x240 meter piece sits
METER_CHANNEL = (8, 231)                    # usable fill span inside the piece, y0..y1
METER_SEG     = (96, 225, 12)               # green source rows y0..y1 and segment pitch
BUTTONS = {"repair": (7, 162), "sell": (72, 162), "map": (117, 162)}
ARROWS  = [(19, 434, "up"), (52, 434, "down"), (90, 434, "up"), (123, 434, "down")]

def _chassis():
    p = os.path.join(HERE, "incoming", "newhud_reference.png")
    return Image.open(p).convert("RGBA").resize((W, H), Image.LANCZOS)

def _piece(n):
    return Image.open(os.path.join(HERE, "incoming", f"{n}.png")).convert("RGBA")

def render(cameos=None, power=0.55, radar_img=None,
           pressed_button=None, pressed_arrow=None):
    """cameos: list of up to 10 PIL images (or None) for the build slots."""
    im = _chassis().copy()

    if radar_img is not None:
        x, y, w, h = RADAR
        im.alpha_composite(radar_img.convert("RGBA").resize((w, h), Image.NEAREST), (x, y))

    if cameos:
        i = 0
        for y in CELL_Y:
            for x in CELL_X:
                if i < len(cameos) and cameos[i] is not None:
                    c = cameos[i].convert("RGBA").resize((CELL_W, CELL_H), Image.NEAREST)
                    im.alpha_composite(c, (x, y))
                i += 1

    # power bar: empty channel as the base, then the bottom slice of the filled piece.
    # (Copying the chassis onto itself, as this first did, is a no-op.)
    mx, my = METER_POS
    c0, c1 = METER_CHANNEL
    s0, s1, pitch = METER_SEG
    im.alpha_composite(_piece("meter_empty"), (mx, my))
    lvl = max(0.0, min(1.0, power))
    span = c1 - c0 + 1
    n_seg = int(round(span / pitch * lvl))
    if n_seg > 0:
        full = _piece("meter_assembled")
        seg = full.crop((0, s1 + 1 - pitch, full.width, s1 + 1))   # one segment, bottom-most
        for k in range(n_seg):                                     # tile upward from the floor
            y = c1 + 1 - (k + 1) * pitch
            if y < c0: break
            im.alpha_composite(seg, (mx, my + y))

    if pressed_button in BUTTONS:
        im.alpha_composite(_piece(f"button_{pressed_button}"), BUTTONS[pressed_button])
    if pressed_arrow is not None and 0 <= pressed_arrow < 4:
        x, y, d = ARROWS[pressed_arrow]
        im.alpha_composite(_piece(f"arrow_{d}_pressed"), (x - 3, y - 3))
    return im

if __name__ == "__main__":
    import glob
    names = ["FACT","E1","NUKE","E2","PROC","JEEP","WEAP","MTNK","HAND","APC"]
    cams = [Image.open(os.path.join(HERE, "art", f"{n}.png")) for n in names
            if os.path.exists(os.path.join(HERE, "art", f"{n}.png"))]
    a = render(); a.save("hud_empty.png"); a.resize((W*3, H*3), Image.NEAREST).save("hud_empty@3x.png")
    b = render(cameos=cams, power=0.62)
    b.save("hud_live.png"); b.resize((W*3, H*3), Image.NEAREST).save("hud_live@3x.png")
    print("rendered hud_empty.png and hud_live.png (%d cameos)" % len(cams))
