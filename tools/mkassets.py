#!/usr/bin/env python3
"""
God of Thunder 32X - asset pipeline.

Reads the original DOS GOTRES.DAT, decodes the encrypted directory and the
LZSS-compressed payloads, converts all Mode-X "paned" bitmaps into linear
chunky bitmaps, and emits a single flat archive (gotres.bin) plus a C header
describing it. The archive is embedded directly into the 32X ROM image.

Archive layout (all little-endian is NOT used - SH2 runs big-endian, so
everything is written big-endian to allow direct struct access from C):

  struct got_dir_hdr { u32 magic; u32 count; }
  struct got_entry   { char name[12]; u32 offset; u32 size; u32 flags; } * count
  ... payload blob ...
"""

import argparse
import os
import re
import struct
import sys

MAGIC = 0x474F5433  # 'GOT3'

RES_MAX_ENTRIES = 256
RES_HEADER_ENTRY_SIZE = 23

# Resource kinds
K_RAW = 0
K_TILES = 1   # array of 262-byte Mode-X 16x16 blocks -> linear 16x16
K_PIC = 2     # single paned picture (variable size, 6-byte header)
K_ACTOR = 3   # 5200 bytes: 5120 pane data (16 frames of 16x16) + 40 info
K_PAL = 4


def decrypt_dir(buf):
    return bytes((b ^ ((0x80 + i) & 0xFF)) & 0xFF for i, b in enumerate(buf))


def lzss_decompress(src, dest_size):
    """Exact reimplementation of the original GOT LZSS (relative window)."""
    dest = bytearray()
    i = 0
    while len(dest) < dest_size:
        v = src[i]
        i += 1
        for _ in range(8):
            if len(dest) >= dest_size:
                return bytes(dest)
            if v & 1:
                dest.append(src[i])
                i += 1
            else:
                off = src[i] | (src[i + 1] << 8)
                i += 2
                count = (off >> 12) + 2
                off &= 0xFFF
                start = len(dest) - off
                for q in range(count):
                    dest.append(dest[start + q])
            v >>= 1
    return bytes(dest)


class ResFile:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        raw = self.data[:RES_MAX_ENTRIES * RES_HEADER_ENTRY_SIZE]
        hdr = decrypt_dir(raw)
        self.entries = {}
        self.order = []
        for n in range(RES_MAX_ENTRIES):
            e = hdr[n * RES_HEADER_ENTRY_SIZE:(n + 1) * RES_HEADER_ENTRY_SIZE]
            name = e[:9].split(b'\0')[0].decode('latin1')
            off, size, usize = struct.unpack('<III', e[9:21])
            key = struct.unpack('<H', e[21:23])[0]
            if off == 0 and size == 0:
                continue
            self.entries[name.upper()] = (off, size, usize, key)
            self.order.append(name.upper())

    def read(self, name):
        off, size, usize, key = self.entries[name.upper()]
        raw = self.data[off:off + size]
        if key != 0:
            declen = struct.unpack('<H', raw[:2])[0]
            flag = struct.unpack('<H', raw[2:4])[0]
            assert flag == 1, f'{name}: unexpected compression flag {flag}'
            out = lzss_decompress(raw[4:], declen)
            assert len(out) == declen
            return out
        return raw


def depane(src, w, h):
    """Convert Mode-X 4-plane data to linear chunky pixels.

    Plane p holds every 4th pixel starting at column p.
    """
    out = bytearray(w * h)
    stride = w // 4
    idx = 0
    for plane in range(4):
        for y in range(h):
            row = y * w
            for x in range(stride):
                out[row + x * 4 + plane] = src[idx]
                idx += 1
    return bytes(out)


def conv_pic_block(blk):
    """A 'pic' block: u16 width/4, u16 height, u16 transparent, then panes."""
    w4, h, _transp = struct.unpack('<HHH', blk[:6])
    w = w4 * 4
    body = blk[6:]
    need = w * h
    if len(body) < need:
        body = body + b'\0' * (need - len(body))
    px = depane(body, w, h)
    # Normalise transparency: colour 15 is treated as transparent like colour 0
    px = bytes(0 if c == 15 else c for c in px)
    return w, h, px


