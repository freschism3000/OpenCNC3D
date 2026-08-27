"""Waveform + spectrogram PNGs from a WAV, with numpy and PIL only.

    python3 tools/plot.py out/nuyell1.wav out/nuyell1.png [--title "..."]

Draws, top to bottom:
  - the waveform (both channels overlaid if stereo, L blue / R orange)
  - a log-magnitude spectrogram, 0 Hz at the bottom

The point is to LOOK at it. A flat line is a dead decoder. A spectrogram that is a
uniform wash across all frequencies is noise, which is what a wrong ADPCM table
produces; real speech and real music have visible structure.
"""
import sys, struct, wave
import numpy as np
from PIL import Image, ImageDraw


def read_wav(path):
    w = wave.open(path, "rb")
    ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    assert sw == 2, "expected 16 bit"
    raw = w.readframes(n)
    w.close()
    a = np.frombuffer(raw, dtype="<i2").astype(np.float32)
    if ch == 2:
        a = a.reshape(-1, 2)
    else:
        a = a.reshape(-1, 1)
    return a, rate, ch


def spectrogram(mono, rate, width, height, nfft=1024, log_freq=False):
    hop = max(1, len(mono) // width)
    cols = []
    win = np.hanning(nfft).astype(np.float32)
    for i in range(width):
        s = i * hop
        seg = mono[s:s + nfft]
        if len(seg) < nfft:
            seg = np.pad(seg, (0, nfft - len(seg)))
        sp = np.abs(np.fft.rfft(seg * win))
        cols.append(sp[: nfft // 2])
    m = np.array(cols).T                      # freq x time
    # ABSOLUTE dBFS, not per-file auto gain. Auto gain makes a quiet noise floor
    # look like content and would have hidden a dead decoder behind a pretty picture.
    ref = 32768.0 * (nfft / 4.0)              # full scale sine through this window
    m = 20.0 * np.log10(m / ref + 1e-9)
    lo, hi = -90.0, -10.0
    m = np.clip((m - lo) / (hi - lo), 0, 1)
    # resize freq axis to `height` by simple binning
    fbins = m.shape[0]
    idx = (np.arange(height) * (fbins - 1) / max(1, height - 1)).astype(int)
    m = m[idx]
    return m[::-1]                            # 0 Hz at the bottom


def colormap(v):
    """dark -> blue -> green -> yellow -> white, so structure is obvious."""
    v = np.clip(v, 0, 1)
    r = np.clip(1.6 * v - 0.5, 0, 1)
    g = np.clip(1.8 * v - 0.2, 0, 1)
    b = np.clip(2.2 * v, 0, 1) * (1 - np.clip(1.5 * v - 0.6, 0, 1))
    return (np.dstack([r, g, b]) * 255).astype(np.uint8)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    title = ""
    if "--title" in sys.argv:
        title = sys.argv[sys.argv.index("--title") + 1]

    a, rate, ch = read_wav(src)
    W, WAVE_H, SPEC_H, PAD = 1100, 220, 300, 34
    H = PAD + WAVE_H + 8 + SPEC_H + 22

    img = Image.new("RGB", (W, H), (16, 16, 20))
    d = ImageDraw.Draw(img)

    n = a.shape[0]
    mono = a.mean(axis=1)
    peak = float(np.abs(a).max()) if n else 0.0
    rms = float(np.sqrt((mono.astype(np.float64) ** 2).mean())) if n else 0.0
    dur = n / float(rate)

    hdr = "%s   %d Hz  %dch  %.3f s  peak %d  rms %.1f" % (
        title or src.split("/")[-1], rate, ch, dur, int(peak), rms)
    d.text((10, 10), hdr, fill=(230, 230, 235))

    # waveform: min/max envelope per pixel column.
    # Stereo goes in two stacked lanes, never overlaid: an overlay just paints the
    # second channel on top of the first and hides exactly the thing you are looking
    # for, which is the two channels differing.
    y0 = PAD
    nlanes = a.shape[1]
    lane_h = WAVE_H // nlanes
    cols = np.array_split(np.arange(n), W) if n else []
    colours = [(90, 170, 255), (255, 165, 70)]
    labels = ["L", "R"] if nlanes == 2 else [""]
    for c in range(nlanes):
        mid = y0 + c * lane_h + lane_h // 2
        d.line([(0, mid), (W, mid)], fill=(60, 60, 70))
        for i, sl in enumerate(cols):
            if len(sl) == 0:
                continue
            seg = a[sl[0]:sl[-1] + 1, c]
            lo = int(mid - seg.max() / 32768.0 * (lane_h // 2 - 2))
            hi = int(mid - seg.min() / 32768.0 * (lane_h // 2 - 2))
            d.line([(i, lo), (i, hi)], fill=colours[c % 2])
        if labels[c]:
            d.text((4, y0 + c * lane_h + 2), labels[c], fill=(200, 200, 210))

    # spectrogram
    y1 = y0 + WAVE_H + 8
    m = spectrogram(mono, rate, W, SPEC_H)
    img.paste(Image.fromarray(colormap(m)), (0, y1))

    d.text((6, y1 - 14), "waveform (full scale +/-32768)", fill=(150, 150, 160))
    d.text((6, y1 + SPEC_H + 4), "spectrogram  0 Hz at bottom, %d Hz at top" % (rate // 2),
           fill=(150, 150, 160))

    img.save(dst)
    print("%s -> %s   %d Hz %dch %.3fs peak %d rms %.1f" % (src, dst, rate, ch, dur, peak, rms))


if __name__ == "__main__":
    main()
