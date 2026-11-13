#!/usr/bin/env python3
"""Normalise Asuka's dumped PPM frames onto one fixed canvas.

The FM Towns changes video mode as it boots (640x480 POST, 640x400 boot menu,
320x240 game graphics), so the raw dump has frames of differing sizes. The
ffmpeg image2 demuxer needs a constant frame size, and a viewer should not see
the picture jump around, so every frame is letterboxed into a single canvas at
the largest integer scale that fits, with NEAREST sampling (keeps the pixel art
crisp). 320x240 game mode thus lands on exactly 2x and fills the canvas.

Usage:
    normalise_frames.py <src_dir> <dst_dir> [canvas_w] [canvas_h]

Frames are read as frame_%04d.ppm and written the same way.
"""

import os
import sys

import numpy as np

CANVAS_W, CANVAS_H = 640, 480


def read_ppm(path):
    with open(path, "rb") as fh:
        assert fh.readline().strip() == b"P6", f"{path}: not a binary PPM"
        w, h = map(int, fh.readline().split())
        fh.readline()
        return np.frombuffer(fh.read(), dtype=np.uint8).reshape(h, w, 3)


def write_ppm(path, img):
    h, w, _ = img.shape
    with open(path, "wb") as fh:
        fh.write(b"P6\n%d %d\n255\n" % (w, h))
        fh.write(np.ascontiguousarray(img).tobytes())


def nearest_scale(img, factor):
    if factor == 1:
        return img
    return np.repeat(np.repeat(img, factor, axis=0), factor, axis=1)


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2

    src, dst = sys.argv[1], sys.argv[2]
    canvas_w = int(sys.argv[3]) if len(sys.argv) > 3 else CANVAS_W
    canvas_h = int(sys.argv[4]) if len(sys.argv) > 4 else CANVAS_H

    os.makedirs(dst, exist_ok=True)
    names = sorted(n for n in os.listdir(src) if n.endswith(".ppm"))

    for name in names:
        img = read_ppm(os.path.join(src, name))
        h, w, _ = img.shape
        factor = max(1, min(canvas_w // w, canvas_h // h))
        img = nearest_scale(img, factor)
        h, w, _ = img.shape
        canvas = np.zeros((canvas_h, canvas_w, 3), dtype=np.uint8)
        x0 = (canvas_w - w) // 2
        y0 = (canvas_h - h) // 2
        canvas[y0:y0 + h, x0:x0 + w] = img
        write_ppm(os.path.join(dst, name), canvas)

    print(f"normalised {len(names)} frames -> {canvas_w}x{canvas_h} in {dst}")


if __name__ == "__main__":
    sys.exit(main())