def conv_tiles(data, block=262):
    """Convert an array of fixed-size pic blocks into linear 16x16 tiles."""
    n = len(data) // block
    out = bytearray()
    meta = []
    for i in range(n):
        blk = data[i * block:(i + 1) * block]
        w, h, px = conv_pic_block(blk)
        meta.append((w, h))
        out += px
    # sanity: everything should be uniform for tile sets
    return bytes(out), meta


LEVEL_SIZE = 512
# byte offsets of the s16 arrays inside a LEVEL record
LEVEL_U16_RANGES = ((352, 30), (412, 30))   # static_x[30], static_y[30]


def swap_level_words(data):
    """Byte-swap the 16-bit fields of every 512-byte LEVEL record."""
    out = bytearray(data)
    for base in range(0, len(out) - LEVEL_SIZE + 1, LEVEL_SIZE):
        for off, count in LEVEL_U16_RANGES:
            for i in range(count):
                p = base + off + i * 2
                out[p], out[p + 1] = out[p + 1], out[p]
    return bytes(out)


def conv_actor(data):
    """ACTORn layout (see ACTOR_DATA in 1_DEFINE.H):

        char pic[16][256];   /* 4096 - 16 frames of 16x16, linear          */
        char shot[4][256];   /* 1024 - projectile frames               */
        ACTOR_NFO actor_info;/*   40 - at offset 5120                  */
        ACTOR_NFO shot_info; /*   40                                   */

    Each frame is a bare 256-byte block with no per-block header
    (unlike the 262-byte tile blocks).
    Unlike the background tiles, actor frames are stored *linearly* - the
    original blits them with xput/createSurface, which copy 256 bytes
    straight through with no plane juggling. Only colour 15 needs folding
    onto 0, since the engine treats both as transparent.
    """
    frames = bytearray()
    for f in range(16):
        blk = data[f * 256:(f + 1) * 256]
        frames += bytes(0 if c == 15 else c for c in blk)
    info = data[5120:5160]
    return bytes(frames), info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--res', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--header', required=True)
    ap.add_argument('--manifest')
    args = ap.parse_args()

    rf = ResFile(args.res)
    blobs = []   # (name, bytes, flags)

    def add(name, payload, flags=0):
        assert len(name) < 12, name
        blobs.append((name.upper(), payload, flags))

    # ---- palette ----------------------------------------------------------
    pal = rf.read('PALETTE')
    add('PALETTE', pal, K_PAL)
    if 'STORYPAL' in rf.entries:
        add('STORYPAL', rf.read('STORYPAL'), K_PAL)

    # ---- background tile sets (16x16, 262-byte blocks) --------------------
    for area in (1, 2, 3):
        nm = f'BPICS{area}'
        if nm in rf.entries:
            px, meta = conv_tiles(rf.read(nm), 262)
            assert all(m == (16, 16) for m in meta), f'{nm} not 16x16: {set(meta)}'
            add(nm, px, K_TILES)

    # ---- objects (32 x 16x16) --------------------------------------------
    px, meta = conv_tiles(rf.read('OBJECTS'), 262)
    add('OBJECTS', px, K_TILES)

    # ---- level/screen data -----------------------------------------------
    # LEVEL records embed 16-bit fields (static_x/static_y) that Turbo C
    # wrote little-endian on x86. The SH2 is big-endian and the C struct is
    # mapped straight onto these bytes, so the halfwords must be swapped
    # here or every object lands off-screen (x=2 reads back as 512).
    for area in (1, 2, 3):
        nm = f'SDAT{area}'
        if nm in rf.entries:
            add(nm, swap_level_words(rf.read(nm)), K_RAW)

    # ---- actors -----------------------------------------------------------
    actor_ids = []
    for name in rf.order:
        m = re.fullmatch(r'ACTOR(\d+)', name)
        if m:
            actor_ids.append(int(m.group(1)))
    actor_ids.sort()
    for aid in actor_ids:
        data = rf.read(f'ACTOR{aid}')
        if len(data) < 5160:
            data = data + b'\0' * (5160 - len(data))
        frames, info = conv_actor(data)
        add(f'ACTOR{aid}', frames + info, K_ACTOR)

    # ---- faces / single pictures -----------------------------------------
    for name in rf.order:
        if re.fullmatch(r'FACE\d+', name) or name in (
                'ODINPIC', 'HAMPIC', 'OPENBACK', 'STORYPIC'):
            w, h, px = conv_pic_block(rf.read(name))
            add(name, struct.pack('>HH', w, h) + px, K_PIC)

    # ---- status panel & opening pictures ---------------------------------
    # STATUS is a normal pic block: 6-byte header then 320x48 paned pixels.
    w, h, px = conv_pic_block(rf.read('STATUS'))
    assert (w, h) == (320, 48), f'unexpected STATUS geometry {w}x{h}'
    add('STATUS', struct.pack('>HH', w, h) + px, K_PIC)

    for name in rf.order:
        if re.fullmatch(r'OPENP\d+', name):
            add(name, rf.read(name), K_RAW)

    # ---- text / font ------------------------------------------------------
    add('TEXT', rf.read('TEXT'), K_RAW)

    # ---- speech / story / misc -------------------------------------------
    for name in rf.order:
        if (re.fullmatch(r'SPEAK\d+', name) or re.fullmatch(r'STORY\d+', name)
                or name in ('RANDOM', 'DEMO')):
            add(name, rf.read(name), K_RAW)

    # ---- boss data --------------------------------------------------------
    for name in rf.order:
        if re.fullmatch(r'BOSS[VP]\d+', name):
            add(name, rf.read(name), K_RAW)

    # ---- songs (kept raw; music is stubbed for now, PWM later) -----------
    for name in rf.order:
        if re.fullmatch(r'SONG\d*', name) or name in (
                'WINSONG', 'BOSSSONG', 'OPENSONG'):
            add(name, rf.read(name), K_RAW)

    # ---- sound effects ----------------------------------------------------
    for name in ('DIGSOUND', 'PCSOUNDS'):
        if name in rf.entries:
            add(name, rf.read(name), K_RAW)

    # ---- serialise --------------------------------------------------------
    count = len(blobs)
    dir_size = 8 + count * 24
    payload = bytearray()
    dirent = bytearray()
    dirent += struct.pack('>II', MAGIC, count)
    # first pass to compute offsets
    off = dir_size
    for name, data, flags in blobs:
        nb = name.encode('ascii')
        nb = nb + b'\0' * (12 - len(nb))
        dirent += nb + struct.pack('>III', off, len(data), flags)
        payload += data
        # pad each entry to 4 bytes for aligned access from SH2
        pad = (-len(data)) & 3
        payload += b'\0' * pad
        off += len(data) + pad

    blob = bytes(dirent) + bytes(payload)
    with open(args.out, 'wb') as f:
        f.write(blob)

    with open(args.header, 'w') as f:
        f.write('/* Auto-generated by tools/mkassets.py - do not edit. */\n')
        f.write('#ifndef GOT_ASSETS_H\n#define GOT_ASSETS_H\n\n')
        f.write(f'#define GOTRES_MAGIC 0x{MAGIC:08X}u\n')
        f.write(f'#define GOTRES_COUNT {count}\n')
        f.write(f'#define GOTRES_SIZE {len(blob)}\n\n')
        f.write('#define RESK_RAW   0\n#define RESK_TILES 1\n')
        f.write('#define RESK_PIC   2\n#define RESK_ACTOR 3\n#define RESK_PAL   4\n\n')
        f.write('#endif /* GOT_ASSETS_H */\n')

    if args.manifest:
        with open(args.manifest, 'w') as f:
            f.write('name,offset,size,flags\n')
            o = dir_size
            for name, data, flags in blobs:
                f.write(f'{name},{o},{len(data)},{flags}\n')
                o += len(data) + ((-len(data)) & 3)

    print(f'assets: {count} entries, {len(blob)} bytes '
          f'({len(blob)/1024:.1f} KB) -> {args.out}')


if __name__ == '__main__':
    sys.exit(main())
