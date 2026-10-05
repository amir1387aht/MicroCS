#!/usr/bin/env python3
"""Approximate flash/RAM usage by component from a GNU ld map file (post --gc-sections).
usage: map_sizes.py firmware.map"""
import re, sys, collections

def group(obj):
    o = obj.replace("\\", "/")
    if "/libc_nano.a" in o or "/libc.a" in o: return "newlib (libc)"
    if "/libm.a" in o: return "newlib (libm)"
    if "/libgcc.a" in o: return "libgcc (soft-float/div helpers)"
    if "/libnosys.a" in o or "crt" in o.split("/")[-1]: return "crt/nosys"
    b = o.split("/")[-1].rsplit(".", 1)[0]
    if b in ("mcs_compiler", "mcs_parser", "mcs_lexer"): return "MicroCS compiler (lexer/parser/codegen)"
    if b.startswith("mcs_lib"): return "MicroCS stdlib"
    if b in ("mcs_vfs", "mcs_vfs_ram", "mcs_vfs_posix", "mcs_fs_lib"): return "module: fs (VFS + ramfs + File API)"
    if b.startswith("mcs_hal"): return "module: hal (+ simulator)"
    if b == "mcs_sched": return "module: scheduler"
    if b == "mcs_shell": return "module: shell"
    if b.startswith("mcs_"): return "MicroCS VM core (vm/gc/objects/loader)"
    if b in ("main", "board", "startup"): return "port (main.c includes the demo image)"
    return "other"

def main(path):
    lines = open(path, errors="replace").read().split("\n")
    try:
        start = next(i for i, l in enumerate(lines) if l.startswith("Linker script and memory map"))
    except StopIteration:
        sys.exit("not a GNU ld map")
    flash = collections.Counter(); ram = collections.Counter()
    sec = None
    pend = None
    for l in lines[start:]:
        m = re.match(r"^ (\.[\w.$]+)\s*$", l)
        if m: pend = m.group(1); continue
        m = re.match(r"^ (\.[\w.$]+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", l)
        if not m: pend = None; continue
        name = m.group(1) or pend; pend = None
        if not name: continue
        addr, size, obj = int(m.group(2), 16), int(m.group(3), 16), m.group(4)
        if size == 0: continue
        g = group(obj)
        if name.startswith((".text", ".rodata", ".ARM", ".isr")) and addr < 0x20000000: flash[g] += size
        elif name.startswith(".data"): flash[g] += size; ram[g] += size
        elif name.startswith((".bss", "COMMON")) or name == "COMMON": ram[g] += size
    print("%-44s %9s %9s" % ("component", "flash", "ram(static)"))
    for g in sorted(set(flash) | set(ram), key=lambda k: -flash[k]):
        print("%-44s %9d %9d" % (g, flash[g], ram[g]))
    print("%-44s %9d %9d" % ("TOTAL (input sections)", sum(flash.values()), sum(ram.values())))
    print("note: sizes are linker input sections after --gc-sections; mergeable string\n"
          "literals are counted before merging, so TOTAL is slightly above `size` text.")

if __name__ == "__main__":
    main(sys.argv[1])
