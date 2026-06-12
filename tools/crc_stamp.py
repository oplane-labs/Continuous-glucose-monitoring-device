#!/usr/bin/env python3
"""
Post-build CRC stamp tool for CGM-3000 firmware.

Computes CRC-32 (IEEE 802.3) over .text and .rodata sections of the linked ELF,
then patches text_crc32 and text_length into the .firmware_header section in
both the ELF and the HEX file.

Usage:
    python crc_stamp.py --elf zephyr.elf [--hex zephyr.hex]
"""

import argparse
import binascii
import struct
import sys
from pathlib import Path

try:
    from elftools.elf.elffile import ELFFile
except ImportError:
    sys.exit("pyelftools required: pip install pyelftools")

# Must match firmware_header.h exactly
FW_HEADER_MAGIC_0   = 0xC63D0001
FW_HEADER_MAGIC_1   = 0x3FC2FFFF
OFFSET_TEXT_CRC32   = 12
OFFSET_TEXT_LENGTH  = 16


def compute_crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def find_section(elf: "ELFFile", name: str):
    for section in elf.iter_sections():
        if section.name == name:
            return section
    return None


def stamp(elf_path: Path) -> tuple:
    with open(elf_path, "r+b") as f:
        elf = ELFFile(f)

        hdr_sec = find_section(elf, ".firmware_header")
        if hdr_sec is None:
            sys.exit("ERROR: .firmware_header section not found in ELF")

        text_sec = find_section(elf, ".text")
        if text_sec is None:
            sys.exit("ERROR: .text section not found in ELF")

        rodata_sec = find_section(elf, ".rodata")

        verified = bytearray(text_sec.data())
        if rodata_sec is not None:
            text_end = text_sec.header.sh_addr + text_sec.header.sh_size
            rodata_start = rodata_sec.header.sh_addr
            if rodata_start > text_end:
                verified += b"\xff" * (rodata_start - text_end)
            verified += rodata_sec.data()

        crc_val = compute_crc32(bytes(verified))
        region_len = len(verified)

        hdr_off = hdr_sec.header.sh_offset
        f.seek(hdr_off + OFFSET_TEXT_CRC32)
        f.write(struct.pack("<I", crc_val))
        f.seek(hdr_off + OFFSET_TEXT_LENGTH)
        f.write(struct.pack("<I", region_len))

        hdr_lma = hdr_sec.header.sh_addr

    print(f"[crc_stamp] CRC-32=0x{crc_val:08X} length={region_len} header_lma=0x{hdr_lma:08X}")
    return crc_val, region_len, hdr_lma


def patch_hex(hex_path: Path, hdr_lma: int, crc_val: int, region_len: int) -> None:
    try:
        from intelhex import IntelHex
    except ImportError:
        print("[crc_stamp] WARNING: intelhex not installed; HEX not patched")
        return

    ih = IntelHex(str(hex_path))
    for i, b in enumerate(struct.pack("<I", crc_val)):
        ih[hdr_lma + OFFSET_TEXT_CRC32 + i] = b
    for i, b in enumerate(struct.pack("<I", region_len)):
        ih[hdr_lma + OFFSET_TEXT_LENGTH + i] = b
    ih.write_hex_file(str(hex_path))
    print(f"[crc_stamp] HEX patched: {hex_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--hex", required=False, type=Path)
    args = parser.parse_args()

    crc_val, region_len, hdr_lma = stamp(args.elf)
    if args.hex and args.hex.exists():
        patch_hex(args.hex, hdr_lma, crc_val, region_len)


if __name__ == "__main__":
    main()
