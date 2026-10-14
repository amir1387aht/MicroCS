const assert = require("assert"),
  fs = require("fs"),
  path = require("path");
const window = {};
new Function(
  "window",
  fs.readFileSync(path.join(__dirname, "../../tools/studio/source.js"), "utf8"),
)(window);
const S = window.SourceText;
let checks = 0;
function equal(a, b, label) {
  assert.deepStrictEqual(a, b, label);
  checks++;
}
const raw = "a\r\nβ\r🚀\nz";
equal(S.display(raw), "a\nβ\n🚀\nz", "LF view, UTF16 columns retained");
equal(
  S.slice(raw, 2, 7),
  "β\r🚀\n",
  "normalized offsets address raw separators and surrogate units",
);
equal(S.reconcile(raw, S.display(raw)), raw, "no edit is not an EOL rewrite");
equal(
  S.reconcile(raw, "A\nβ\n🚀\nz"),
  "A\r\nβ\r🚀\nz",
  "ordinary character edit preserves all separators",
);
equal(
  S.edits(raw, [{ a: 2, b: 3, text: "two" }]),
  "a\r\ntwo\r🚀\nz",
  "bounded edit preserves unrelated raw payload",
);
equal(
  S.edits(raw, [{ a: 2, b: 2, text: "new\n" }]),
  "a\r\nnew\r\nβ\r🚀\nz",
  "new structural newline uses first existing EOL",
);
equal(
  S.edits(raw, [{ a: 2, b: 2, text: "new\n" }], true),
  "a\r\nnew\nβ\r🚀\nz",
  "exact paste can introduce independent newline style",
);
equal(
  S.edits(raw, [{ a: 0, b: 8, text: " A\n B\n C\n D" }]),
  " A\r\n B\r C\n D",
  "same line count preserves mixed separators",
);
for (const changes of [
  [{ a: -1, b: 1, text: "" }],
  [{ a: 2, b: 1, text: "" }],
  [{ a: 0, b: 99, text: "" }],
  [
    { a: 2, b: 4, text: "" },
    { a: 1, b: 2, text: "" },
  ],
]) {
  assert.throws(() => S.edits(raw, changes));
  checks++;
}
const text = "x α\r\nx β\rx γ\n";
equal(
  S.replaceAll(text, /x/g, "y\n", true),
  "y\r\n α\r\ny\r\n β\ry\r\n γ\n",
  "replace all adds lines without rewriting existing separators",
);
equal(
  S.replaceAll(text, /x/g, "$&", true),
  text.replace(/x/g, "$$&"),
  "literal replacement does not expand dollars",
);
for (const re of [
  /x/g,
  /(x)( (α|β|γ))/g,
  /(?<letter>x) (?<value>.)/g,
  /(x)(z)?/g,
  /(?=x)/g,
  /^x/gm,
])
  for (const rep of [
    "$$",
    "$&",
    "$`",
    "$'",
    "$1-$2-$3",
    "$10-$01-$00-$99",
    "$<letter>:$<value>",
    "z\n",
  ]) {
    const result = S.replaceAll(text, re, rep, false);
    equal(
      S.display(result),
      S.display(text).replace(re, rep),
      "native JS substitution semantics " + re + " " + rep,
    );
  }
for (const newline of ["\n", "\r\n", "\r"])
  for (const prefix of ['@"', '$@"', '@$"']) {
    const input = [
      "if (true)",
      "{",
      "var text = " + prefix + "first  ",
      "فارسی 🚀  ",
      'end";',
      "Console.WriteLine(text);",
      "}",
    ].join(newline);
    const view = S.display(input),
      offset = view.indexOf("Console");
    const changed = S.edits(input, [
      { a: offset, b: offset + 7, text: "Terminal" },
    ]);
    equal(
      changed,
      input.replace("Console", "Terminal"),
      "multiline payload preserved " + prefix + JSON.stringify(newline),
    );
    equal(
      S.reconcile(input, S.display(input) + " // trailing"),
      input + " // trailing",
      "native input retains payload",
    );
  }
