#!/usr/bin/env python3
"""Run a MicroCS Cortex-M firmware ELF inside the Unicorn CPU emulator.

Implements the tiny virtual board from ports/cortex-m/board.h:
  0x40000000 UART TX (write)      0x40000004 EXIT (write)
  0x40000008 TICKS ms (read)      0x4000000C INSNS executed (read)
  0x40000010 UART RX status       0x40000014 UART RX data
  0x40000018 phase marker (write; per-phase instruction counts are reported)
stdin feeds the UART RX line (EOF -> 'closed'), UART TX goes to stdout.

Instruction counts are exact (counted per executed instruction). Time is
DERIVED: ticks = instructions / (mhz * 1000), i.e. it assumes 1 instruction per
cycle; real Cortex-M cores need more cycles (flash wait states, multi-cycle
loads, branches), so treat it as a lower bound, not a benchmark of real silicon.

usage: cm_emu.py firmware.elf [--cpu m0|m3|m4|m33|m7] [--mhz 48] [--max-insns N] [--profile N]
  --profile N  print the N functions that executed the most instructions
               (needs arm-none-eabi-nm on PATH to resolve symbols)
requires: pip install unicorn
"""
import argparse, collections, os, select, struct, subprocess, sys
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, \
    UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ, UC_PROT_ALL
from unicorn import arm_const as A

CPUS = {"m0": A.UC_CPU_ARM_CORTEX_M0, "m3": A.UC_CPU_ARM_CORTEX_M3, "m4": A.UC_CPU_ARM_CORTEX_M4,
        "m33": A.UC_CPU_ARM_CORTEX_M33, "m7": A.UC_CPU_ARM_CORTEX_M7}
MMIO = 0x40000000


def load_elf(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 1:
        sys.exit("not an ELF32 file")
    e_phoff, = struct.unpack_from("<I", d, 28)
    e_phentsize, e_phnum = struct.unpack_from("<HH", d, 42)
    segs = []
    for i in range(e_phnum):
        p_type, p_off, p_vaddr, p_paddr, p_filesz, p_memsz, _, _ = struct.unpack_from("<8I", d, e_phoff + i * e_phentsize)
        if p_type == 1 and p_filesz:
            segs.append((p_paddr, d[p_off:p_off + p_filesz]))
    return segs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--cpu", default="m4", choices=sorted(CPUS))
    ap.add_argument("--mhz", type=float, default=48.0, help="assumed clock for the derived ms tick (1 IPC)")
    ap.add_argument("--max-insns", type=int, default=2_000_000_000)
    ap.add_argument("--quiet", action="store_true", help="no summary on stderr")
    ap.add_argument("--profile", type=int, default=0, metavar="N", help="report the N hottest functions")
    ap.add_argument("--lines", type=int, default=0, metavar="N", help="with --profile: also report the N hottest source lines (addr2line)")
    ap.add_argument("--callers", metavar="SYMBOL", help="with --profile: also count who calls SYMBOL (e.g. memcpy)")
    a = ap.parse_args()

    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc.ctl_set_cpu_model(CPUS[a.cpu])
    uc.mem_map(0x00000000, 2 * 1024 * 1024, UC_PROT_ALL)
    uc.mem_map(0x20000000, 512 * 1024, UC_PROT_ALL)
    uc.mem_map(MMIO, 0x1000, UC_PROT_ALL)
    uc.mem_map(0xE0000000, 0x100000, UC_PROT_ALL)  # system control space (CPACR etc.) as plain RAM
    for addr, blob in load_elf(a.elf):
        uc.mem_write(addr, blob)
    sp, reset = struct.unpack("<II", bytes(uc.mem_read(0, 8)))

    st = {"n": 0, "exit": None, "rx": b"", "rx_closed": False, "marks": [], "out": sys.stdout.buffer}
    per_ms = a.mhz * 1000.0

    prof = collections.Counter() if a.profile else None
    callers = collections.Counter()
    watch = symbol_addr(a.elf, a.callers) if a.callers else None

    def on_code(uc_, addr, size, _):
        st["n"] += 1
        if prof is not None:
            prof[addr] += 1
            if addr == watch:
                callers[uc_.reg_read(A.UC_ARM_REG_LR) & ~1] += 1
        if st["n"] >= a.max_insns:
            st["exit"] = 124
            uc_.emu_stop()

    def rx_fill():
        if st["rx"] or st["rx_closed"]:
            return
        r, _, _ = select.select([sys.stdin], [], [], 0)
        if r:
            b = os.read(sys.stdin.fileno(), 4096)
            if b:
                st["rx"] += b
            else:
                st["rx_closed"] = True

    def on_read(uc_, access, addr, size, value, _):
        off = addr - MMIO
        if off == 0x08:
            v = int(st["n"] / per_ms)
        elif off == 0x0C:
            v = st["n"] & 0xFFFFFFFF
        elif off == 0x10:
            rx_fill()
            v = 1 if st["rx"] else (2 if st["rx_closed"] else 0)
        elif off == 0x14:
            rx_fill()
            v = 0
            if st["rx"]:
                v = st["rx"][0]
                st["rx"] = st["rx"][1:]
        else:
            return
        uc_.mem_write(addr, struct.pack("<I", v & 0xFFFFFFFF))

    def on_write(uc_, access, addr, size, value, _):
        off = addr - MMIO
        if off == 0x00:
            try:
                st["out"].write(bytes([value & 0xFF]))
                if value == 10:
                    st["out"].flush()
            except BrokenPipeError:      # host side closed the "UART"
                st["exit"] = 0
                uc_.emu_stop()
        elif off == 0x04:
            st["exit"] = value
            uc_.emu_stop()
        elif off == 0x18:
            st["marks"].append((value, st["n"]))

    uc.hook_add(UC_HOOK_CODE, on_code)
    uc.hook_add(UC_HOOK_MEM_READ, on_read, begin=MMIO, end=MMIO + 0xFFF)
    uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=MMIO, end=MMIO + 0xFFF)
    uc.reg_write(A.UC_ARM_REG_SP, sp)
    try:
        uc.emu_start(reset | 1, 0xFFFFFFFE)
    except UcError as e:
        pc = uc.reg_read(A.UC_ARM_REG_PC)
        st["out"].flush()
        sys.stderr.write("emulator fault: %s at pc=0x%08x after %d instructions\n" % (e, pc, st["n"]))
        return 3
    try:
        st["out"].flush()
    except BrokenPipeError:
        sys.stderr.close()
        os._exit(st["exit"] or 0)
    if not a.quiet:
        sys.stderr.write("[emu] cpu=cortex-%s instructions=%d exit=%s\n" % (a.cpu, st["n"], st["exit"]))
    if prof:
        report_profile(a.elf, prof, a.profile, st["n"])
        if a.lines:
            report_lines(a.elf, prof, a.lines, st["n"])
        if watch is not None:
            report_profile(a.elf, callers, a.profile, sum(callers.values()), "calls to %s by caller" % a.callers)
    return st["exit"] if st["exit"] is not None else 2


