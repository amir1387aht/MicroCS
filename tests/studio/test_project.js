// Actual pure language/project model; no mock completion engine or remote sources.
const fs = require("fs"),
  vm = require("vm"),
  assert = require("assert"),
  path = require("path"),
  cp = require("child_process");
global.window = {};
for (const f of ["docs.js", "lang.js"])
  vm.runInThisContext(fs.readFileSync(path.join(__dirname, "../../tools/studio", f), "utf8"));
const L = window.Lang;
let checks = 0;
const check = (x, msg) => {
  assert(x, msg);
  checks++;
};
const library =
  'namespace Example;\npublic class Meter { public int Value = 42; public int Read(string separator="a,b") { return Value; } }\nint Twice(int value) { return value*2; }\nenum Mode { First, Second }\n';
const main =
  "using Example;\nvar meter = new Meter();\nConsole.WriteLine(meter.Read());\nConsole.WriteLine(Twice(21));\n";
let docs = [
  { id: "lib", name: "lib.cs", text: library },
  { id: "main", name: "main.cs", text: main },
];
L.setProjectDocuments(docs, "main");
check(L.isClassName("Meter", main), "cross-file type");
check(
  L.membersOf("Meter", false, main).Read[0].params[0].def === '"a,b"',
  "cross-file member literal default",
);
check(L.varType("meter", main, main.length) === "Meter", "active inferred receiver");
check(
  L.locals(main, main.length).has("meter") && !L.locals(main, main.length).has("separator"),
  "foreign parameters do not leak",
);
check(L.functionsAt(main, main.length).Twice[0].documentId === "lib", "exported root function");
let target = L.definitionsAt(main, main.indexOf("Meter") + 2)[0];
check(
  target.documentId === "lib" && library.slice(target.offset, target.offset + 5) === "Meter",
  "cross-file class definition",
);
target = L.definitionsAt(main, main.indexOf("Read") + 2)[0];
check(
  target.documentId === "lib" && library.slice(target.offset, target.offset + 4) === "Read",
  "member definition",
);
target = L.definitionsAt(main, main.indexOf("Twice") + 2)[0];
check(
  target.documentId === "lib" && library.slice(target.offset, target.offset + 5) === "Twice",
  "function definition",
);
const enumMain = "Mode.";
L.setProjectDocuments([{ ...docs[0] }, { id: "main", name: "main.cs", text: enumMain }], "main");
check(L.membersOf("Mode", true, enumMain).Second, "cross-file enum members");
target = L.definitionsAt(enumMain, 2)[0];
check(
  target.documentId === "lib" && library.slice(target.offset, target.offset + 4) === "Mode",
  "enum definition",
);
const hidden =
  "void Outer() { void Secret() {} int hidden = 1; } class Other { int privateOffset = 1; }";
L.setProjectDocuments([{ id: "other", name: "other.cs", text: hidden }, docs[1]], "main");
check(!L.functionsAt(main, main.length).Secret, "foreign local function excluded");
check(
  !L.locals(main, main.length).has("hidden") && L.varType("privateOffset", main, 50) === null,
  "foreign scopes do not overlap active offsets",
);
const incomplete = 'var meter = new Meter(); var text=$"{meter.Read(';
L.setProjectDocuments([docs[0], { ...docs[1], text: incomplete }], "main");
check(
  L.signaturesFor(L.callContext(incomplete, incomplete.length), incomplete)[0].params[0].name ===
    "separator",
  "incomplete active interpolation does not mask foreign declarations",
);
const changed = library.replace('Read(string separator="a,b")', "Changed()");
L.setProjectDocuments([{ ...docs[0], text: changed }, docs[1]], "main");
check(
  !L.membersOf("Meter", false, main).Read && L.membersOf("Meter", false, main).Changed,
  "unsaved export edit invalidates cached members",
);
L.setProjectDocuments([docs[1]], "main");
check(!L.classInfo("Meter", main), "closed member exports removed");
L.setProjectDocuments(
  [
    docs[0],
    { id: "dup1", name: "duplicate.cs", text: "class Meter {}" },
    { id: "dup2", name: "duplicate2.cs", text: "class Meter {}" },
    docs[1],
  ],
  "main",
);
check(!L.classInfo("Meter", main), "ambiguous foreign classes not silently selected");
L.setProjectDocuments(null);
check(!L.classInfo("Meter", main), "project clear removes external types");
for (const source of [
  'int value=1; { string value="x"; value; }',
  "void Run(int argument) { argument; }",
  "var f=(int first, string second)=>second;",
  "class C { int Value; void Run(){ Value=1; } }",
  "class e {} var x=new e();",
  "enum e { One } e.One;",
  "Thing Result() { return null; } Result();",
]) {
  const name = source.includes("second")
    ? "second"
    : source.includes("argument")
      ? "argument"
      : source.includes("Value")
        ? "Value"
        : source.includes("Result")
          ? "Result"
          : source.includes("e ") || source.includes("e.")
            ? "e"
            : "value";
  let pos = source.lastIndexOf(name);
  if (name === "e")
    pos = source.includes("e.One") ? source.indexOf("e.One") : source.indexOf("e()");
  const t = L.definitionsAt(source, pos + Math.min(1, name.length - 1))[0];
  check(
    t && source.slice(t.offset, t.offset + name.length) === name,
    "accurate name offsets: " + source,
  );
}
const baseSource =
  "class Base { public int Value=42; public int Read() { return Value; } public int Read(int arg) { return arg; } }";
