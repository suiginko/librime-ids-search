#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
dump_ids3.py — IDS3 二进制数据表 (ids_data.bin) 反解与文本导出工具
"""

import sys
import struct
import os

BLOCK_NAMES = [
    "基本", "扩A", "扩B", "扩C", "扩D", "扩E", "扩F", "扩G", "扩H", "扩I", "扩J",
    "兼容", "兼容补充", "部首补充", "康熙部首", "笔画", "未知"
]

def dump_ids3(bin_path, output_path=None, sample_count=None):
    if not os.path.exists(bin_path):
        print(f"Error: {bin_path} does not exist.")
        return

    with open(bin_path, 'rb') as f:
        blob = f.read()

    header = blob[:32]
    magic, ver, flags, max_cp, num_chars, num_keys, num_sec = struct.unpack('<4sHHIIII', header[:24])
    if magic != b'IDS3':
        print(f"Error: Invalid magic {magic}, expected IDS3")
        return

    print(f"=== IDS3 Binary Data Overview ===")
    print(f"File Size: {len(blob) / 1024 / 1024:.2f} MB")
    print(f"Magic: {magic.decode('ascii')} (Version {ver})")
    print(f"Total Characters: {num_chars}")
    print(f"Max Codepoint: 0x{max_cp:X}")
    print(f"Index Keys: {num_keys}")
    print(f"Sections: {num_sec}")

    # 读取 15 个节区目录
    sections = {}
    st = 32
    for i in range(num_sec):
        sec_type, pad, off, length = struct.unpack('<IIQQ', blob[st + i * 24: st + (i + 1) * 24])
        sections[sec_type] = (off, length)

    # 1: CP2ID, 2: ID2CP, 3: HOT, 8: COLD_OFF, 9: COLD
    id2cp_off, id2cp_len = sections[2]
    hot_off, hot_len = sections[3]
    cold_off_off, cold_off_len = sections[8]
    cold_off, cold_len = sections[9]

    id2cp = struct.unpack(f'<{num_chars}I', blob[id2cp_off: id2cp_off + num_chars * 4])
    cold_offsets = struct.unpack(f'<{num_chars}I', blob[cold_off_off: cold_off_off + num_chars * 4])

    count_to_export = min(num_chars, sample_count) if sample_count else num_chars

    lines = []
    header_line = "# 汉字\tUnicode\t读音\tIDS结构\t分区\t拆分深度\t字频权重"
    lines.append(header_line)

    for id in range(count_to_export):
        cp = id2cp[id]
        char_str = chr(cp)

        # HOT 记录: freq u32, depth u8, block u8, char_token u16, flags u32 (12字节)
        h_pos = hot_off + id * 12
        freq, depth, block_id, char_token, flags = struct.unpack('<IBBH I', blob[h_pos: h_pos + 12])
        block_name = BLOCK_NAMES[block_id] if block_id < len(BLOCK_NAMES) else "未知"

        # COLD 记录: py_len u16, py bytes, ids_len u16, ids bytes
        c_pos = cold_off + cold_offsets[id]
        py_len = struct.unpack('<H', blob[c_pos: c_pos + 2])[0]
        py_str = blob[c_pos + 2: c_pos + 2 + py_len].decode('utf-8', errors='replace')
        p_ids = c_pos + 2 + py_len
        ids_len = struct.unpack('<H', blob[p_ids: p_ids + 2])[0]
        ids_str = blob[p_ids + 2: p_ids + 2 + ids_len].decode('utf-8', errors='replace')

        line = f"{char_str}\tU+{cp:04X}\t{py_str}\t{ids_str}\t{block_name}\t{depth}\t{freq}"
        lines.append(line)

    if output_path:
        with open(output_path, 'w', encoding='utf-8') as out:
            out.write('\n'.join(lines) + '\n')
        print(f"\n[Success] Exported {count_to_export} characters to: {output_path}")
        print(f"Output File Size: {os.path.getsize(output_path) / 1024 / 1024:.2f} MB")
    else:
        print("\n--- Preview (First 15 characters) ---")
        for l in lines[:16]:
            print(l)

if __name__ == '__main__':
    bin_file = sys.argv[1] if len(sys.argv) > 1 else 'ids_data.bin'
    out_file = sys.argv[2] if len(sys.argv) > 2 else 'ids_data_dumped.txt'
    dump_ids3(bin_file, out_file)
