#!/usr/bin/env python3
"""校验文档规定的双屏 GIF，并打包为 v1 RGB565 差分流。"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
import zlib
from fractions import Fraction
from pathlib import Path

WIDTH = HEIGHT = 160
FPS = 25
FRAME_BYTES = WIDTH * HEIGHT * 2
ASSETS_CAPACITY = 16 * 1024 * 1024
MAGIC = b"DEYEANIM"
VERSION = 1
# 顺序、源文件名和帧数与 docs/需求补充说明.md（V4）一致。
ANIMATIONS = (
    ("calm", "01_calm", 130),
    ("happy", "02_happy", 112),
    ("surprised", "03_surprised", 56),
    ("angry", "04_angry", 130),
    ("sad", "05_sad", 130),
    ("sleepy", "06_sleepy", 143),
    ("shy", "07_shy", 130),
    ("scared", "08_scared", 135),
    ("battery", "09_battery", 75),
    ("network4g", "10_network4g", 75),
    ("network_wifi", "11_network_wifi", 75),
    ("standby", "12_standby", 75),
    ("power_on", "13_power_on", 100),
    ("wake", "14_cancel_standby", 75),
)
EMOTIONS = tuple(item[0] for item in ANIMATIONS)
TABLE = struct.Struct("<12sHHIIII")
HEADER = struct.Struct("<8sHHII")
ASSET_TABLE = struct.Struct("<32sIIHH")
FRAME_HEADER = struct.Struct("<I")


def gif_metadata(path: Path) -> list[int]:
    """读取 GIF 原始延时和循环扩展；解复用器可能会规范化延时。"""
    data = path.read_bytes()
    if data[:6] not in (b"GIF87a", b"GIF89a"):
        raise ValueError(f"{path}: invalid GIF signature")
    width, height, flags = struct.unpack_from("<HHB", data, 6)
    if (width, height) != (WIDTH, HEIGHT):
        raise ValueError(f"{path}: canvas must be 160x160")
    cursor = 13 + (3 * (2 ** ((flags & 7) + 1)) if flags & 128 else 0)
    delays, loops = [], []
    delay = None

    def blocks() -> list[bytes]:
        nonlocal cursor
        chunks = []
        while True:
            if cursor >= len(data):
                raise ValueError(f"{path}: truncated GIF sub-block")
            length = data[cursor]
            cursor += 1
            if not length:
                return chunks
            if cursor + length > len(data):
                raise ValueError(f"{path}: truncated GIF payload")
            chunks.append(data[cursor:cursor + length])
            cursor += length

    while cursor < len(data):
        marker = data[cursor]
        cursor += 1
        if marker == 0x3B:
            if not delays or loops != [0] or any(value != 4 for value in delays):
                raise ValueError(f"{path}: require every delay=40ms and one infinite loop extension")
            return delays
        if marker == 0x21:
            label = data[cursor]
            cursor += 1
            chunks = blocks()
            if label == 0xF9:
                if len(chunks) != 1 or len(chunks[0]) != 4:
                    raise ValueError(f"{path}: invalid graphics control extension")
                delay = struct.unpack_from("<H", chunks[0], 1)[0]
            elif label == 0xFF and chunks and chunks[0] in (b"NETSCAPE2.0", b"ANIMEXTS1.0"):
                if len(chunks) != 2 or len(chunks[1]) != 3 or chunks[1][0] != 1:
                    raise ValueError(f"{path}: invalid loop extension")
                loops.append(struct.unpack_from("<H", chunks[1], 1)[0])
        elif marker == 0x2C:
            x, y, w, h, flags = struct.unpack_from("<HHHHB", data, cursor)
            if (x, y, w, h) != (0, 0, WIDTH, HEIGHT):
                raise ValueError(f"{path}: every stored frame must be full 160x160")
            cursor += 9 + (3 * (2 ** ((flags & 7) + 1)) if flags & 128 else 0)
            cursor += 1  # LZW 最小代码大小
            blocks()
            delays.append(delay)
            delay = None
        else:
            raise ValueError(f"{path}: unexpected GIF block {marker:#x}")
    raise ValueError(f"{path}: missing GIF trailer")


def probe(path: Path, expected_frames: int) -> tuple:
    delays = gif_metadata(path)
    metadata = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-ignore_loop", "1", "-min_delay", "0",
        "-select_streams", "v:0", "-show_frames", "-show_streams", "-of", "json", str(path),
    ]))
    frames = metadata["frames"]
    stream, = metadata["streams"]
    if len(frames) != expected_frames or len(delays) != expected_frames:
        raise ValueError(f"{path}: expected {expected_frames} frames, got {len(frames)} decoded/{len(delays)} stored")
    time_base = Fraction(stream["time_base"])
    timing = []
    for index, frame in enumerate(frames):
        pts = Fraction(frame["pts"]) * time_base
        duration = Fraction(frame.get("duration", frame.get("pkt_duration", -1))) * time_base
        if (frame["width"], frame["height"]) != (WIDTH, HEIGHT):
            raise ValueError(f"{path}: frame {index} must be 160x160")
        if pts != Fraction(index, FPS) or duration != Fraction(1, FPS):
            raise ValueError(f"{path}: frame {index} must have contiguous 40ms timing")
        timing.append((pts, duration))
    if Fraction(stream["duration"]) != Fraction(expected_frames, FPS):
        raise ValueError(f"{path}: total duration differs from documentation")
    return tuple(timing)


def decode(path: Path, expected_frames: int) -> list[bytes]:
    raw = subprocess.check_output([
        "ffmpeg", "-v", "error", "-ignore_loop", "1", "-min_delay", "0", "-i", str(path),
        "-map", "0:v:0", "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "rgb24", "-",
    ])
    rgb_size = WIDTH * HEIGHT * 3
    if len(raw) != rgb_size * expected_frames:
        raise ValueError(f"{path}: unexpected decoded size {len(raw)}")
    frames = []
    for off in range(0, len(raw), rgb_size):
        rgb = raw[off:off + rgb_size]
        out = bytearray(FRAME_BYTES)
        for i in range(WIDTH * HEIGHT):
            r, g, b = rgb[i * 3:i * 3 + 3]
            struct.pack_into("<H", out, i * 2, ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        frames.append(bytes(out))
    return frames


def pack_stream(frames: list[bytes]) -> bytes:
    if not frames or any(len(frame) != FRAME_BYTES for frame in frames):
        raise ValueError("stream requires nonempty 160x160 RGB565 frames")
    previous = bytes(FRAME_BYTES)
    out = bytearray()
    for frame in frames:
        compressed = zlib.compress(bytes(a ^ b for a, b in zip(frame, previous)), 9)
        out += FRAME_HEADER.pack(len(compressed)) + compressed
        previous = frame
    return bytes(out)


def unpack_stream(stream: bytes, count: int) -> list[bytes]:
    previous = bytes(FRAME_BYTES)
    frames = []
    cursor = 0
    for _ in range(count):
        if cursor + 4 > len(stream):
            raise ValueError("truncated frame header")
        length, = FRAME_HEADER.unpack_from(stream, cursor)
        cursor += 4
        if length == 0 or cursor + length > len(stream):
            raise ValueError("truncated compressed frame")
        inflater = zlib.decompressobj()
        delta = inflater.decompress(stream[cursor:cursor + length], FRAME_BYTES + 1)
        if len(delta) != FRAME_BYTES or not inflater.eof or inflater.unused_data or inflater.unconsumed_tail:
            raise ValueError("invalid delta frame size or zlib boundary")
        previous = bytes(a ^ b for a, b in zip(delta, previous))
        frames.append(previous)
        cursor += length
    if cursor != len(stream):
        raise ValueError("trailing stream bytes")
    return frames


def unpack_animation(payload: bytes) -> dict[str, tuple[list[bytes], list[bytes]]]:
    magic, version, count, table_offset, data_offset = HEADER.unpack_from(payload)
    if (magic, version, table_offset, data_offset) != (MAGIC, VERSION, HEADER.size, HEADER.size + count * TABLE.size):
        raise ValueError("invalid animation header")
    result = {}
    for i in range(count):
        name, frames, fps, lo, ll, ro, rl = TABLE.unpack_from(payload, table_offset + i * TABLE.size)
        name = name.rstrip(b"\0").decode("ascii")
        if not name or name in result or fps != FPS or not frames:
            raise ValueError("invalid animation record")
        eyes = []
        for offset, length in ((lo, ll), (ro, rl)):
            if offset < data_offset or offset + length > len(payload):
                raise ValueError("stream outside animation payload")
            eyes.append(unpack_stream(payload[offset:offset + length], frames))
        result[name] = tuple(eyes)
    return result


def unpack_assets(payload: bytes) -> dict[str, bytes]:
    count, checksum, size = struct.unpack_from("<III", payload)
    if size != len(payload) - 12 or sum(payload[12:]) & 0xFFFF != checksum:
        raise ValueError("invalid assets size/checksum")
    start = 12 + count * ASSET_TABLE.size
    if start > len(payload):
        raise ValueError("invalid assets table")
    files = {}
    for i in range(count):
        name, length, offset, _, _ = ASSET_TABLE.unpack_from(payload, 12 + i * ASSET_TABLE.size)
        name = name.rstrip(b"\0").decode("utf-8")
        pos = start + offset
        if name in files or payload[pos:pos + 2] != b"ZZ" or pos + 2 + length > len(payload):
            raise ValueError("invalid assets entry")
        files[name] = payload[pos + 2:pos + 2 + length]
    return files


def package_assets(animation: bytes, srmodels: bytes | None = None, *, base_files: dict[str, bytes] | None = None) -> bytes:
    files = dict(base_files) if base_files is not None else {"index.json": b'{"version":1}'}
    files["dual_eye.anim"] = animation
    if srmodels is not None:
        files["srmodels.bin"] = srmodels
        index = json.loads(files["index.json"])
        index["srmodels"] = "srmodels.bin"
        files["index.json"] = json.dumps(index, separators=(",", ":")).encode("utf-8")
    entries = []
    merged = bytearray()
    for name in sorted(files, key=lambda value: (Path(value).suffix, value)):
        entries.append((name, len(merged), len(files[name])))
        merged += b"ZZ" + files[name]
    table = bytearray()
    for name, offset, length in entries:
        encoded = name.encode("utf-8")
        if len(encoded) > 32:
            raise ValueError(f"asset name too long: {name}")
        table += ASSET_TABLE.pack(encoded.ljust(32, b"\0"), length, offset, 0, 0)
    combined = table + merged
    result = struct.pack("<III", len(entries), sum(combined) & 0xFFFF, len(combined)) + combined
    if len(result) > ASSETS_CAPACITY:
        raise ValueError(f"assets package {len(result)} exceeds 16MiB partition")
    if unpack_assets(result) != files:
        raise ValueError("assets container roundtrip failed")
    return result


def build_animation(input_dir: Path, baseline: bytes | None = None) -> bytes:
    old = unpack_animation(baseline) if baseline is not None else None
    streams = {}
    records = []
    sources = {}
    for index, (name, stem, count) in enumerate(ANIMATIONS):
        encoded = name.encode("ascii")
        if len(encoded) > 12:
            raise ValueError(f"v1 name exceeds 12 bytes: {name}")
        left_path, right_path = (input_dir / f"{stem}_{eye}.gif" for eye in ("L", "R"))
        if probe(left_path, count) != probe(right_path, count):
            raise ValueError(f"{name}: left/right timing differs")
        eyes = (decode(left_path, count), decode(right_path, count))
        if old is not None and index < 8 and old.get(name) != eyes:
            raise ValueError(f"{name}: source frames differ from original eight animations")
        keys = []
        for frames in eyes:
            stream = pack_stream(frames)
            key = hashlib.sha256(stream).digest()
            streams.setdefault(key, stream)
            keys.append(key)
        records.append((encoded, count, *keys))
        sources[name] = eyes
        print(f"validated {name}: {count} frames/eye, {count / FPS:.2f}s, 160x160, 40ms, infinite loop")
    header_size = HEADER.size + TABLE.size * len(records)
    offsets = {}
    cursor = header_size
    for key, stream in streams.items():
        offsets[key] = cursor
        cursor += len(stream)
    payload = bytearray(cursor)
    HEADER.pack_into(payload, 0, MAGIC, VERSION, len(records), HEADER.size, header_size)
    for index, (name, count, lk, rk) in enumerate(records):
        TABLE.pack_into(payload, HEADER.size + index * TABLE.size, name.ljust(12, b"\0"), count, FPS,
                        offsets[lk], len(streams[lk]), offsets[rk], len(streams[rk]))
    for key, stream in streams.items():
        payload[offsets[key]:offsets[key] + len(stream)] = stream
    if unpack_animation(payload) != sources:
        raise ValueError("animation roundtrip differs from decoded RGB565 sources")
    print(f"roundtrip verified all {sum(item[2] for item in ANIMATIONS) * 2} frames, {len(streams)} unique streams")
    if old is not None:
        print("original eight animations: every left/right RGB565 frame unchanged")
    return bytes(payload)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=Path("docs/LCD资源动画/gif"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--package-output", type=Path)
    parser.add_argument("--base-package", type=Path, help="preserve files/index and compare original eight; defaults to existing package-output")
    parser.add_argument("--srmodels", type=Path, help="explicitly replace/add srmodels.bin")
    args = parser.parse_args()
    base_path = args.base_package or (args.package_output if args.package_output and args.package_output.exists() else None)
    base_files = unpack_assets(base_path.read_bytes()) if base_path else None
    animation = build_animation(args.input, base_files["dual_eye.anim"] if base_files is not None else None)
    package = None
    if args.package_output:
        package = package_assets(animation, args.srmodels.read_bytes() if args.srmodels else None, base_files=base_files)
        restored = unpack_assets(package)
        if base_files is not None:
            for name in base_files.keys() - {"dual_eye.anim"}:
                if args.srmodels and name in ("index.json", "srmodels.bin"):
                    continue
                if restored[name] != base_files[name]:
                    raise ValueError(f"preserved asset differs: {name}")
                print(f"preserved {name}: {len(restored[name])} bytes sha256={hashlib.sha256(restored[name]).hexdigest()}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(animation)
    if package is not None:
        args.package_output.parent.mkdir(parents=True, exist_ok=True)
        args.package_output.write_bytes(package)
        print(f"wrote {args.package_output} ({len(package)} bytes; {ASSETS_CAPACITY - len(package)} bytes free in 16MiB)")
    print(f"wrote {args.output} ({len(animation)} bytes)")


if __name__ == "__main__":
    main()
