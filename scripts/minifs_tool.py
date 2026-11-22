#!/usr/bin/env python3
"""
minifs_tool.py — Host utility to manipulate MiniKernel MiniFS disk images.
Supports: format, mkdir, cp (host->minifs), ls, cat, export.
Fully supports direct, indirect, and double-indirect blocks.
"""

import os
import sys
import struct
import time

FS_MAGIC = 0x4D494E32  # "MIN2"
FS_BLOCK_SIZE = 1024
FS_SECTOR_SIZE = 512
FS_START_LBA = 2048
FS_PARTITION_OFFSET = FS_START_LBA * FS_SECTOR_SIZE

FS_TOTAL_BLOCKS = 16384
FS_TOTAL_INODES = 256
FS_INODES_PER_BLOCK = 16
FS_INODE_TABLE_BLOCKS = 16

FS_INODE_BITMAP_BLOCK = 1
FS_BLOCK_BITMAP_BLOCK = 2
FS_INODE_TABLE_START = 4
FS_DATA_BLOCKS_START = 20
FS_ROOT_INODE = 1

FS_DIRECT_BLOCKS = 8
FS_PTRS_PER_BLOCK = FS_BLOCK_SIZE // 4 # 256

FS_TYPE_FILE = 1
FS_TYPE_DIR = 2

