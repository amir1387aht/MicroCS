#!/usr/bin/env python3
"""Render the README charts (assets/bench.svg, assets/footprint.svg).

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
bench = [("fib(30) recursion", 74, 109), ("10 M-iteration loop", 330, 931), ("1 M objects + calls", 232, 386)]
fig, ax = plt.subplots(figsize=(8, 2.9))
y = range(len(bench))
h = 0.36
b1 = ax.barh([i - h / 2 for i in y], [b[1] for b in bench], h, color=VIOLET, label="MicroCS (bytecode image)")
b2 = ax.barh([i + h / 2 for i in y], [b[2] for b in bench], h, color=GREY, label="CPython 3.13")
for bars in (b1, b2):
    for r in bars:
        ax.text(r.get_width() + 12, r.get_y() + r.get_height() / 2, "%d ms" % r.get_width(), va="center", color=FG, fontsize=10)
ax.set_yticks(list(y)); ax.set_yticklabels([b[0] for b in bench], color=FG)
ax.invert_yaxis(); ax.set_xlim(0, 1080); ax.set_xticks([])
ax.spines["bottom"].set_visible(False)
leg = ax.legend(loc="lower right", frameon=False, fontsize=10)
for t in leg.get_texts(): t.set_color(FG)
ax.set_title("Host x86-64, gcc -O2, best of 5 — lower is better", fontsize=11, loc="left", color=FG)
style(ax); ax.spines["bottom"].set_visible(False)
fig.tight_layout()
fig.savefig("assets/bench.svg", transparent=True)

# --- flash by component (m33-full firmware, bytes) ----------------------------
parts = [("stdlib", 65500, VIOLET), ("VM core", 53822, "#a78bfa"), ("compiler", 44656, CYAN),
         ("newlib libm+libc", 50210, GREY), ("fs", 9071, "#22d3ee"), ("hal", 5925, "#67e8f9"),
         ("other", 7974 + 6760 + 1428 + 76, "#cbd5e1")]
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
print("wrote assets/bench.svg assets/footprint.svg")
