#!/usr/bin/env python3
"""IMA4-ADPCM audio cache (.nsa v1) generator for the 3DS port.

Walks the game's VFS (BSA archives in openmw.cfg fallback-archive order, then
loose files), decodes every audio file with ffmpeg (WAV PCM, MP3 voices/music),
optionally downsamples anything above --max-rate, encodes to IMA4 ADPCM in
EXACTLY the block layout openal-soft's FmtIMA4 mixer expects, and writes
<out>/<fnv1a(vfs_path)>.nsa.

At runtime the 3DS port hashes the corrected VFS path with the same FNV-1a:
  - static SFX buffers upload the IMA4 blocks DIRECTLY to OpenAL
    (AL_FORMAT_MONO/STEREO_IMA4 + AL_UNPACK_BLOCK_ALIGNMENT_SOFT) — 4x less
    buffer RAM than PCM16 and zero FFmpeg work;
  - voice/music streams decode IMA4->PCM16 in a ~30-line loop instead of
    running the FFmpeg mp3 decoder.

openal-soft IMA4 block layout (per block, verified against
alc/core/voice.cpp LoadSamples<IMA4Data> in the openal-soft-3ds fork):
  per channel, 4 bytes of header first (channel-interleaved): s16le first
  sample (emitted verbatim), then s16le step index; after all channel
  headers the nibble data follows in 4-byte groups per channel, low nibble
  first. samples_per_block includes the header sample, so a block holds
  1 + (block nibble count) samples and (spb-1) must be divisible by 8.

.nsa header (little-endian u32 fields):
  magic 'NSA1' (0x3141534E), version=1, rate, channels, samples_per_block,
  block_bytes (all channels), num_frames (true length, pre-padding),
  data_size (bytes of block payload that follow).

Usage (WSL):
  python3 preprocess_audio_nsa.py \
      --data-dir "/mnt/c/Users/.../Citra/sdmc/3ds/openmw/Data Files" \
      --out      "/mnt/c/Users/.../Citra/sdmc/3ds/openmw/cache/snd" \
      --ffmpeg   "/mnt/c/.../ffmpeg.exe" --ffprobe "/mnt/c/.../ffprobe.exe"

Requires: audioop (stdlib <=3.12), ffmpeg/ffprobe binaries (Windows .exe via
WSL interop is fine).
"""

import argparse
import audioop
import os
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

AUDIO_EXTS = (".wav", ".mp3", ".ogg", ".flac")

NSA_MAGIC = 0x3141534E  # 'NSA1' LE
NSA_VERSION = 1
SAMPLES_PER_BLOCK = 65  # 1 header sample + 64 nibbles = 36 bytes per channel


def fnv1a(s: str) -> int:
    h = 2166136261
    for ch in s.encode("utf-8"):
        h ^= ch
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def normalize(path: str) -> str:
    return path.replace("\\", "/").lower()


# --- Morrowind BSA reader (same as the texture preprocessor) ----------------

def read_bsa_index(path):
    with open(path, "rb") as f:
        head = f.read(12)
        if len(head) < 12:
            return
        version, hash_off, num = struct.unpack("<III", head)
        if version != 0x100:
            print(f"  ! {os.path.basename(path)}: not a Morrowind BSA (version 0x{version:x})")
            return
        dir_bytes = f.read(hash_off)
        sizes_offsets = struct.unpack_from(f"<{num * 2}I", dir_bytes, 0)
        name_offs = struct.unpack_from(f"<{num}I", dir_bytes, num * 8)
        names_blob = dir_bytes[num * 12 :]
        data_start = 12 + hash_off + num * 8
        for i in range(num):
            size = sizes_offsets[i * 2]
            off = sizes_offsets[i * 2 + 1]
            end = names_blob.find(b"\0", name_offs[i])
            name = names_blob[name_offs[i] : end].decode("cp1252", "replace")
            yield normalize(name), data_start + off, size