class MiniFS:
    def __init__(self, disk_path):
        self.disk_path = disk_path
        self.f = open(disk_path, "r+b")

    def close(self):
        self.f.close()

    def read_block(self, block_idx):
        self.f.seek(FS_PARTITION_OFFSET + block_idx * FS_BLOCK_SIZE)
        return bytearray(self.f.read(FS_BLOCK_SIZE))

    def write_block(self, block_idx, data):
        if len(data) < FS_BLOCK_SIZE:
            data = data + b'\x00' * (FS_BLOCK_SIZE - len(data))
        self.f.seek(FS_PARTITION_OFFSET + block_idx * FS_BLOCK_SIZE)
        self.f.write(data[:FS_BLOCK_SIZE])

    def format(self, volume_name="MINIFS_SYSTEM"):
        print(f"Formatting MiniFS volume on {self.disk_path}...")
        # 1. Zero bitmaps and inode table
        zero = b'\x00' * FS_BLOCK_SIZE
        self.write_block(FS_INODE_BITMAP_BLOCK, zero)
        self.write_block(FS_BLOCK_BITMAP_BLOCK, zero)
        for b in range(FS_INODE_TABLE_BLOCKS):
            self.write_block(FS_INODE_TABLE_START + b, zero)

        # 2. Superblock
        vol_bytes = volume_name.encode('ascii')[:31]
        sb = struct.pack(
            "<IIIIIIIIIIII32s",
            FS_MAGIC,
            FS_BLOCK_SIZE,
            FS_TOTAL_BLOCKS,
            FS_TOTAL_INODES,
            FS_TOTAL_BLOCKS - FS_DATA_BLOCKS_START - 1, # free_blocks
            FS_TOTAL_INODES - 1, # free_inodes (root used)
            FS_INODE_BITMAP_BLOCK,
            FS_BLOCK_BITMAP_BLOCK,
            FS_INODE_TABLE_START,
            FS_INODE_TABLE_BLOCKS,
            FS_DATA_BLOCKS_START,
            FS_ROOT_INODE,
            vol_bytes
        )
        sb += b'\x00' * (FS_BLOCK_SIZE - len(sb))
        self.write_block(0, sb)

        # 3. Mark Inode 1 and Block 20 as allocated
        ino_bmp = self.read_block(FS_INODE_BITMAP_BLOCK)
        ino_bmp[0] |= 0x01 # Inode 1
        self.write_block(FS_INODE_BITMAP_BLOCK, ino_bmp)

        blk_bmp = self.read_block(FS_BLOCK_BITMAP_BLOCK)
        blk_bmp[0] |= 0x01 # Data block 0 (physical block FS_DATA_BLOCKS_START)
        self.write_block(FS_BLOCK_BITMAP_BLOCK, blk_bmp)

        # 4. Root inode
        root_data_block = FS_DATA_BLOCKS_START
        now = int(time.time())
        direct = [0] * FS_DIRECT_BLOCKS
        direct[0] = root_data_block
        root_ino = struct.pack(
            "<IHHII8IIIII",
            1, # id
            FS_TYPE_DIR,
            0o755,
            64, # size = 2 * 32 bytes
            1,  # block count
            *direct,
            0, # indirect
            0, # double_indirect
            now,
            now
        )
        self.write_inode(1, root_ino)

        # 5. Root entries (. and ..)
        dot = struct.pack("<IB27s", 1, FS_TYPE_DIR, b".\x00")
        dotdot = struct.pack("<IB27s", 1, FS_TYPE_DIR, b"..\x00")
        root_dir_data = dot + dotdot + b'\x00' * (FS_BLOCK_SIZE - 64)
        self.write_block(root_data_block, root_dir_data)
        print("MiniFS formatted successfully.")

    def read_inode(self, inode_id):
        idx = inode_id - 1
        block = FS_INODE_TABLE_START + (idx // FS_INODES_PER_BLOCK)
        offset = (idx % FS_INODES_PER_BLOCK) * 64
        buf = self.read_block(block)
        raw = buf[offset:offset+64]
        ino_id, ino_type, perm, size, blocks = struct.unpack('<IHHII', raw[:16])
        direct = list(struct.unpack('<8I', raw[16:48]))
        indirect, double_indirect = struct.unpack('<II', raw[48:56])
        created, modified = struct.unpack('<II', raw[56:64])
        return {
            'id': ino_id, 'type': ino_type, 'perm': perm, 'size': size,
            'blocks': blocks, 'direct': direct, 'indirect': indirect,
            'double_indirect': double_indirect, 'created': created, 'modified': modified
        }

    def write_inode(self, inode_id, raw_bytes):
        idx = inode_id - 1
        block = FS_INODE_TABLE_START + (idx // FS_INODES_PER_BLOCK)
        offset = (idx % FS_INODES_PER_BLOCK) * 64
        buf = self.read_block(block)
        buf[offset:offset+64] = raw_bytes[:64]
        self.write_block(block, buf)

    def save_inode(self, ino):
        raw = struct.pack(
            "<IHHII8IIIII",
            ino['id'], ino['type'], ino['perm'], ino['size'], ino['blocks'],
            *ino['direct'], ino['indirect'], ino['double_indirect'],
            ino['created'], ino['modified']
        )
        self.write_inode(ino['id'], raw)

    def alloc_block(self):
        bmp = self.read_block(FS_BLOCK_BITMAP_BLOCK)
        total_data = FS_TOTAL_BLOCKS - FS_DATA_BLOCKS_START
        for i in range(total_data):
            byte_idx = i // 8
            bit = 1 << (i % 8)
            if not (bmp[byte_idx] & bit):
                bmp[byte_idx] |= bit
                self.write_block(FS_BLOCK_BITMAP_BLOCK, bmp)
                phys = FS_DATA_BLOCKS_START + i
                self.write_block(phys, b'\x00' * FS_BLOCK_SIZE)
                return phys
        raise RuntimeError("No free data blocks on disk")

    def alloc_inode(self):
        bmp = self.read_block(FS_INODE_BITMAP_BLOCK)
        for i in range(FS_TOTAL_INODES):
            byte_idx = i // 8
            bit = 1 << (i % 8)
            if not (bmp[byte_idx] & bit):
                bmp[byte_idx] |= bit
                self.write_block(FS_INODE_BITMAP_BLOCK, bmp)
                return i + 1
        raise RuntimeError("No free inodes on disk")

    def bmap(self, ino, logical_block, allocate=True):
        if logical_block < FS_DIRECT_BLOCKS:
            if ino['direct'][logical_block] != 0:
                return ino['direct'][logical_block]
            if not allocate:
                return 0
            new_b = self.alloc_block()
            ino['direct'][logical_block] = new_b
            ino['blocks'] += 1
            return new_b

        logical_block -= FS_DIRECT_BLOCKS
        if logical_block < FS_PTRS_PER_BLOCK:
            if ino['indirect'] == 0:
                if not allocate:
                    return 0
                ino['indirect'] = self.alloc_block()
                ino['blocks'] += 1
            ind_buf = self.read_block(ino['indirect'])
            ptrs = list(struct.unpack('<256I', ind_buf))
            if ptrs[logical_block] != 0:
                return ptrs[logical_block]
            if not allocate:
                return 0
            new_b = self.alloc_block()
            ptrs[logical_block] = new_b
            ino['blocks'] += 1
            self.write_block(ino['indirect'], struct.pack('<256I', *ptrs))
            return new_b

        logical_block -= FS_PTRS_PER_BLOCK
        if logical_block < (FS_PTRS_PER_BLOCK * FS_PTRS_PER_BLOCK):
            idx1 = logical_block // FS_PTRS_PER_BLOCK
            idx2 = logical_block % FS_PTRS_PER_BLOCK
            if ino['double_indirect'] == 0:
                if not allocate:
                    return 0
                ino['double_indirect'] = self.alloc_block()
                ino['blocks'] += 1
            dind_buf = self.read_block(ino['double_indirect'])
            ptrs1 = list(struct.unpack('<256I', dind_buf))
            if ptrs1[idx1] == 0:
                if not allocate:
                    return 0
                ptrs1[idx1] = self.alloc_block()
                ino['blocks'] += 1
                self.write_block(ino['double_indirect'], struct.pack('<256I', *ptrs1))
            ind2_buf = self.read_block(ptrs1[idx1])
            ptrs2 = list(struct.unpack('<256I', ind2_buf))
            if ptrs2[idx2] != 0:
                return ptrs2[idx2]
            if not allocate:
                return 0
            new_b = self.alloc_block()
            ptrs2[idx2] = new_b
            ino['blocks'] += 1
            self.write_block(ptrs1[idx1], struct.pack('<256I', *ptrs2))
            return new_b

        raise RuntimeError(f"Logical block {logical_block} out of capacity")

    def get_dir_entries(self, dir_ino):
        entries = []
        for b in dir_ino['direct']:
            if b == 0:
                continue
            raw = self.read_block(b)
            for i in range(FS_BLOCK_SIZE // 32):
                ent = raw[i*32:(i+1)*32]
                eid, etype = struct.unpack('<IB', ent[:5])
                if eid != 0:
                    name = ent[5:].split(b'\x00')[0].decode('ascii', errors='replace')
                    entries.append((eid, etype, name, b, i))
        return entries

    def add_dir_entry(self, parent_id, child_id, child_type, name):
        p_ino = self.read_inode(parent_id)
        entries_per_block = FS_BLOCK_SIZE // 32
        for b_idx in range(FS_DIRECT_BLOCKS):
            phys_b = p_ino['direct'][b_idx]
            if phys_b == 0:
                phys_b = self.alloc_block()
                p_ino['direct'][b_idx] = phys_b
                p_ino['blocks'] += 1
            raw = self.read_block(phys_b)
            for i in range(entries_per_block):
                eid, = struct.unpack('<I', raw[i*32:i*32+4])
                if eid == 0:
                    name_bytes = name.encode('ascii')[:26]
                    ent = struct.pack('<IB27s', child_id, child_type, name_bytes)
                    raw[i*32:(i+1)*32] = ent
                    self.write_block(phys_b, raw)
                    p_ino['size'] += 32
                    self.save_inode(p_ino)
                    return
        raise RuntimeError("Directory block full")

    def resolve_path(self, path):
        parts = [p for p in path.strip('/').split('/') if p]
        curr_id = FS_ROOT_INODE
        for p in parts:
            c_ino = self.read_inode(curr_id)
            if c_ino['type'] != FS_TYPE_DIR:
                return None
            found = False
            for eid, etype, name, _, _ in self.get_dir_entries(c_ino):
                if name == p:
                    curr_id = eid
                    found = True
                    break
            if not found:
                return None
        return curr_id

    def mkdir(self, path):
        parts = [p for p in path.strip('/').split('/') if p]
        if not parts:
            return
        parent_path = '/' + '/'.join(parts[:-1])
        base = parts[-1]
        p_id = self.resolve_path(parent_path)
        if p_id is None:
            self.mkdir(parent_path)
            p_id = self.resolve_path(parent_path)

        p_ino = self.read_inode(p_id)
        for eid, etype, name, _, _ in self.get_dir_entries(p_ino):
            if name == base:
                return # Already exists

        new_ino_id = self.alloc_inode()
        new_data = self.alloc_block()
        now = int(time.time())

        # . and ..
        dot = struct.pack("<IB27s", new_ino_id, FS_TYPE_DIR, b".\x00")
        dotdot = struct.pack("<IB27s", p_id, FS_TYPE_DIR, b"..\x00")
        blk_data = dot + dotdot + b'\x00' * (FS_BLOCK_SIZE - 64)
        self.write_block(new_data, blk_data)

        direct = [0] * FS_DIRECT_BLOCKS
        direct[0] = new_data
        new_ino = {
            'id': new_ino_id, 'type': FS_TYPE_DIR, 'perm': 0o755,
            'size': 64, 'blocks': 1, 'direct': direct,
            'indirect': 0, 'double_indirect': 0,
            'created': now, 'modified': now
        }
        self.save_inode(new_ino)
        self.add_dir_entry(p_id, new_ino_id, FS_TYPE_DIR, base)

    def write_file(self, target_path, data):
        parts = [p for p in target_path.strip('/').split('/') if p]
        parent_path = '/' + '/'.join(parts[:-1])
        base = parts[-1]
        p_id = self.resolve_path(parent_path)
        if p_id is None:
            self.mkdir(parent_path)
            p_id = self.resolve_path(parent_path)

        # Check if already exists
        p_ino = self.read_inode(p_id)
        existing_id = None
        for eid, etype, name, _, _ in self.get_dir_entries(p_ino):
            if name == base:
                existing_id = eid
                break

        now = int(time.time())
        if existing_id is None:
            file_id = self.alloc_inode()
            direct = [0] * FS_DIRECT_BLOCKS
            f_ino = {
                'id': file_id, 'type': FS_TYPE_FILE, 'perm': 0o644,
                'size': 0, 'blocks': 0, 'direct': direct,
                'indirect': 0, 'double_indirect': 0,
                'created': now, 'modified': now
            }
            self.save_inode(f_ino)
            self.add_dir_entry(p_id, file_id, FS_TYPE_FILE, base)
        else:
            file_id = existing_id
            f_ino = self.read_inode(file_id)

        # Write data blocks
        total_len = len(data)
        num_blocks = (total_len + FS_BLOCK_SIZE - 1) // FS_BLOCK_SIZE
        if num_blocks == 0:
            num_blocks = 0

        for b_idx in range(num_blocks):
            phys_b = self.bmap(f_ino, b_idx, allocate=True)
            chunk = data[b_idx * FS_BLOCK_SIZE : (b_idx + 1) * FS_BLOCK_SIZE]
            self.write_block(phys_b, chunk)

        f_ino['size'] = total_len
        f_ino['modified'] = now
        self.save_inode(f_ino)

    def read_file(self, target_path):
        f_id = self.resolve_path(target_path)
        if f_id is None:
            return None
        f_ino = self.read_inode(f_id)
        if f_ino['type'] != FS_TYPE_FILE:
            return None
        total_len = f_ino['size']
        num_blocks = (total_len + FS_BLOCK_SIZE - 1) // FS_BLOCK_SIZE
        data = bytearray()
        for b_idx in range(num_blocks):
            phys_b = self.bmap(f_ino, b_idx, allocate=False)
            if phys_b == 0:
                data.extend(b'\x00' * FS_BLOCK_SIZE)
            else:
                data.extend(self.read_block(phys_b))
        return bytes(data[:total_len])

    def list_dir(self, dir_id=1, prefix=""):
        d_ino = self.read_inode(dir_id)
        for eid, etype, name, _, _ in self.get_dir_entries(d_ino):
            if name in ('.', '..'):
                continue
            full_name = f"{prefix}/{name}" if prefix else name
            if etype == FS_TYPE_DIR:
                print(f" [DIR]  {full_name}/")
                self.list_dir(eid, full_name)
            else:
                f_ino = self.read_inode(eid)
                print(f" [FILE] {full_name} ({f_ino['size']} bytes, {f_ino['blocks']} blocks)")

def main():
    if len(sys.argv) < 3:
        print("Usage: minifs_tool.py <disk.img> <command> [args...]")
        print("Commands:")
        print("  format")
        print("  ls")
        print("  mkdir <fs_path>")
        print("  cp <host_file> <fs_path>")
        print("  import_dir <host_dir> [fs_dir]")
        sys.exit(1)

    disk = sys.argv[1]
    cmd = sys.argv[2]
    fs = MiniFS(disk)

    try:
        if cmd == "format":
            fs.format()
        elif cmd == "ls":
            fs.list_dir(1)
        elif cmd == "cat":
            content = fs.read_file(sys.argv[3])
            if content is None:
                print(f"File not found: {sys.argv[3]}")
                sys.exit(1)
            sys.stdout.buffer.write(content)
        elif cmd == "mkdir":
            fs.mkdir(sys.argv[3])
            print(f"Created directory {sys.argv[3]}")
        elif cmd == "cp":
            host_path = sys.argv[3]
            fs_path = sys.argv[4]
            with open(host_path, "rb") as in_f:
                data = in_f.read()
            fs.write_file(fs_path, data)
            print(f"Copied {host_path} -> {fs_path} ({len(data)} bytes)")
        elif cmd == "import_dir":
            host_dir = sys.argv[3]
            fs_dir = sys.argv[4] if len(sys.argv) > 4 else ""
            for root, dirs, files in os.walk(host_dir):
                rel = os.path.relpath(root, host_dir).replace('\\', '/')
                target_dir = f"{fs_dir}/{rel}" if rel != "." else fs_dir
                if target_dir:
                    fs.mkdir(target_dir)
                for f in files:
                    h_file = os.path.join(root, f)
                    with open(h_file, "rb") as in_f:
                        d = in_f.read()
                    t_file = f"{target_dir}/{f}" if target_dir else f"/{f}"
                    fs.write_file(t_file, d)
                    print(f"Imported {t_file} ({len(d)} bytes)")
        else:
            print(f"Unknown command: {cmd}")
    finally:
        fs.close()

if __name__ == "__main__":
    main()

