/* Canonical source keeps its original CRLF/CR/LF bytes. Textarea selections
 * address an LF-only view; offsets must never be used against raw text. */
(() => {
  const display = (text) => String(text).replace(/\r\n|\r/g, "\n");
  function view(raw) {
    const offsets = [0];
    let text = "";
    for (let i = 0; i < raw.length; i++) {
      if (raw[i] === "\r") {
        text += "\n";
        if (raw[i + 1] === "\n") i++;
      } else text += raw[i];
      offsets.push(i + 1);
    }
    return { text, offsets };
  }
  const breaks = (raw) => raw.match(/\r\n|\r|\n/g) || [];
  function replacement(raw, a, b, text, exact, fallback) {
    if (exact) return text;
    // Structural edits (indent/comment/replace) with the same line count keep
    // each separator, including mixed endings inside multiline literals.
    const old = breaks(raw.slice(a, b)),
      plain = display(text);
    const count = (plain.match(/\n/g) || []).length;
    let index = 0;
    return plain.replace(/\n/g, () =>
      old.length === count ? old[index++] : fallback,
    );
  }
  function edits(raw, changes, exact = false) {
    const { text, offsets } = view(raw);
    let end = 0,
      result = "";
    const fallback = (raw.match(/\r\n|\r|\n/) || ["\n"])[0];
    for (const { a, b, text: inserted } of changes) {
      if (
        !Number.isInteger(a) ||
        !Number.isInteger(b) ||
        a < end ||
        b < a ||
        b > text.length
      )
        throw new Error("Invalid source edit range.");
      result +=
        raw.slice(offsets[end], offsets[a]) +
        replacement(raw, offsets[a], offsets[b], inserted, exact, fallback);
      end = b;
    }
    return result + raw.slice(offsets[end]);
  }
  function reconcile(raw, next) {
    const previous = display(raw);
    if (previous === next) return raw;
    let a = 0,
      b = previous.length,
      e = next.length;
    while (a < b && a < e && previous[a] === next[a]) a++;
    while (b > a && e > a && previous[b - 1] === next[e - 1]) {
      b--;
      e--;
    }
    return edits(raw, [{ a, b, text: next.slice(a, e) }]);
  }
  function slice(raw, a, b) {
    const { offsets } = view(raw);
    return raw.slice(offsets[a], offsets[b]);
  }
  function replaceAll(raw, re, substitute, literal) {
    const v = display(raw),
      changes = [];
    v.replace(re, (...args) => {
      const named =
        typeof args[args.length - 1] === "object" ? args.pop() : null;
      const input = args.pop(),
        a = args.pop(),
        matched = args.shift(),
        captures = args;
      const text = literal
        ? substitute
        : substitute.replace(/\$([$&`']|<[^>]*>|\d{1,2})/g, (token, key) => {
            if (key === "$") return "$";
            if (key === "&") return matched;
            if (key === "`") return input.slice(0, a);
            if (key === "'") return input.slice(a + matched.length);
            if (key[0] === "<")
              return named ? named[key.slice(1, -1)] || "" : token;
            let n = Number(key);
            if (n > 0 && n <= captures.length) return captures[n - 1] || "";
            if (
              key.length === 2 &&
              Number(key[0]) > 0 &&
              Number(key[0]) <= captures.length
            )
              return (captures[Number(key[0]) - 1] || "") + key[1];
            return token;
          });
      changes.push({ a, b: a + matched.length, text });
      return matched;
    });
    return edits(raw, changes);
  }
  // Byte-only edits cannot be identified by native textarea undo. Keep a
  // separate, explicitly invoked history of contiguous same-display edits.
  class EolHistory {
    constructor(limit = 128, budget = 2 * 1024 * 1024) {
      this.limit = limit; this.budget = budget; this.clear();
    }
    clear() { this.items = []; this.cursor = 0; this.units = 0; }
    record(before, after, selectionBefore, selectionAfter) {
      if (before === after) return true;
      if (display(before) !== display(after)) { this.clear(); return false; }
      if (this.items.length && this.current() !== before) this.clear();
      this.items.splice(this.cursor);
      this.units = this.items.reduce((n, item) => n + item.before.length + item.after.length, 0);
      const cost = before.length + after.length;
      if (cost > this.budget || this.limit < 1) { this.clear(); return false; }
      this.items.push({ before, after, selectionBefore: [...selectionBefore], selectionAfter: [...selectionAfter] });
      this.units += cost;
      while (this.items.length > this.limit || this.units > this.budget) {
        const old = this.items.shift(); this.units -= old.before.length + old.after.length;
      }
      this.cursor = this.items.length;
      return true;
    }
    current() {
      return this.cursor ? this.items[this.cursor - 1].after : this.items[0]?.before;
    }
    step(raw, redo = false) {
      if (!this.items.length) return null;
      if (raw !== this.current()) { this.clear(); return null; }
      const item = this.items[redo ? this.cursor : this.cursor - 1];
      if (!item) return null;
      this.cursor += redo ? 1 : -1;
      return { raw: redo ? item.after : item.before,
        selection: [...(redo ? item.selectionAfter : item.selectionBefore)] };
    }
  }
  window.SourceText = Object.freeze({
    EolHistory,
    display,
    edits,
    reconcile,
    slice,
    replaceAll,
  });
})();
