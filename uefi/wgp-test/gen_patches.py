#!/usr/bin/env python3
import sys

def read_hex(path):
    sites, base = [], 0
    with open(path) as f:
        for line in f:
            if not line.startswith(":"):
                continue
            count = int(line[1:3], 16)
            addr = int(line[3:7], 16)
            typ = int(line[7:9], 16)
            data = bytes.fromhex(line[9:9 + count * 2])
            if typ == 2:
                base = int(line[9:13], 16) << 4
            elif typ == 0:
                sites.append((base + addr, data))
    return sorted(sites)

def main():
    if len(sys.argv) < 3:
        print("Usage: gen_patches.py <input.hex> <output.h>", file=sys.stderr)
        sys.exit(1)

    hex_path = sys.argv[1]
    out_path = sys.argv[2]
    sites = read_hex(hex_path)

    with open(out_path, "w") as f:
        f.write("/* Auto-generated from " + hex_path + " - DO NOT EDIT */\n")
        f.write("#ifndef PATCHES_DATA_H\n")
        f.write("#define PATCHES_DATA_H\n\n")
        f.write("static const smu_patch_t g_smu_patches[] = {\n")
        for addr, data in sites:
            bytes_str = ", ".join(f"0x{b:02X}" for b in data)
            f.write(f"    {{ 0x{addr:05X}U, {len(data)}U, {{ {bytes_str} }} }},\n")
        f.write("};\n\n")
        f.write("#define NUM_PATCHES (sizeof(g_smu_patches) / sizeof(g_smu_patches[0]))\n\n")
        f.write("#endif // PATCHES_DATA_H\n")

if __name__ == "__main__":
    main()
