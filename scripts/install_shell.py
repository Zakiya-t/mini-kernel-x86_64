import sys
import os
import struct

script_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, script_dir)
from minifs_tool import MiniFS, FS_MAGIC

disk_path = 'disk.img'
if not os.path.exists(disk_path):
    print("Creating 1 GiB disk.img...")
    with open(disk_path, 'wb') as f:
        f.truncate(1024 * 1024 * 1024)

fs = MiniFS(disk_path)
needs_format = False
try:
    sb = fs.read_block(0)
    magic = struct.unpack('<I', sb[:4])[0]
    if magic != FS_MAGIC:
        needs_format = True
except Exception:
    needs_format = True

if needs_format:
    print("Formatting disk.img with MiniFS...")
    fs.format('MINIFS_SYSTEM')
    fs.write_file('/demo.c', b'#include <stdio.h>\n\nint main(void) {\n    printf("Hello from MiniKernel!\\n");\n    return 0;\n}\n')
    fs.write_file('/notes.txt', b'MiniKernel persistent storage on ATA Hard Disk.\n')
else:
    print("[OK] MiniFS filesystem verified on disk.img (preserved)")

try:
    fs.mkdir('/bin')
except Exception:
    pass

elf_path = os.path.join('myshell-indp', 'myshell.elf')
if os.path.exists(elf_path):
    with open(elf_path, 'rb') as f:
        data = f.read()
    fs.write_file('/bin/myshell', data)
    fs.write_file('/bin/sh', data)
    fs.write_file('/myshell', data)
    print(f"[OK] Installed /bin/myshell ({len(data)} bytes) on disk.img")
else:
    print(f"[ERROR] {elf_path} not found!")

fs.close()


