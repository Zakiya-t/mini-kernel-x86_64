#!/usr/bin/env python3
import os
import struct
import sys

DISK_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "disk.img")
EXPORT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "exported_fs")

FS_START_LBA = 2048
FS_SECTOR_SIZE = 512
FS_BLOCK_SIZE = 1024
FS_PARTITION_OFFSET = FS_START_LBA * FS_SECTOR_SIZE

def read_block(f, block_idx):
    f.seek(FS_PARTITION_OFFSET + block_idx * FS_BLOCK_SIZE)
    return f.read(FS_BLOCK_SIZE)

def read_inode(f, inode_table_block, inode_id):
    index = inode_id - 1
    inodes_per_block = 16
    block = inode_table_block + (index // inodes_per_block)
    offset = (index % inodes_per_block) * 64

    f.seek(FS_PARTITION_OFFSET + block * FS_BLOCK_SIZE + offset)
    raw = f.read(64)
    if len(raw) < 64:
        return None

    ino_id, ino_type, perm, size, blocks = struct.unpack('<IHHII', raw[:16])
    direct = struct.unpack('<8I', raw[16:48])
    indirect, double_indirect = struct.unpack('<II', raw[48:56])
    created_time, modified_time = struct.unpack('<II', raw[56:64])
    return {
        'id': ino_id,
        'type': ino_type,
        'perm': perm,
        'size': size,
        'blocks': blocks,
        'direct': direct,
        'indirect': indirect,
        'double_indirect': double_indirect
    }

def get_bmap(f, inode, logical_block):
    if logical_block < 8:
        return inode['direct'][logical_block]
    logical_block -= 8
    if logical_block < 256:
        if inode['indirect'] == 0:
            return 0
        raw = read_block(f, inode['indirect'])
        ptrs = struct.unpack('<256I', raw)
        return ptrs[logical_block]
    logical_block -= 256
    if logical_block < 256 * 256:
        if inode['double_indirect'] == 0:
            return 0
        raw1 = read_block(f, inode['double_indirect'])
        ptrs1 = struct.unpack('<256I', raw1)
        idx1 = logical_block // 256
        idx2 = logical_block % 256
        if ptrs1[idx1] == 0:
            return 0
        raw2 = read_block(f, ptrs1[idx1])
        ptrs2 = struct.unpack('<256I', raw2)
        return ptrs2[idx2]
    return 0

def read_file_data(f, inode):
    data = bytearray()
    remaining = inode['size']
    num_blocks = (remaining + FS_BLOCK_SIZE - 1) // FS_BLOCK_SIZE
    for b_idx in range(num_blocks):
        phys_b = get_bmap(f, inode, b_idx)
        chunk = min(remaining, FS_BLOCK_SIZE)
        if phys_b == 0:
            data.extend(b'\x00' * chunk)
        else:
            raw_block = read_block(f, phys_b)
            data.extend(raw_block[:chunk])
        remaining -= chunk
    return bytes(data)

def export_directory(f, inode_table_block, inode_id, dest_dir, rel_path=""):
    inode = read_inode(f, inode_table_block, inode_id)
    if not inode or inode['type'] != 2:
        return

    os.makedirs(dest_dir, exist_ok=True)

    entries = []
    for b in inode['direct']:
        if b == 0:
            continue
        raw_block = read_block(f, b)
        for i in range(FS_BLOCK_SIZE // 32):
            ent_bytes = raw_block[i*32:(i+1)*32]
            eid, etype = struct.unpack('<IB', ent_bytes[:5])
            if eid != 0:
                name_bytes = ent_bytes[5:].split(b'\x00')[0]
                name = name_bytes.decode('ascii', errors='replace')
                entries.append((eid, etype, name))

    for eid, etype, name in entries:
        if name in ('.', '..'):
            continue

        item_rel = f"{rel_path}/{name}" if rel_path else name
        item_dest = os.path.join(dest_dir, name)

        if etype == 2: # DIR
            print(f" [DIR]  {item_rel}/")
            export_directory(f, inode_table_block, eid, item_dest, item_rel)
        elif etype == 1: # FILE
            file_inode = read_inode(f, inode_table_block, eid)
            content = read_file_data(f, file_inode)
            with open(item_dest, 'wb') as out_f:
                out_f.write(content)
            print(f" [FILE] {item_rel} ({len(content)} bytes)")

def main():
    if not os.path.exists(DISK_PATH):
        print(f"Error: disk image not found at {DISK_PATH}")
        sys.exit(1)

    print("================================================================")
    print(" MiniFS Storage Exporter -> Windows File Explorer Folders")
    print("================================================================")
    print(f"Source Disk Image : {DISK_PATH}")
    print(f"Target Directory  : {EXPORT_DIR}")

    with open(DISK_PATH, "rb") as f:
        sb_raw = read_block(f, 0)
        magic, bsize, total_blocks, total_inodes = struct.unpack("<IIII", sb_raw[:16])
        if magic != 0x4D494E32 and magic != 0x4D494E49:
            print(f"Error: Invalid MiniFS magic (0x{magic:08x})")
            sys.exit(1)

        vol_name = sb_raw[48:80].decode('ascii', errors='ignore').split('\x00')[0]
        inode_tbl_block = struct.unpack("<I", sb_raw[32:36])[0]

        print("----------------------------------------------------------------")
        print(f"Mounted Volume    : {vol_name:<30}")
        print("Extracting filesystem hierarchy to Windows folders...\n")

        export_directory(f, inode_tbl_block, 1, EXPORT_DIR)

    print("\n----------------------------------------------------------------")
    print(f"[SUCCESS] All files and folders extracted to:\n   -> {EXPORT_DIR}")
    print("You can now open this folder in Windows File Explorer!")
    print("================================================================")

if __name__ == "__main__":
    main()
