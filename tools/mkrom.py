#!/usr/bin/env python3
"""Pad the linked binary to a valid Sega 32X ROM and fix the header checksum."""

import sys


def main():
    if len(sys.argv) != 3:
        print('usage: mkrom.py <in.bin> <out.32x>', file=sys.stderr)
        return 1

    data = bytearray(open(sys.argv[1], 'rb').read())

    # Round up to a power-of-two size of at least 512 KB - real cartridges
    # (and several emulators) expect that.
    size = 512 * 1024
    while size < len(data):
        size *= 2
    data += b'\0' * (size - len(data))

    # Mega Drive header checksum: sum of every word from 0x200 to the end.
    checksum = 0
    for i in range(0x200, len(data), 2):
        checksum = (checksum + (data[i] << 8) + data[i + 1]) & 0xFFFF
    data[0x18E] = (checksum >> 8) & 0xFF
    data[0x18F] = checksum & 0xFF

    open(sys.argv[2], 'wb').write(bytes(data))
    print(f'wrote {sys.argv[2]}: {len(data)} bytes '
          f'({len(data)/1024/1024:.2f} MB), checksum 0x{checksum:04X}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