def read_syms(elf):
    try:
        out = subprocess.run(["arm-none-eabi-nm", "-S", "--defined-only", elf], capture_output=True, text=True).stdout
    except OSError:
        sys.stderr.write("[profile] arm-none-eabi-nm not found\n"); return []
    syms = []
    for line in out.splitlines():
        f = line.split()
        if len(f) == 4 and f[2] in "tTwW":
            syms.append((int(f[0], 16) & ~1, int(f[1], 16), f[3]))
    return sorted(syms)


def symbol_addr(elf, name):
    for start, _, n in read_syms(elf):
        if n == name:
            return start
    sys.stderr.write("[profile] symbol %s not found\n" % name)
    return None


def report_profile(elf, prof, top, total, what="instructions"):
    """Attribute per-address counts to functions (nm symbol table)."""
    syms = read_syms(elf)
    if not syms or not total:
        return
    starts = [x[0] for x in syms]
    import bisect
    by_fn = collections.Counter()
    for addr, n in prof.items():
        i = bisect.bisect_right(starts, addr) - 1
        name = syms[i][2] if i >= 0 and addr < syms[i][0] + max(syms[i][1], 1) else "?"
        by_fn[name] += n
    sys.stderr.write("[profile] top %d of %d %s\n" % (top, total, what))
    for name, n in by_fn.most_common(top):
        sys.stderr.write("  %6.2f%% %12d  %s\n" % (100.0 * n / total, n, name))


def report_lines(elf, prof, top, total):
    """Attribute per-address counts to source lines (arm-none-eabi-addr2line)."""
    addrs = sorted(prof)
    try:
        out = subprocess.run(["arm-none-eabi-addr2line", "-e", elf] + ["0x%x" % x for x in addrs],
                             capture_output=True, text=True).stdout.splitlines()
    except OSError:
        sys.stderr.write("[profile] arm-none-eabi-addr2line not found\n"); return
    by_line = collections.Counter()
    for addr, loc in zip(addrs, out):
        by_line[os.path.basename(loc.split(" ")[0])] += prof[addr]
    sys.stderr.write("[profile] top %d source lines\n" % top)
    for loc, n in by_line.most_common(top):
        sys.stderr.write("  %6.2f%% %12d  %s\n" % (100.0 * n / total, n, loc))


if __name__ == "__main__":
    sys.exit(main())
