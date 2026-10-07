#!/usr/bin/env python3
"""Render the README charts (assets/bench.svg, assets/footprint.svg, assets/compare.svg).

Numbers are copied from `make bench` and `tools/map_sizes.py` output (see
docs/PERFORMANCE.md); update them here and re-run after re-measuring.
Transparent background + mid-grey text so the SVGs work in light and dark mode.
"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.family": "DejaVu Sans", "svg.fonttype": "path", "font.size": 11})
FG = "#8b949e"
VIOLET, CYAN, GREY = "#8b5cf6", "#06b6d4", "#94a3b8"

def style(ax):
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    ax.spines["bottom"].set_color(FG)
    ax.tick_params(colors=FG, length=0)
    ax.xaxis.label.set_color(FG)
    ax.title.set_color(FG)

# --- host benchmarks (ms, best of 5; lower is better) -------------------------
# (name, MicroCS 1.6 image, MicroCS 1.5 image, CPython 3.13) - same machine, same session
bench = [("fib(30) recursion", 41, 73, 97), ("10 M-iteration loop", 95, 322, 895), ("1 M objects + calls", 132, 241, 356)]
fig, ax = plt.subplots(figsize=(8, 3.3))
y = range(len(bench))
h = 0.26
series = [(1, VIOLET, "MicroCS 1.6 (optimized image)"), (2, "#c4b5fd", "MicroCS 1.5 (image)"), (3, GREY, "CPython 3.13")]
for k, (col, color, label) in enumerate(series):
    bars = ax.barh([i + (k - 1) * h for i in y], [b[col] for b in bench], h, color=color, label=label)
    for r in bars:
        ax.text(r.get_width() + 10, r.get_y() + r.get_height() / 2, "%d ms" % r.get_width(), va="center", color=FG, fontsize=9.5)
ax.set_yticks(list(y)); ax.set_yticklabels([b[0] for b in bench], color=FG)
ax.invert_yaxis(); ax.set_xlim(0, 1060); ax.set_xticks([])
leg = ax.legend(loc="lower right", frameon=False, fontsize=9.5)
for t in leg.get_texts(): t.set_color(FG)
ax.set_title("Host x86-64, gcc -O2, best of 5 — lower is better", fontsize=11, loc="left", color=FG)
style(ax); ax.spines["bottom"].set_visible(False)
fig.tight_layout()
fig.savefig("assets/bench.svg", transparent=True)

# --- flash by component (m33-full firmware, bytes) ----------------------------
parts = [("VM core + loader", 69070, VIOLET), ("stdlib", 67192, "#a78bfa"), ("compiler", 45002, CYAN),
         ("newlib libm+libc", 50090, GREY), ("hal", 32286, "#67e8f9"), ("fs", 9071, "#22d3ee"),
         ("other", 8172 + 6063 + 1428 + 76, "#cbd5e1")]
total = sum(p[1] for p in parts)
fig, ax = plt.subplots(figsize=(8, 1.9))
left = 0
for name, v, c in parts:
    ax.barh(0, v, left=left, color=c, height=0.55, edgecolor="none")
    if v / total > 0.06 and name != "other":
        ax.text(left + v / 2, 0, "%s\n%.0f KB" % (name, v / 1024), ha="center", va="center", fontsize=9, color="#0b1020")
    left += v
ax.set_xlim(0, total); ax.set_ylim(-0.5, 0.5); ax.set_yticks([]); ax.set_xticks([])
small = [p for p in parts if p[1] / total <= 0.06 or p[0] == "other"]
ax.set_xlabel("  ·  ".join("%s %.1f KB" % (p[0], p[1] / 1024) for p in small) + "  ·  other = libgcc, port, scheduler", fontsize=9)
ax.set_title("Cortex-M33 firmware (-Os), flash by component — the compiler is optional", fontsize=11, loc="left", color=FG)
style(ax); ax.spines["bottom"].set_visible(False)
fig.tight_layout()
fig.savefig("assets/footprint.svg", transparent=True)

# --- MicroCS vs MicroPython vs .NET nanoFramework (bench/compare/) -------------
# M4F: emulated instructions (millions), MicroCS optimized image vs MicroPython .mpy
mcu = [("fib", 1.43, 5.23), ("loop", 6.70, 23.71), ("objects", 3.19, 7.45), ("sensor", 0.68, 2.15), ("strings", 0.49, 1.79)]
# PC x86-64, ms: MicroCS image, MicroPython 1.26 unix port, nanoFramework nanoCLR 1.1.311 virtual device
pc = [("fib(30)", 41, 254, 717), ("loop 10 M", 95, 780, 1485), ("objects 1 M", 132, 548, 4272)]
ORANGE, BLUE = "#f59e0b", "#3b82f6"
fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 3.6), gridspec_kw={"width_ratios": [1.15, 1]})
h = 0.38
for k, (col, color, label) in enumerate([(1, VIOLET, "MicroCS 1.6"), (2, ORANGE, "MicroPython 1.26")]):
    bars = a1.barh([i + (k - 0.5) * h for i in range(len(mcu))], [m[col] for m in mcu], h, color=color, label=label)
    for r in bars:
        a1.text(r.get_width() + 0.3, r.get_y() + r.get_height() / 2, "%.2f M" % r.get_width(), va="center", color=FG, fontsize=9)
a1.set_yticks(range(len(mcu))); a1.set_yticklabels([m[0] for m in mcu], color=FG)
a1.invert_yaxis(); a1.set_xlim(0, 29); a1.set_xticks([])
a1.set_title("Cortex-M4F (emulated), instructions", fontsize=11, loc="left", color=FG)
h = 0.27
for k, (col, color, label) in enumerate([(1, VIOLET, "MicroCS 1.6"), (2, ORANGE, "MicroPython 1.26"), (3, BLUE, ".NET nanoFramework")]):
    bars = a2.barh([i + (k - 1) * h for i in range(len(pc))], [m[col] for m in pc], h, color=color, label=label)
    for r in bars:
        a2.text(r.get_width() * 1.08, r.get_y() + r.get_height() / 2, "%d ms" % r.get_width(), va="center", color=FG, fontsize=9)
a2.set_xscale("log"); a2.set_xlim(20, 20000); a2.set_xticks([]); a2.minorticks_off()
a2.set_yticks(range(len(pc))); a2.set_yticklabels([m[0] for m in pc], color=FG); a2.invert_yaxis()
a2.set_title("PC x86-64, ms (log scale)", fontsize=11, loc="left", color=FG)
for ax in (a1, a2):
    style(ax); ax.spines["bottom"].set_visible(False)
fig.suptitle("Same scripts, same machine — lower is better", x=0.01, y=0.97, ha="left", color=FG, fontsize=11)
fig.tight_layout(rect=(0, 0, 1, 0.93))
hs, ls = a2.get_legend_handles_labels()
leg = fig.legend(hs, ls, loc="upper right", ncol=3, frameon=False, fontsize=9, bbox_to_anchor=(0.99, 0.995))
for t in leg.get_texts(): t.set_color(FG)
fig.savefig("assets/compare.svg", transparent=True)
print("wrote assets/bench.svg assets/footprint.svg assets/compare.svg")
