/* Offline, whitespace-only C# formatting. Never rewrite literal/comment bytes.
 * This is not a compiler: ambiguous operators/generic brackets are left alone. */
(function (root) {
  "use strict";
  function literalEnd(src, at, prefix) {
    const quote = prefix[1], verb = prefix[0].includes("@"), interp = prefix[0].includes("$");
    let end = at + prefix[0].length, hole = 0;
    while (end < src.length) {
      if (quote.length >= 3) { if (src.startsWith(quote, end)) return end + quote.length; end++; continue; }
      if (hole) {
        if (src.startsWith("//", end)) { const n = src.indexOf("\n", end); end = n < 0 ? src.length : n; continue; }
        if (src.startsWith("/*", end)) { const n = src.indexOf("*/", end + 2); end = n < 0 ? src.length : n + 2; continue; }
        const inner = /^(?:\$+@|@\$|@|\$+)?("{3,}|"|')/.exec(src.slice(end));
        if (inner) { end = literalEnd(src, end, inner); continue; }
        if (src[end] === "{") hole++; else if (src[end] === "}") hole--;
        end++; continue;
      }
      if (interp && src[end] === "{") { if (src[end + 1] === "{") end += 2; else { hole = 1; end++; } }
      else if (src[end] === quote) { if (verb && src[end + 1] === '"') end += 2; else return end + 1; }
      else if (!verb && src[end] === "\\") end += Math.min(2, src.length - end);
      else end++;
    }
    return end;
  }
  function lex(src) {
    const mask = src.split(""), ranges = [], protectedLines = new Set();
    let row = 0;
    for (let i = 0; i < src.length;) {
      const start = i, firstRow = row;
      let end = i, kind = "";
      if (src.startsWith("//", i)) { end = src.indexOf("\n", i); if (end < 0) end = src.length; kind = "comment"; }
      else if (src.startsWith("/*", i)) { end = src.indexOf("*/", i + 2); end = end < 0 ? src.length : end + 2; kind = "comment"; }
      else {
        const prefix = /^(?:\$+@|@\$|@|\$+)?("{3,}|"|')/.exec(src.slice(i));
        if (prefix) {
          kind = "literal";
          end = literalEnd(src, i, prefix);
        }
      }
      if (kind) {
        for (let k = start; k < end; k++) { if (src[k] === "\n") row++; else mask[k] = " "; }
        if (row > firstRow) for (let r = firstRow; r <= row; r++) protectedLines.add(r);
        ranges.push({ start, end, kind }); i = end;
      } else { if (src[i] === "\n") row++; i++; }
    }
    return { mask: mask.join(""), ranges, protectedLines };
  }
  function spacing(src, base, ranges) {
    const code = s => s.replace(/\b(if|for|foreach|while|switch|catch|lock|using)\s*\(/g, "$1 (")
      .replace(/\s*,\s*/g, ", ").replace(/\s*=>\s*/g, " => ")
      .replace(/(?<![=!<>+*\/%&|?^-])\s*=(?!=|>)\s*/g, " = ")
      .replace(/\s*(==|!=|&&|\|\|)\s*/g, " $1 ");
    let out = "", at = 0;
    for (const r of ranges) {
      if (r.end <= base || r.start >= base + src.length) continue;
      const a = Math.max(0, r.start - base), b = Math.min(src.length, r.end - base);
      out += code(src.slice(at, a)) + src.slice(a, b); at = b;
    }
    return out + code(src.slice(at));
  }
  function reflow(src) {
    const l = lex(src), protectedAt = new Map(l.ranges.map(r => [r.start, r]));
    const eol = src.includes("\r\n") ? "\r\n" : "\n"; let out = "", parens = 0;
    const newline = () => { out = out.replace(/[ \t\r]+$/, ""); if (!out.endsWith("\n")) out += eol; };
    for (let i = 0; i < src.length; i++) {
      const protectedRange = protectedAt.get(i);
      if (protectedRange) { out += src.slice(i, protectedRange.end); i = protectedRange.end - 1; continue; }
      const c = src[i];
      if (l.mask[i] !== c || /\s/.test(c)) { out += c; continue; }
      if (c === "(") parens++; else if (c === ")") parens = Math.max(0, parens - 1);
      if (c === "{") { newline(); out += c; if (!/^\r?\n/.test(src.slice(i + 1, i + 3))) newline(); }
      else if (c === "}") {
        newline(); out += c;
        const tail = src.slice(i + 1).trimStart();
        if (tail && !tail.startsWith("//") && !/^[;,).\]]/.test(tail) && !/^\r?\n/.test(src.slice(i + 1, i + 3))) newline();
      } else { out += c; if (c === ";" && !parens && src.slice(i + 1).trim() && !src.slice(i + 1).trimStart().startsWith("//") && !/^\r?\n/.test(src.slice(i + 1, i + 3))) newline(); }
    }
    return out.replace(/^\r?\n/, "");
  }
  function format(src, options = {}) {
    // Do not normalize CRLF inside literals: preserve every protected byte.
    if (options.reflow) src = reflow(src);
    const l = lex(src), lines = src.split("\n"), blanks = l.mask.split("\n");
    const indent = [2, 4, 8].includes(options.indentSize) ? options.indentSize : 4;
    const out = [], stack = []; let offset = 0, single = -1;
    for (let i = 0; i < lines.length; i++) {
      const raw = lines[i], t = raw.trim(), b = blanks[i].trim(), top = stack[stack.length - 1];
      let ind = top ? top.ind : 0;
      if (/^[}\])]/.test(b) && top) ind = top.at;
      else if (b[0] === "{" && top && top.ch !== "{") ind = top.at;
      else {
        if (top?.sw && !/^(case\b|default\s*:)/.test(b)) ind++;
        if (single >= 0 && b[0] !== "{") ind = Math.max(ind, single);
        else if (/^(\.(?!\.)|\?\?|&&|\|\||\?(?![.\[])|:(?!:))/.test(b) && !top?.sw) ind++;
      }
      const orig = /^[ \t]*/.exec(raw)[0].replace(/\t/g, "    ").length;
      let col = indent * Math.max(0, ind);
      if (top && (top.ch !== "{" || top.inline) && !/^[}\])]/.test(b) && orig > col) col = orig;
      const tailProtected = l.ranges.some(r => r.start < offset + raw.length && r.end >= offset + raw.length);
      const content = raw.endsWith("\r") ? raw.slice(0, -1) : raw;
      const spaced = spacing(content, offset, l.ranges).trimStart();
      const body = tailProtected ? spaced : spaced.trimEnd();
      out.push(l.protectedLines.has(i) ? raw : !t ? (raw.endsWith("\r") ? "\r" : "") : /^#/.test(t) ? content.trimEnd() + (raw.endsWith("\r") ? "\r" : "") : " ".repeat(col) + body + (raw.endsWith("\r") ? "\r" : ""));
      const lineInd = col / indent | 0;
      for (let k = 0; k < b.length; k++) {
        const c = b[k];
        if ("{([".includes(c)) {
          const prev = stack[stack.length - 1];
          const sw = c === "{" && (/\bswitch\s*\(/.test(b.slice(0, k)) || k === 0 && /\bswitch\s*\(/.test(blanks[i - 1] || ""));
          stack.push({ ch: c, at: lineInd, ind: c !== "{" && prev?.line === i && prev.ch !== "{" ? prev.ind : lineInd + 1, line: i, sw, inline: c === "{" && /\S/.test(b.slice(k + 1).replace(/[}\])]+[;,]?\s*$/, "")) });
        } else if ("})]".includes(c)) stack.pop();
      }
      single = /^(if|else|for|foreach|while|using|lock)\b/.test(b) && !/[;{}]$/.test(b) && !/\)\s*\S.*;$/.test(b) || /^else$/.test(b) ? lineInd + 1 : -1;
      offset += raw.length + 1;
    }
    return out.join("\n");
  }
  function mapOffset(before, after, pos) {
    if (pos >= before.length) return after.length;
    const oldRanges = lex(before).ranges, newRanges = lex(after).ranges;
    const index = oldRanges.findIndex(r => pos >= r.start && pos < r.end);
    if (index >= 0 && newRanges[index] && before.slice(oldRanges[index].start, oldRanges[index].end) === after.slice(newRanges[index].start, newRanges[index].end))
      return newRanges[index].start + pos - oldRanges[index].start;
    let count = 0;
    for (let i = 0; i < pos; i++) if (!/\s/.test(before[i])) count++;
    let p = 0;
    for (; p < after.length; p++) if (!/\s/.test(after[p]) && count-- === 0) break;
    return p;
  }
  const api = { lex, format, mapOffset };
  root.StudioFormat = api;
  if (typeof module !== "undefined") module.exports = api;
})(typeof window === "undefined" ? globalThis : window);