const derivedSource = "class Derived : Base {}";
const receiver = "var value=new Derived(); value.Read(); value.Value;";
L.setProjectDocuments(
  [
    { id: "base", name: "base.cs", text: baseSource },
    { id: "derived", name: "derived.cs", text: derivedSource },
    { id: "use", name: "use.cs", text: receiver },
  ],
  "use",
);
const inherited = L.definitionsAt(receiver, receiver.indexOf("Read") + 1);
check(
  inherited.length === 2 &&
    inherited.every(
      (x) => x.documentId === "base" && baseSource.slice(x.offset, x.offset + 4) === "Read",
    ),
  "inherited overload definitions retain source ownership",
);
const field = L.definitionsAt(receiver, receiver.indexOf("Value") + 1)[0];
check(
  field.documentId === "base" && baseSource.slice(field.offset, field.offset + 5) === "Value",
  "initialized inherited field definition",
);
check(
  L.definitionsAt("// Read()", 4).length === 0 && L.definitionsAt('"Read"', 2).length === 0,
  "comments and literals never navigate",
);
L.setProjectDocuments(null);
const literal = 'var payload=@"one\r\nβ two"; // no terminating newline';
const bundle = L.projectBundle(
  [
    { id: "entry", name: "main.cs", text: main },
    { id: "lib", name: "lib.cs", text: library },
    { id: "payload", name: "payload.cs", text: literal },
  ],
  "entry",
);
check(
  bundle.text === library + "\n" + literal + "\n" + main + "\n",
  "exact payload/line-ending preservation and entry-last ordering",
);
for (const f of bundle.files) {
  const t = L.projectLocation(bundle, f.firstLine);
  check(t.documentId === f.id && t.line === 1, "source start map");
  const end = L.projectLocation(bundle, f.lastLine);
  check(end.documentId === f.id, "source last-line map");
}
check(
  L.projectLocation(bundle, 0) === null && L.projectLocation(bundle, 99999) === null,
  "invalid source locations rejected",
);
for (const text of [
  "/* unfinished",
  'var s=@"unfinished',
  'var s=$"{value',
  'var s="unfinished\nNext();',
  "var c='x",
  "class C {",
  "Console.WriteLine(1]);",
])
  assert.throws(
    () => L.projectBundle([{ id: "x", name: "x.cs", text }], "x"),
    /incomplete|unmatched/,
  );
checks += 7;
for (const bad of [
  [
    { id: "x", name: "x.cs", text: "" },
    { id: "x", name: "y.cs", text: "" },
  ],
  [{ id: "x", name: "x.js", text: "" }],
  [{ id: "x", name: "x.cs", text: "\0" }],
  [{ id: "x", name: "x.cs", text: " ".repeat(262145) }],
  Array.from({ length: 33 }, (_, i) => ({ id: "" + i, name: i + ".cs", text: "" })),
])
  assert.throws(() => L.projectBundle(bad, "x"));
checks += 5;
assert.throws(() => L.projectBundle(docs, "missing"), /entry/);
checks++;
// Compile and execute the *actual* bundle, copied image and XIP on the real host.
const executable = path.resolve(process.argv[2] || "./mcs");
if (fs.existsSync(executable)) {
  const out = path.resolve("build/studio-project");
  fs.mkdirSync(out, { recursive: true });
  const source = path.join(out, "project.cs"),
    image = path.join(out, "project.mcsb");
  const actual = L.projectBundle(docs, "main");
  fs.writeFileSync(source, actual.text);
  assert.strictEqual(cp.execFileSync(executable, [source], { encoding: "utf8" }), "42\n42\n");
  checks++;
  for (const args of [[], ["-O0"]]) {
    cp.execFileSync(executable, [...args, "-c", source, "-o", image]);
    for (const mode of [[], ["--xip"]]) {
      assert.strictEqual(
        cp.execFileSync(executable, [...mode, image], { encoding: "utf8" }),
        "42\n42\n",
      );
      checks++;
    }
  }
} else throw new Error("Actual compiler executable required for project semantics");
console.log(
  `PASS explicit project symbols/navigation/bundling: ${checks} assertions; actual source/copy/XIP execution, no external import/SDK claim`,
);
