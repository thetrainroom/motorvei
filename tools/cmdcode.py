"""
Decode CarMotion IR Mini code blocks (as CarManager writes them to the CVs) into command data, and back.

Code block: header, stored bytes..., where
  header bit 7   = on-air flag bit (1: data sent as-is, 0: data sent inverted)
  header bits 0-2 = number of bytes (data + CRC)
  stored bytes   = on-air bits inverted, so data = stored ^ (0x00 if flag else 0xFF) ^ 0xFF
Last data byte is CRC-8/MAXIM over the preceding data bytes.

    python tools/cmdcode.py 43 BE 2A 83          # decode a stored block
    python tools/cmdcode.py --encode BE 2A       # data -> CRC, flag, stored block, on-air bits
"""

import sys


def crc8_maxim(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0x8C if c & 1 else c >> 1
    return c


def decode_block(block):
    """Stored CV block -> (data bytes incl. CRC, crc_ok)."""
    hdr, n = block[0], block[0] & 0x07
    stored = block[1:1 + n]
    flag = hdr >> 7
    data = [b ^ 0xFF if flag else b for b in stored]
    return data, crc8_maxim(data[:-1]) == data[-1]


def encode(cmd):
    """Command bytes (without CRC) -> (data, flag, stored block, on-air bit string)."""
    data = list(cmd) + [crc8_maxim(cmd)]
    ones = sum(bin(b).count("1") for b in data)
    flag = 1 if ones < 8 * len(data) - ones else 0
    air = data if flag else [b ^ 0xFF for b in data]
    stored = [b ^ 0xFF for b in air]
    hdr = (flag << 7) | 0x40 | len(data)
    bits = str(flag) + "".join(format(b, "08b") for b in air)
    return data, flag, [hdr] + stored, bits


def main(argv):
    if argv and argv[0] == "--encode":
        data, flag, block, bits = encode([int(x, 16) for x in argv[1:]])
        print("data  ", " ".join("%02X" % b for b in data))
        print("block ", " ".join("%02X" % b for b in block), "(header bit 6 assumed set)")
        print("on-air", bits)
    else:
        data, ok = decode_block([int(x, 16) for x in argv])
        print(" ".join("%02X" % b for b in data), "CRC OK" if ok else "CRC FAIL")


if __name__ == "__main__":
    main(sys.argv[1:])
