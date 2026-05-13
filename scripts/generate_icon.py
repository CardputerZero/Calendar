#!/usr/bin/env python3
import struct
import zlib
from pathlib import Path


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def png_rgba(width, height, pixels):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixels(x, y))
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


def main():
    size = 100

    def pixel(x, y):
        margin = 10
        radius = 15
        inside = margin <= x < size - margin and margin <= y < size - margin
        corner = False
        for cx, cy in (
            (margin + radius, margin + radius),
            (size - margin - radius - 1, margin + radius),
            (margin + radius, size - margin - radius - 1),
            (size - margin - radius - 1, size - margin - radius - 1),
        ):
            if abs(x - cx) > radius and abs(y - cy) > radius:
                corner = True
        if not inside or corner:
            return (0, 0, 0, 0)

        header = y < 30
        if header:
            color = (42, 128, 237, 255)
        else:
            color = (246, 250, 252, 255)

        if 18 <= y <= 21 and (27 <= x <= 32 or 67 <= x <= 72):
            color = (255, 224, 138, 255)

        if 39 <= y <= 83 and 19 <= x <= 81:
            if (x - 19) % 12 == 0 or (y - 39) % 11 == 0:
                color = (198, 211, 219, 255)

        if 50 <= x <= 60 and 61 <= y <= 71:
            color = (42, 128, 237, 255)
        if 52 <= x <= 58 and 63 <= y <= 69:
            color = (255, 255, 255, 255)
        return color

    out = Path(__file__).resolve().parents[1] / "share/images/calendar.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(png_rgba(size, size, pixel))


if __name__ == "__main__":
    main()
