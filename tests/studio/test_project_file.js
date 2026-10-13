// Versioned, portable source snapshot contract; real compiler execution of valid
// restored source only. No arbitrary image execution or external resource reads.
const fs = require("fs"),
  vm = require("vm"),
  assert = require("assert"),
  path = require("path"),
  cp = require("child_process");
global.window = {};
for (const f of ["docs.js", "lang.js", "project.js"])
  vm.runInThisContext(
    fs.readFileSync(path.join(__dirname, "../../tools/studio", f), "utf8"),
  );
const P = window.SourceProject,
  L = window.Lang;
let checks = 0;
const check = (ok, why) => {
  assert(ok, why);
  checks++;
};
const documents = [
  {
    id: "browser-random-one",
    name: "/lib/meter.cs",
    text: "class Meter { public int Read() { return 42; } }\n",
  },
  {
    id: "browser-random-two",
    name: "main.cs",
    text: "Console.WriteLine(new Meter().Read());\n",
  },
];
const text = P.stringify("portable β project", documents, documents[1].id),
  snapshot = P.parse(text);
check(
  snapshot.format === "microcs-source-project" && snapshot.version === 1,
  "explicit supported format/version",
);
check(
  snapshot.files[0].name === "lib/meter.cs" &&
    snapshot.files[0].id === "source-1" &&
    snapshot.entry === "source-2",
  "relative labels and canonical stable IDs, not browser/device identities",
);
check(
  snapshot.files.every((f, i) => f.text === documents[i].text),
  "source contents unchanged",
);
check(
  P.stringify(snapshot.name, snapshot.files, snapshot.entry) === text,
  "stable serialization",
);
check(P.parse("\uFEFF" + text).entry === snapshot.entry, "UTF8 BOM form");
for (const payload of [
  "// EOF comment",
  "class C { /* unfinished",
  'var text=$"{incomplete',
  'var payload=@"one\r\nβ 🚀\rthree\n";',
  '\\"\u0001\t\r\n\ud800',
]) {
  const d = [{ id: "draft", name: "draft.cs", text: payload }];
  const restored = P.parse(P.stringify("draft", d, "draft"));
  check(
    restored.files[0].text === payload,
    "draft/exact Unicode/control/line ending roundtrip",
  );
}
const unfinished = P.parse(
  P.stringify("draft", [{ id: "x", name: "x.cs", text: "class C {" }], "x"),
);
assert.throws(
  () => L.projectBundle(unfinished.files, unfinished.entry),
  /unmatched/,
);
checks++;
const copied = P.validate(snapshot);
copied.files[0].text = "changed";
check(snapshot.files[0].text === documents[0].text, "defensive result copy");
const repeated = [
  { id: "one", name: "same.cs", text: "class A {}" },
  { id: "two", name: "same.cs", text: "class B {}" },
];
check(
  P.parse(P.stringify("distinct tabs", repeated, "two")).files.length === 2,
  "duplicate labels preserve distinct explicit member IDs",
);
const invalid = [
  null,
  [],
  {},
  { ...snapshot, version: 2 },
  { ...snapshot, format: "csproj" },
  { ...snapshot, unknown: true },
  { ...snapshot, name: "" },
  { ...snapshot, name: "x".repeat(129) },
  { ...snapshot, name: "control\n" },
  { ...snapshot, entry: "missing" },
  { ...snapshot, files: [] },
  {
    ...snapshot,
    files: Array.from({ length: 33 }, (_, i) => ({
      id: "id" + i,
      name: i + ".cs",
      text: "",
    })),
  },
  {
    ...snapshot,
    files: [
      { id: "same", name: "x.cs", text: "" },
      { id: "same", name: "y.cs", text: "" },
    ],
  },
  { ...snapshot, files: [{ id: "", name: "x.cs", text: "" }] },
  { ...snapshot, files: [{ id: "x".repeat(65), name: "x.cs", text: "" }] },
  { ...snapshot, files: [{ id: "one", name: "x.cs", text: null }] },
  { ...snapshot, files: [{ id: "one", name: "x.cs", text: "\0" }] },
  {
    ...snapshot,
    files: [{ id: "one", name: "x.cs", text: " ".repeat(262145) }],
  },
  {
    ...snapshot,
    files: [
      {
        id: "one",
        name: "x.cs",
        text: "",
        url: "https://example.invalid/source.cs",
      },
    ],
  },
];
for (const value of invalid) {
  assert.throws(() => P.parse(JSON.stringify(value)));
  checks++;
}
for (const name of [
  "/root.cs",
  "../escape.cs",
  "dir/../escape.cs",
  "dir//x.cs",
  "dir/./x.cs",
  "C:\\x.cs",
  "https://example.invalid/x.cs",
  "remote:m.cs",
  "not-source.mcsb",
  "control\nx.cs",
  "x".repeat(254) + ".cs",
]) {
  assert.throws(() =>
    P.parse(
      JSON.stringify({
        ...snapshot,
        entry: "one",
        files: [{ id: "one", name, text: "" }],
      }),
    ),
  );
  checks++;
}
for (const input of [
  null,
  42,
  "{ invalid",
  "[]",
  " ".repeat(P.MAX_BYTES + 1),
  "β".repeat(P.MAX_BYTES / 2 + 1),
]) {
  assert.throws(() => P.parse(input));
  checks++;
}
assert.throws(() => P.stringify("missing", documents, "not-member"));
checks++;
const maximum = [{ id: "x", name: "x.cs", text: "\u0001".repeat(262144) }];
check(
  Buffer.byteLength(P.stringify("max escaped source", maximum, "x")) <
    P.MAX_BYTES,
  "largest allowed escaped source fits serialized byte budget",
);
const out = path.resolve("build/studio-project-file");
fs.mkdirSync(out, { recursive: true });
const source = path.join(out, "restored.cs"),
  image = path.join(out, "restored.mcsb"),
  mcs = path.resolve(process.argv[2] || "./mcs");
fs.writeFileSync(source, L.projectBundle(snapshot.files, snapshot.entry).text);
check(
  cp.execFileSync(mcs, [source], { encoding: "utf8", timeout: 10000 }) ===
    "42\n",
  "actual restored project source execution",
);
for (const args of [[], ["-O0"]]) {
  cp.execFileSync(mcs, [...args, "-c", source, "-o", image], {
    timeout: 10000,
  });
  for (const mode of [[], ["--xip"]])
    check(
      cp.execFileSync(mcs, [...mode, image], {
        encoding: "utf8",
        timeout: 10000,
      }) === "42\n",
      "actual restored optimized/noopt copy/XIP execution",
    );
}
console.log(
  `PASS portable project files: ${checks} checks; drafts preserved, no implicit execution/import/SDK/signing claim`,
);
