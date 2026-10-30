#!/usr/bin/env python3

from __future__ import annotations

import struct
import wave
import zlib
from pathlib import Path


OUTPUT_DIR = Path(__file__).resolve().parents[1] / "resources"
BACKGROUND = (201, 190, 156, 255)
BROWN = (83, 54, 31, 255)
GREEN = (48, 75, 43, 255)
CREAM = (242, 234, 212, 255)
OLIVE = (145, 125, 76, 255)

FONT = {
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "G": ("01111", "10000", "10000", "10111", "10001", "10001", "01111"),
    "I": ("11111", "00100", "00100", "00100", "00100", "00100", "11111"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
}


def canvas(width: int, height: int, color: tuple[int, int, int, int]):
    return [[color for _ in range(width)] for _ in range(height)]


def rectangle(image, x: int, y: int, width: int, height: int, color) -> None:
    for py in range(max(0, y), min(len(image), y + height)):
        for px in range(max(0, x), min(len(image[0]), x + width)):
            image[py][px] = color


def draw_text(image, text: str, x: int, y: int, scale: int, color) -> None:
    cursor = x
    for char in text:
        pattern = FONT[char]
        for row, bits in enumerate(pattern):
            for column, bit in enumerate(bits):
                if bit == "1":
                    rectangle(
                        image,
                        cursor + column * scale,
                        y + row * scale,
                        scale,
                        scale,
                        color,
                    )
        cursor += 6 * scale


def write_png(path: Path, image) -> None:
    height = len(image)
    width = len(image[0])
    raw = bytearray()
    for row in image:
        raw.append(0)
        for pixel in row:
            raw.extend(pixel)

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + kind
            + data
            + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
        )

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def make_icon() -> None:
    image = canvas(48, 48, BACKGROUND)
    rectangle(image, 2, 2, 44, 44, BROWN)
    rectangle(image, 5, 5, 38, 38, CREAM)
    rectangle(image, 5, 5, 38, 8, GREEN)
    rectangle(image, 10, 16, 28, 4, OLIVE)
    rectangle(image, 13, 22, 22, 3, BROWN)
    rectangle(image, 16, 28, 16, 3, OLIVE)
    draw_text(image, "AE", 12, 34, 2, GREEN)
    write_png(OUTPUT_DIR / "icon.png", image)


def make_banner() -> None:
    image = canvas(256, 128, BACKGROUND)
    rectangle(image, 0, 0, 256, 23, GREEN)
    rectangle(image, 12, 34, 232, 82, CREAM)
    rectangle(image, 20, 43, 216, 4, BROWN)
    rectangle(image, 20, 54, 216, 3, OLIVE)
    rectangle(image, 20, 66, 216, 3, BROWN)
    rectangle(image, 20, 78, 216, 3, OLIVE)
    draw_text(image, "AGITATE", 20, 92, 3, GREEN)
    write_png(OUTPUT_DIR / "banner.png", image)


def make_silence() -> None:
    with wave.open(str(OUTPUT_DIR / "silence.wav"), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(32768)
        output.writeframes(b"\0\0" * 32768)


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    make_icon()
    make_banner()
    make_silence()


if __name__ == "__main__":
    main()
