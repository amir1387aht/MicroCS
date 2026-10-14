// Pure model plus real trusted compiler source/copy/XIP examples when ./mcs is supplied.
const assert = require("assert"),
  fs = require("fs"),
  path = require("path");
const { spawnSync } = require("child_process");
const { SNIPPETS, SnippetText: S } = new Function(
  fs.readFileSync(
    path.join(__dirname, "../../tools/studio/snippets.js"),
    "utf8",
  ) + "\nreturn { SNIPPETS, SnippetText };",
)();
let checks = 0;
const equal = (a, b, why) => {
  assert.deepStrictEqual(a, b, why);
  checks++;
};
const rejected = (text, why) => {
  assert.throws(() => S.parse(text), undefined, why);
  checks++;
};
const labels =
  "wifi wifievents httpget httppost json tcpclient tcpserver udp httpserver route wsserver wsclient captive wps espnow ntp u8g2 u8frame u8page u8x8 gpiocfg i2ccfg spicfg uartcfg dmabuf spiasync cw cwi for forr foreach while loop do if ife switch try tryf tryio class ctor prop propg interface enum func lambda pin pinin onchange gpioirq every after timer i2cdev i2cscan spidev uart uartrx adc pwm servo sw wdt readfile append ls has plot pulse regs bcd clamp map usingb cmd".split(
    " ",
  );
