import subprocess
import time
import sys
import os
import select
import re

def strip_ansi(s):
    return re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', s)

def test_model2():
    cmd = [
        "qemu-system-x86_64",
        "-cdrom", "bin/minikernel.iso",
        "-m", "512M",
        "-drive", "file=disk.img,format=raw,index=0,media=disk,file.locking=off",
        "-chardev", "stdio,id=char0,signal=off",
        "-serial", "chardev:char0",
        "-display", "none",
        "-no-reboot"
    ]

    print("[TEST] Starting QEMU for Model 2 Verification...")
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0
    )

    os.set_blocking(proc.stdout.fileno(), False)

    full_output = ""
    def read_until(target, timeout=12):
        nonlocal full_output
        start = time.time()
        buf = ""
        while time.time() - start < timeout:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if r:
                chunk = proc.stdout.read(1024)
                if not chunk:
                    break
                text = chunk.decode('utf-8', errors='replace')
                buf += text
                full_output += text
                sys.stdout.write(text)
                sys.stdout.flush()
                clean = strip_ansi(buf)
                if target in clean or target in buf or "root:~" in clean:
                    return True
            if proc.poll() is not None:
                break
        return False

    # Wait for the MyShell prompt
    print("[WAIT] Waiting for MyShell prompt ('❯')...")
    if not read_until("❯", timeout=15):
        print("\n[FAIL] Did not reach MyShell prompt!")
        proc.kill()
        return False

    print("\n[PASS] Reached MyShell Prompt! Testing 'help' command...")
    time.sleep(0.5)
    proc.stdin.write(b"help\n")
    proc.stdin.flush()
    if not read_until("Built-in Command Suite", timeout=8):
        print("\n[FAIL] Help command timed out!")
        proc.kill()
        return False

    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] Help command succeeded! Testing 'sysinfo'...")
    time.sleep(0.3)
    proc.stdin.write(b"sysinfo\n")
    proc.stdin.flush()
    if not read_until("OS:", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] sysinfo succeeded! Testing 'lscpu'...")
    time.sleep(0.3)
    proc.stdin.write(b"lscpu\n")
    proc.stdin.flush()
    if not read_until("Architecture:", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] lscpu succeeded! Testing 'ls -l'...")
    time.sleep(0.3)
    proc.stdin.write(b"ls -l\n")
    proc.stdin.flush()
    if not read_until("demo.c", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] ls -l succeeded! Testing 'cat /demo.c'...")
    time.sleep(0.3)
    proc.stdin.write(b"cat /demo.c\n")
    proc.stdin.flush()
    if not read_until("Hello from MiniKernel", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] cat succeeded! Testing 'ps'...")
    time.sleep(0.3)
    proc.stdin.write(b"ps\n")
    proc.stdin.flush()
    if not read_until("myshell", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] ps succeeded! Testing 'whoami'...")
    time.sleep(0.3)
    proc.stdin.write(b"whoami\n")
    proc.stdin.flush()
    if not read_until("Ring 3", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] whoami succeeded! Testing 'sched' configuration...")
    time.sleep(0.3)
    proc.stdin.write(b"sched\n")
    proc.stdin.flush()
    if not read_until("Active Policy:", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] sched status succeeded! Testing 'sched quantum 10'...")
    time.sleep(0.3)
    proc.stdin.write(b"sched quantum 10\n")
    proc.stdin.flush()
    if not read_until("Scheduler time quantum updated to 10 ticks", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] sched quantum succeeded! Testing 'sched priority'...")
    time.sleep(0.3)
    proc.stdin.write(b"sched priority\n")
    proc.stdin.flush()
    if not read_until("Priority Preemptive", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] sched priority succeeded! Testing 'sched rr 5'...")
    time.sleep(0.3)
    proc.stdin.write(b"sched rr 5\n")
    proc.stdin.flush()
    if not read_until("Round Robin (RR)", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] sched rr succeeded! Testing 'diskinfo'...")
    time.sleep(0.3)
    proc.stdin.write(b"diskinfo\n")
    proc.stdin.flush()
    if not read_until("QEMU HARDDISK", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] diskinfo succeeded! Testing 'memtest'...")
    time.sleep(0.3)
    proc.stdin.write(b"memtest\n")
    proc.stdin.flush()
    if not read_until("[PASS]", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] memtest succeeded! Testing 'mem'...")
    time.sleep(0.3)
    proc.stdin.write(b"mem\n")
    proc.stdin.flush()
    if not read_until("MiniKernel Memory Telemetry", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] mem succeeded! Testing 'echo Hello from Model 2'...")
    time.sleep(0.3)
    proc.stdin.write(b"echo Hello from Model 2\n")
    proc.stdin.flush()
    if not read_until("Hello from Model 2", timeout=6):
        proc.kill()
        return False
    if not read_until("❯", timeout=6):
        proc.kill()
        return False

    print("\n[PASS] Testing clean ACPI shutdown via 'exit'...")
    time.sleep(0.3)
    proc.stdin.write(b"exit\n")
    proc.stdin.flush()

    try:
        proc.wait(timeout=8)
        print(f"\n[PASS] QEMU terminated cleanly with exit code {proc.returncode}!")
        return True
    except subprocess.TimeoutExpired:
        print("\n[FAIL] QEMU did not shutdown in time!")
        proc.kill()
        return False

if __name__ == "__main__":
    if test_model2():
        print("\n=======================================================")
        print("  ALL MODEL 2 VERIFICATION TESTS PASSED SUCCESSFULLY!  ")
        print("=======================================================")
        sys.exit(0)
    else:
        print("\n=== TESTS FAILED ===")
        sys.exit(1)