def build_vfs(data_dir, cfg_path):
    archives = []
    if cfg_path and os.path.isfile(cfg_path):
        with open(cfg_path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if line.startswith("fallback-archive="):
                    archives.append(line.split("=", 1)[1].strip().strip('"'))
    if not archives:
        archives = [n for n in sorted(os.listdir(data_dir)) if n.lower().endswith(".bsa")]

    vfs = {}
    for arch in archives:
        p = os.path.join(data_dir, arch)
        if not os.path.isfile(p):
            print(f"  ! archive missing: {arch}")
            continue
        n = 0
        for name, off, size in read_bsa_index(p):
            vfs[name] = ("bsa", p, off, size)
            n += 1
        print(f"  {arch}: {n} entries")

    loose = 0
    for root, _dirs, files in os.walk(data_dir):
        for fn in files:
            if fn.lower().endswith(".bsa"):
                continue
            full = os.path.join(root, fn)
            rel = normalize(os.path.relpath(full, data_dir))
            vfs[rel] = ("file", full)
            loose += 1
    print(f"  loose files: {loose}")
    return vfs


def read_entry(entry) -> bytes:
    if entry[0] == "bsa":
        _, p, off, size = entry
        with open(p, "rb") as f:
            f.seek(off)
            return f.read(size)
    with open(entry[1], "rb") as f:
        return f.read()


# --- decode via ffmpeg -------------------------------------------------------
#
# NOTE: input/output go through TEMP FILES, not pipes. The typical setup runs
# Windows ffmpeg.exe from WSL (interop) and large pipe traffic across that
# boundary deadlocks; files on /mnt/c work reliably from both sides.

def to_win(path: str) -> str:
    """/mnt/c/foo -> C:\\foo for Windows .exe arguments; unix paths pass through."""
    if path.startswith("/mnt/") and len(path) > 6 and path[6] == "/":
        return path[5].upper() + ":" + path[6:].replace("/", "\\")
    return path


def probe(ffprobe, tmp_in):
    """Returns (rate, channels) of the first audio stream."""
    r = subprocess.run(
        [ffprobe, "-v", "error", "-select_streams", "a:0", "-show_entries",
         "stream=sample_rate,channels", "-of", "csv=p=0", "-i", to_win(tmp_in)],
        capture_output=True, timeout=120)
    line = r.stdout.decode("ascii", "replace").strip().splitlines()
    if not line or "," not in line[0]:
        raise RuntimeError(f"ffprobe: no audio stream ({r.stderr.decode('utf-8','replace')[-120:]})")
    rate_s, chans_s = line[0].split(",")[:2]
    return int(rate_s), int(chans_s)


def decode_pcm(ffmpeg, tmp_in, tmp_out, rate, channels):
    """Decode tmp_in to s16le at (rate, channels). Returns bytes."""
    cmd = [ffmpeg, "-v", "error", "-y", "-i", to_win(tmp_in), "-f", "s16le",
           "-acodec", "pcm_s16le", "-ac", str(channels), "-ar", str(rate), to_win(tmp_out)]
    r = subprocess.run(cmd, capture_output=True, timeout=600)
    if r.returncode != 0:
        raise RuntimeError(f"ffmpeg: {r.stderr.decode('utf-8', 'replace')[-160:]}")
    with open(tmp_out, "rb") as f:
        out = f.read()
    if not out:
        raise RuntimeError("ffmpeg: empty decode")
    return out


# --- IMA4 encode --------------------------------------------------------------

# audioop packs the FIRST delta of each byte in the HIGH nibble; the
# openal-soft mixer reads the low nibble first. Swap every byte's nibbles.
_NIBSWAP = bytes(((b >> 4) | ((b & 0xF) << 4)) for b in range(256))


def encode_channel(mono_s16: bytes, spb: int) -> bytes:
    """One channel of s16le -> concatenated per-channel IMA4 blocks
    (4-byte header + (spb-1)/2 nibble bytes each)."""
    n = len(mono_s16) // 2
    nblocks = (n + spb - 1) // spb
    total = nblocks * spb
    if total != n:  # pad by repeating the final sample (max 2.9ms at 22kHz)
        last = mono_s16[-2:] if n else b"\0\0"
        mono_s16 = mono_s16 + last * (total - n)

    out = bytearray()
    index = 0
    nib_bytes = (spb - 1) // 2
    for b in range(nblocks):
        block = mono_s16[b * spb * 2 : (b + 1) * spb * 2]
        first = struct.unpack_from("<h", block, 0)[0]
        out += struct.pack("<hH", first, index)
        # Encoder state starts at (first, index) — the header carries the
        # first sample verbatim, the nibbles encode samples 2..spb.
        adpcm, (pred, index) = audioop.lin2adpcm(block[2:], 2, (first, index))
        assert len(adpcm) == nib_bytes, (len(adpcm), nib_bytes)
        out += adpcm.translate(_NIBSWAP)
    return bytes(out)


def interleave_blocks(chans: list, spb: int) -> bytes:
    """Interleave per-channel block streams into openal-soft's layout:
    all channel headers (4B each), then nibble data in per-channel 4-byte
    groups."""
    if len(chans) == 1:
        return chans[0]
    nib_bytes = (spb - 1) // 2
    per_block = 4 + nib_bytes
    nblocks = len(chans[0]) // per_block
    out = bytearray()
    for b in range(nblocks):
        base = b * per_block
        for c in chans:
            out += c[base : base + 4]  # headers, channel-interleaved
        for off in range(0, nib_bytes, 4):
            for c in chans:
                out += c[base + 4 + off : base + 4 + off + 4]
    return bytes(out)


def convert_one(path, raw, ffmpeg, ffprobe, max_rate, tmp_dir):
    ext = os.path.splitext(path)[1] or ".bin"
    tag = f"{os.getpid():x}_{fnv1a(path):08x}"
    tmp_in = os.path.join(tmp_dir, f"in_{tag}{ext}")
    tmp_out = os.path.join(tmp_dir, f"out_{tag}.raw")
    try:
        with open(tmp_in, "wb") as f:
            f.write(raw)
        src_rate, src_chans = probe(ffprobe, tmp_in)
        rate = min(src_rate, max_rate)
        channels = 1 if src_chans == 1 else 2
        pcm = decode_pcm(ffmpeg, tmp_in, tmp_out, rate, channels)
    finally:
        for t in (tmp_in, tmp_out):
            try:
                os.remove(t)
            except OSError:
                pass

    frames = len(pcm) // (2 * channels)
    if frames == 0:
        raise RuntimeError("decoded to zero frames")

    spb = SAMPLES_PER_BLOCK
    if channels == 1:
        payload = encode_channel(pcm, spb)
    else:
        left = audioop.tomono(pcm, 2, 1.0, 0.0)
        right = audioop.tomono(pcm, 2, 0.0, 1.0)
        payload = interleave_blocks([encode_channel(left, spb), encode_channel(right, spb)], spb)

    block_bytes = (4 + (spb - 1) // 2) * channels
    hdr = struct.pack("<8I", NSA_MAGIC, NSA_VERSION, rate, channels, spb,
                      block_bytes, frames, len(payload))
    return hdr + payload


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--cfg", default=None)
    ap.add_argument("--ffmpeg", required=True)
    ap.add_argument("--ffprobe", required=True)
    ap.add_argument("--max-rate", type=int, default=32728,
        help="downsample anything above this (32728 = NDSP native)")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--only", default=None, help="only paths containing this substring")
    ap.add_argument("--tmp", default=None,
        help="temp dir for ffmpeg in/out files (must be visible to the ffmpeg binary; default <out>/.tmp)")
    args = ap.parse_args()

    tmp_dir = args.tmp or os.path.join(args.out, ".tmp")
    os.makedirs(tmp_dir, exist_ok=True)

    cfg = args.cfg
    if cfg is None:
        guess = os.path.join(os.path.dirname(args.data_dir.rstrip("/\\")), "config", "openmw.cfg")
        cfg = guess if os.path.isfile(guess) else None
    print(f"cfg: {cfg}")

    print("indexing VFS...")
    vfs = build_vfs(args.data_dir, cfg)
    sounds = {p: e for p, e in vfs.items() if p.endswith(AUDIO_EXTS)}
    if args.only:
        sounds = {p: e for p, e in sounds.items() if args.only in p}
    print(f"{len(sounds)} audio files to consider")

    by_hash = {}
    for p in sounds:
        h = fnv1a(p)
        if h in by_hash and by_hash[h] != p:
            print(f"FATAL: fnv1a collision: {by_hash[h]} vs {p}")
            sys.exit(2)
        by_hash[h] = p

    os.makedirs(args.out, exist_ok=True)

    stats = {"done": 0, "skipped": 0, "failed": 0, "bytes": 0}

    def work(item):
        path, entry = item
        out_path = os.path.join(args.out, f"{fnv1a(path):08x}.nsa")
        if not args.force and os.path.exists(out_path):
            stats["skipped"] += 1
            return
        try:
            raw = read_entry(entry)
            blob = convert_one(path, raw, args.ffmpeg, args.ffprobe, args.max_rate, tmp_dir)
        except Exception as e:
            print(f"  ! {path}: {e}")
            stats["failed"] += 1
            return
        with open(out_path, "wb") as f:
            f.write(blob)
        stats["done"] += 1
        stats["bytes"] += len(blob)
        if stats["done"] % 250 == 0:
            print(f"  {stats['done']} done ({stats['bytes'] // (1024 * 1024)} MB)...")

    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        list(ex.map(work, sorted(sounds.items())))

    print(f"done: {stats['done']} converted, {stats['skipped']} skipped, "
          f"{stats['failed']} failed, {stats['bytes'] // (1024 * 1024)} MB written")


if __name__ == "__main__":
    main()