// The explicit byte-only history never guesses from an LF view.
const H = S.EolHistory;
const states = ["x\r\nβ\r🚀\n", "x\nβ\n🚀\n", "x\rβ\r🚀\r"];
const h = new H();
equal(h.record(states[0], states[1], [0, 9], [9, 9]), true, "record EOL-only");
equal(h.record(states[1], states[2], [1, 3], [3, 3]), true, "same view chain");
equal(h.step(states[2]), {raw: states[1], selection: [1, 3]}, "undo exact LF");
equal(h.step(states[1]), {raw: states[0], selection: [0, 9]}, "undo exact mixed EOL");
equal(h.step(states[0]), null, "old boundary cannot guess");
equal(h.step(states[0], true), {raw: states[1], selection: [9, 9]}, "redo exact selection");
equal(h.record(states[1], states[1], [0, 0], [0, 0]), true, "no-op keeps redo");
equal(h.step(states[1], true).raw, states[2], "redo survives no-op");
h.step(states[2]);
h.record(states[1], states[0], [0, 0], [0, 0]);
equal(h.step(states[0], true), null, "branch removes abandoned redo");
equal(h.step("foreign"), null, "foreign raw invalidates even at same display");
equal(h.items.length, 0, "invalid history cleared");
h.record(states[0], states[1], [0, 0], [0, 0]);
equal(h.record(states[1], "text changed", [0, 0], [0, 0]), false, "text edit is not EOL history");
equal(h.items.length, 0, "text edit invalidates");
const boundedHistory = new H(2, 100);
for (const [a, b] of [["x\n", "x\r"], ["x\r", "x\r\n"], ["x\r\n", "x\n"]])
  boundedHistory.record(a, b, [0, 0], [0, 0]);
equal(boundedHistory.items.length, 2, "count eviction");
equal(boundedHistory.units, 10, "UTF16 units account before and after");
equal(boundedHistory.step("x\n").raw, "x\r\n", "recent entry retained");
equal(boundedHistory.step("x\r\n").raw, "x\r", "oldest retained boundary");
equal(boundedHistory.step("x\r"), null, "evicted boundary not reconstructed");
const units = new H(128, 10);
units.record("🚀\r\n", "🚀\n", [0, 0], [0, 0]);
units.record("🚀\n", "🚀\r", [0, 0], [0, 0]);
equal(units.items.length, 1, "surrogate units trigger budget eviction");
equal(units.units, 6, "surrogates count as two UTF16 units");
equal(units.record("🚀\r".repeat(3), "🚀\n".repeat(3), [0, 0], [0, 0]), false, "oversized fallback explicitly unrecorded");
equal(units.units, 0, "oversized edit clears unavailable chain");
const defaults = new H();
equal([defaults.limit, defaults.budget], [128, 2 * 1024 * 1024], "documented default bounds");
let current = "x\n";
for (let i = 0; i < 129; i++) {
  const next = current === "x\n" ? "x\r" : "x\n";
  defaults.record(current, next, [0, 0], [0, 0]); current = next;
}
equal(defaults.items.length, 128, "default 129th entry evicts only oldest entry");
for (let i = 0; i < 128; i++) current = defaults.step(current).raw;
equal(defaults.step(current), null, "default count stops safely at retained boundary");
const big = "🚀".repeat(262144) + "\r\n";
equal(new H().record(big, S.display(big), [0, 0], [0, 0]), true, "large entry below default budget retained");
const huge = big + big;
const oversized = new H();
equal(oversized.record(huge, S.display(huge), [0, 0], [0, 0]), false, "default UTF16 budget rejects oversized entry");
equal(oversized.step(S.display(huge)), null, "default oversized fallback unavailable");
console.log(
  `PASS ${checks} canonical source/display/edit/substitution assertions`,
);
