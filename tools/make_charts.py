#!/usr/bin/env python3
"""Render the README charts (assets/bench.svg, assets/footprint.svg, assets/compare.svg).

Numbers are copied from `make bench`, `tools/map_sizes.py` and bench/compare/ output (see
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
# (name, MicroCS image, MicroPython 1.26 unix port, CPython 3.13) - same machine, same session
bench = [("fib(30) recursion", 41, 254, 97), ("10 M-iteration loop", 95, 780, 895), ("1 M objects + calls", 132, 548, 356)]
fig, ax = plt.subplots(figsize=(8, 3.3))
y = range(len(bench))
h = 0.26
series = [(1, VIOLET, "MicroCS (optimized image)"), (2, "#f59e0b", "MicroPython 1.26"), (3, GREY, "CPython 3.13")]
for k, (col, color, label) in enumerate(series):
    bars = ax.barh([i + (k - 1) * h for i in y], [b[col] for b in bench], h, color=color, label=label)
    for r in bars:
        ax.text(r.get_width() + 10, r.get_y() + r.get_height() / 2, "%d ms" % r.get_width(), va="center", color=FG, fontsize=9.5)
ax.set_yticks(list(y)); ax.set_yticklabels([b[0] for b in bench], color=FG)
ax.invert_yaxis(); ax.set_xlim(0, 1060); ax.set_xticks([])
leg = ax.legend(loc="lower right", frameon=False, fontsize=9.5)
for t in leg.get_texts(): t.set_color(FG)
ax.set_title("PC x86-64, best of 5 — lower is better", fontsize=11, loc="left", color=FG)
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

# --- bare-metal C vs MicroCS vs MicroPython vs .NET nanoFramework (bench/compare/) ----
# M4F: emulated instructions (millions): C -Os, MicroCS optimized image, MicroPython .mpy
mcu = [("fib", 0.059, 1.43, 5.23), ("loop", 0.51, 6.70, 23.71), ("objects", 0.30, 3.19, 7.45),
       ("sensor", 0.25, 0.68, 2.15), ("strings", 0.13, 0.49, 1.79)]
# PC x86-64, ms: C -O2, MicroCS image, MicroPython 1.26 unix port, nanoFramework nanoCLR 1.1.311 virtual device
pc = [("fib(30)", 1.3, 41, 254, 717), ("loop 10 M", 12, 95, 780, 1485), ("objects 1 M", 32, 132, 548, 4272)]
ORANGE, BLUE, CGREY = "#f59e0b", "#3b82f6", "#64748b"
fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 4.2), gridspec_kw={"width_ratios": [1.15, 1]})
h = 0.27
for k, (col, color, label) in enumerate([(1, CGREY, "bare-metal C"), (2, VIOLET, "MicroCS 1.6"), (3, ORANGE, "MicroPython 1.26")]):
    bars = a1.barh([i + (k - 1) * h for i in range(len(mcu))], [m[col] for m in mcu], h, color=color, label=label)
    for r in bars:
        w = r.get_width()
        a1.text(w + 0.3, r.get_y() + r.get_height() / 2, ("%.2f M" if w >= 0.1 else "%.3f M") % w, va="center", color=FG, fontsize=8.5)
a1.set_yticks(range(len(mcu))); a1.set_yticklabels([m[0] for m in mcu], color=FG)
a1.invert_yaxis(); a1.set_xlim(0, 29); a1.set_xticks([])
a1.set_title("Cortex-M4F (emulated), instructions", fontsize=11, loc="left", color=FG)
h = 0.2
for k, (col, color, label) in enumerate([(1, CGREY, "bare-metal C"), (2, VIOLET, "MicroCS 1.6"), (3, ORANGE, "MicroPython 1.26"), (4, BLUE, ".NET nanoFramework")]):
    bars = a2.barh([i + (k - 1.5) * h for i in range(len(pc))], [m[col] for m in pc], h, color=color, label=label)
    for r in bars:
        w = r.get_width()
        a2.text(w * 1.1, r.get_y() + r.get_height() / 2, ("%d ms" if w >= 10 else "%.1f ms") % w, va="center", color=FG, fontsize=8.5)
a2.set_xscale("log"); a2.set_xlim(0.5, 30000); a2.set_xticks([]); a2.minorticks_off()
a2.set_yticks(range(len(pc))); a2.set_yticklabels([m[0] for m in pc], color=FG); a2.invert_yaxis()
a2.set_title("PC x86-64, ms (log scale)", fontsize=11, loc="left", color=FG)
for ax in (a1, a2):
    style(ax); ax.spines["bottom"].set_visible(False)
fig.suptitle("Same work, same machine — lower is better", x=0.01, y=0.97, ha="left", color=FG, fontsize=11)
fig.tight_layout(rect=(0, 0, 1, 0.92))
hs, ls = a2.get_legend_handles_labels()
leg = fig.legend(hs, ls, loc="upper right", ncol=4, frameon=False, fontsize=9, bbox_to_anchor=(0.99, 0.995))
for t in leg.get_texts(): t.set_color(FG)
fig.savefig("assets/compare.svg", transparent=True)
print("wrote assets/bench.svg assets/footprint.svg assets/compare.svg")
