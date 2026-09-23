#!/usr/bin/env python3
"""生成应用图标，不依赖任何第三方库。

图标是一把抽象的口琴：深色圆角方块上五个竖条，中间那条换成琥珀色做点缀。
竖条高度不一，读起来像波形，16px 下也认得出。

用 BMP 格式写进 ICO（不是 PNG 压缩的 ICO）——
资源编译器对后者的支持参差不齐，BMP 一定有得吃。

用法：
    python tools/make_icons.py
产出：
    app/icons/icon.ico          16 / 32 / 48 / 256 四个尺寸
    app/icons/icon-preview.png  256px，用来预览和贴文档
"""

from __future__ import annotations

import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_DIR = os.path.join(ROOT, "app", "icons")

BACKDROP = (31, 31, 36)  # #1F1F24
BAR = (93, 202, 165)     # #5DCAA5
BAR_ACCENT = (239, 159, 39)  # #EF9F27

# (左边距, 半高)，竖条水平居中在 0.5 上
BARS = [
    (0.20, 0.22, False),
    (0.325, 0.17, False),
    (0.45, 0.25, True),
    (0.575, 0.17, False),
    (0.70, 0.22, False),
]
BAR_WIDTH = 0.10
BACKDROP_RADIUS = 0.22
BACKDROP_INSET = 0.02

SUBSAMPLES = 4


def rounded_rect(u: float, v: float, x0: float, y0: float, x1: float, y1: float, r: float) -> bool:
    """点 (u, v) 是否落在圆角矩形里。"""
    if not (x0 <= u <= x1 and y0 <= v <= y1):
        return False
    cx = min(max(u, x0 + r), x1 - r)
    cy = min(max(v, y0 + r), y1 - r)
    if cx == u and cy == v:
        return True
    return (u - cx) ** 2 + (v - cy) ** 2 <= r * r


def color_at(u: float, v: float) -> tuple[int, int, int, int]:
    inset = BACKDROP_INSET
    if rounded_rect(u, v, inset, inset, 1 - inset, 1 - inset, BACKDROP_RADIUS):
        for x0, half, accent in BARS:
            r = BAR_WIDTH / 2
            if rounded_rect(u, v, x0, 0.5 - half, x0 + BAR_WIDTH, 0.5 + half, r):
                c = BAR_ACCENT if accent else BAR
                return (c[0], c[1], c[2], 255)
        return (BACKDROP[0], BACKDROP[1], BACKDROP[2], 255)
    return (0, 0, 0, 0)


def render(size: int) -> list[list[tuple[int, int, int, int]]]:
    """超采样渲染：每个像素取 SUBSAMPLES² 个样本求平均，得到抗锯齿边缘。"""
    rows = []
    n = SUBSAMPLES
    for py in range(size):
        row = []
        for px in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(n):
                for sx in range(n):
                    u = (px + (sx + 0.5) / n) / size
                    v = (py + (sy + 0.5) / n) / size
                    r, g, b, a = color_at(u, v)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            total_a = acc[3]
            if total_a == 0:
                row.append((0, 0, 0, 0))
            else:
                row.append(
                    (
                        round(acc[0] / total_a),
                        round(acc[1] / total_a),
                        round(acc[2] / total_a),
                        round(total_a / (n * n)),
                    )
                )
        rows.append(row)
    return rows


def bmp_payload(rows, size: int) -> bytes:
    """ICO 里的 BMP：DIB 头 + 自下而上的 BGRA + AND 掩码。"""
    header = struct.pack(
        "<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, size * size * 4, 0, 0, 0, 0
    )
    pixels = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            r, g, b, a = rows[y][x]
            pixels += bytes((b, g, r, a))
    mask_stride = ((size + 31) // 32) * 4
    return header + bytes(pixels) + bytes(mask_stride * size)


def png_bytes(rows, size: int) -> bytes:
    raw = bytearray()
    for y in range(size):
        raw.append(0)
        for x in range(size):
            r, g, b, a = rows[y][x]
            raw += bytes((r, g, b, a))

    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


def main() -> None:
    os.makedirs(ICON_DIR, exist_ok=True)
    sizes = [16, 32, 48, 256]
    images = {s: render(s) for s in sizes}

    entries, blobs = [], []
    offset = 6 + 16 * len(sizes)
    for size in sizes:
        payload = bmp_payload(images[size], size)
        entries.append(
            struct.pack("<BBBBHHII", size if size < 256 else 0, size if size < 256 else 0,
                        0, 0, 1, 32, len(payload), offset)
        )
        blobs.append(payload)
        offset += len(payload)

    ico = struct.pack("<HHH", 0, 1, len(sizes)) + b"".join(entries) + b"".join(blobs)
    ico_path = os.path.join(ICON_DIR, "icon.ico")
    with open(ico_path, "wb") as f:
        f.write(ico)

    png_path = os.path.join(ICON_DIR, "icon-preview.png")
    with open(png_path, "wb") as f:
        f.write(png_bytes(images[256], 256))

    print(f"{ico_path}  {len(ico):,} 字节  ({', '.join(f'{s}px' for s in sizes)})")
    print(f"{png_path}  {os.path.getsize(png_path):,} 字节")


if __name__ == "__main__":
    main()