equal(
  SNIPPETS.map((s) => s.label),
  labels,
  "all legacy 77 labels/order retained",
);
for (const snippet of SNIPPETS) {
  const parsed = S.parse(snippet.text);
  equal(
    /\$\{|\$0/.test(parsed.text),
    false,
    "no markers emitted: " + snippet.label,
  );
  for (const field of parsed.fields)
    equal(
      parsed.text.slice(field.a, field.b).length,
      field.b - field.a,
      "field offsets: " + snippet.label,
    );
}
equal(
  S.parse('Console.WriteLine($"{value}"); $price @$"quote" $0').text,
  'Console.WriteLine($"{value}"); $price @$"quote" ',
  "ordinary dollars and C# interpolation remain source",
);
equal(S.parse("a$0b").final, 1, "legacy final caret");
equal(S.parse("no marker").final, 9, "legacy implicit end");
let parsed = S.parse("${3:first}\r\n${1:β🚀}\r${2:}\n$0", "  ");
equal(
  parsed.text,
  "first\n  β🚀\n  \n  ",
  "normalized view/indent without raw-offset assumptions",
);
equal(
  parsed.fields.map((f) => [f.id, f.a, f.b]),
  [
    [1, 8, 11],
    [2, 14, 14],
    [3, 0, 5],
  ],
  "numeric order independent of textual positions/UTF16",
);
equal(
  S.parse("${1:a\\}b\\\\c}$0").text,
  "a}b\\c",
  "escaped closing brace and backslash",
);
equal(
  S.parse('${1:$"value {42\\}"}$0').text,
  '$"value {42}"',
  "C# interpolation inside a default uses an escaped closing brace",
);
for (const text of [
  "${0:x}$0",
  "${01:x}$0",
  "${33:x}$0",
  "${1:x}${1:y}$0",
  "${1:x}",
  "$0$0",
  "${1:x}$00",
  "${1:x",
  "${x:y}$0",
  "${1:x${2:y}}$0",
  "${1:$0}$0",
  "$1",
  "$12",
  "x".repeat(S.MAX_TEMPLATE + 1),
])
  rejected(text, "deterministic unsupported grammar: " + text.slice(0, 30));
assert.throws(() => S.parse("x", " ".repeat(257)));
checks++;
assert.throws(() => S.parse("x", "not whitespace"));
checks++;
assert.throws(() => S.parse("\n".repeat(1000), " ".repeat(256)));
checks++;
parsed = S.parse("${2:}${1:}$0");
let session = new S.Session(parsed, 1, "[]");
equal(session.contains(1, 1), true, "empty field selected");
equal(session.update("[🚀]"), true, "edit active second textual empty field");
equal(
  session.fields.map((f) => [f.id, f.a, f.b]),
  [
    [1, 1, 3],
    [2, 1, 1],
  ],
  "prior adjacent empty field does not drift",
);
equal(session.final, 3, "final marker shifts after empty field");
equal(session.move().a, 1, "next numbered field before current text");
equal(session.update("[Q🚀]"), true, "insert into earlier empty field");
equal(
  session.fields.map((f) => [f.id, f.a, f.b]),
  [
    [1, 2, 4],
    [2, 1, 2],
  ],
  "later physical field shifts correctly",
);
equal(session.move(true), { done: false, a: 2, b: 4 }, "backward navigation");
equal(
  session.move(true),
  { done: false, a: 2, b: 4 },
  "backward first stays first",
);
session.move();
equal(
  session.move(),
  { done: true, a: 4, b: 4 },
  "final caret independent of numeric order",
);
parsed = S.parse("before ${1:aa} between ${2:tail} after$0");
session = new S.Session(parsed, 0, parsed.text);
equal(
  session.update(parsed.text.replace("aa", "a")),
  true,
  "repeated character deletion stays within actual field",
);
equal(session.field().b - session.field().a, 1, "field shrinks");
equal(
  session.update(session.view.replace("between", "changed")),
  false,
  "outside active source edit invalidates",
);
equal(
  session.contains(session.field().a - 1, session.field().b),
  false,
  "straddling selection invalidates",
);
equal(
  session.contains(session.field().a, session.field().b + 1),
  false,
  "trailing cross-boundary invalidates",
);
assert.throws(() => new S.Session(parsed, -1, parsed.text));
checks++;
assert.throws(() => new S.Session(parsed, 0, "wrong insertion"));
checks++;
assert.throws(
  () => new S.Session(parsed, 0, parsed.text + "x".repeat(S.MAX_DOCUMENT)),
);
checks++;
equal(
  session.update("x".repeat(S.MAX_DOCUMENT + 1)),
  false,
  "document growth budget invalidates safely",
);
const max = S.parse(
  Array.from({ length: 32 }, (_, i) => "${" + (i + 1) + ":}").join("") + "$0",
);
equal(max.fields.length, 32, "exact field bound accepted");
equal(
  S.parse("x".repeat(S.MAX_TEMPLATE)).text.length,
  S.MAX_TEMPLATE,
  "exact template bound accepted",
);
console.log(`PASS ${checks} snippet grammar/session/catalog assertions`);
if (process.argv[2]) {
  const mcs = path.resolve(process.argv[2]);
  const temp = fs.mkdtempSync(
    path.join(require("os").tmpdir(), "mcs-snippet-"),
  );
  const run = (args, expected) => {
    const result = spawnSync(mcs, args, { encoding: "utf8", timeout: 10000 });
    equal(result.status, 0, result.stderr || String(result.error));
    if (expected !== undefined)
      equal(
        result.stdout.trim(),
        expected,
        "actual runtime output " + args.join(" "),
      );
  };
  const expected = {
    for: "0\n1\n2",
    forr: "2\n1\n0",
    foreach: "1\n2\n3",
    while: "",
    if: "yes",
    ife: "yes",
    try: "try",
    tryf: "try\nfinally",
    func: "",
    class: "",
  };
  try {
    for (const [label, output] of Object.entries(expected)) {
      const snippet = SNIPPETS.find((s) => s.label === label),
        source = path.join(temp, label + ".cs");
      fs.writeFileSync(source, S.parse(snippet.text).text);
      run([source], output);
      const image = path.join(temp, label + ".mcsb");
      run(["-c", source, "-o", image]);
      run([image], output);
      run(["--xip", image], output);
    }
    const filled = S.parse(SNIPPETS.find((s) => s.label === "for").text);
    const fields = [...filled.fields].sort((a, b) => b.a - a.a);
    let text = filled.text;
    for (const f of fields)
      text =
        text.slice(0, f.a) +
        (f.id === 1 ? "2" : "Console.WriteLine(i + 10);") +
        text.slice(f.b);
    const file = path.join(temp, "filled.cs");
    fs.writeFileSync(file, text);
    run([file], "10\n11");
    const fn = S.parse(SNIPPETS.find((s) => s.label === "func").text);
    text = fn.text;
    for (const f of [...fn.fields].sort((a, b) => b.a - a.a))
      text =
        text.slice(0, f.a) +
        (f.id === 1 ? "Double" : "x * 2") +
        text.slice(f.b);
    const functionFile = path.join(temp, "filled-function.cs");
    fs.writeFileSync(functionFile, text + "Console.WriteLine(Double(21));\n");
    run([functionFile], "42");
    console.log(
      `PASS trusted default/filled snippets source/copy/XIP compiler executions (${checks} total assertions)`,
    );
  } finally {
    fs.rmSync(temp, { recursive: true, force: true });
  }
}
