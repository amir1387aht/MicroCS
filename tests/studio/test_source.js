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
console.log(
  `PASS ${checks} canonical source/display/edit/substitution assertions`,
);
