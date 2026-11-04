import { readFile, writeFile } from "node:fs/promises";
const packages = [
  ["esptool-js", "LICENSE"],
  ["spark-md5", "LICENSE"],
  ["pako", "LICENSE"],
  ["atob-lite", "LICENSE.md"],
];
const notices = await Promise.all(
  packages.map(
    async ([name, file]) =>
      `${name}\n${"=".repeat(name.length)}\n${await readFile(new URL(`node_modules/${name}/${file}`, import.meta.url), "utf8")}`,
  ),
);
await writeFile(
  new URL("THIRD_PARTY_LICENSES.txt", import.meta.url),
  notices.join("\n\n"),
);
