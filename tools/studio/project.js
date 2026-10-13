/* Portable source snapshots only: no filesystem reads, network fetches, imports
 * or execution. Source text is plaintext; validation is not authentication. */
(() => {
  "use strict";
  const FORMAT = "microcs-source-project",
    VERSION = 1,
    MAX_BYTES = 2 * 1024 * 1024;
  const ownKeys = (obj, keys, where) => {
    if (
      !obj ||
      typeof obj !== "object" ||
      Array.isArray(obj) ||
      Object.keys(obj).length !== keys.length ||
      keys.some((k) => !Object.prototype.hasOwnProperty.call(obj, k))
    )
      throw new Error(`${where}: unsupported or missing fields.`);
  };
  function name(value) {
    if (
      typeof value !== "string" ||
      !value.trim() ||
      value.length > 128 ||
      /[\x00-\x1f\x7f]/.test(value)
    )
      throw new Error(
        "Project name must be 1–128 characters without control characters.",
      );
    return value;
  }
  function label(value) {
    if (
      typeof value !== "string" ||
      !value ||
      value.length > 256 ||
      /[\\:\x00-\x1f\x7f]/.test(value) ||
      !/\.cs$/i.test(value) ||
      value.split("/").some((s) => !s || s === "." || s === "..")
    )
      throw new Error(
        "Source labels must be relative .cs names, at most 256 characters, without traversal or device/URL paths.",
      );
    return value;
  }
  function validate(value) {
    ownKeys(value, ["format", "version", "name", "entry", "files"], "Project");
    if (value.format !== FORMAT || value.version !== VERSION)
      throw new Error(
        "Not a supported MicroCS source project (version 1 required).",
      );
    name(value.name);
    if (
      !Array.isArray(value.files) ||
      value.files.length < 1 ||
      value.files.length > 32
    )
      throw new Error("A source project needs 1–32 files.");
    for (const file of value.files) {
      ownKeys(file, ["id", "name", "text"], "Source file");
      if (typeof file.id !== "string" || !/^[A-Za-z0-9_-]{1,64}$/.test(file.id))
        throw new Error(
          "Source IDs must be nonempty, bounded letters/digits/_/-.",
        );
      label(file.name);
    }
    // Structural source limits are shared with the editor. Incomplete source is
    // deliberately savable; only Run project requires complete lexical bodies.
    window.Lang.validateProjectDocuments(value.files);
    if (
      typeof value.entry !== "string" ||
      !value.files.some((f) => f.id === value.entry)
    )
      throw new Error("The project entry must identify a member source file.");
    return {
      format: FORMAT,
      version: VERSION,
      name: value.name,
      entry: value.entry,
      files: value.files.map((f) => ({ id: f.id, name: f.name, text: f.text })),
    };
  }
  function parse(text) {
    if (
      typeof text !== "string" ||
      text.length > MAX_BYTES ||
      new TextEncoder().encode(text).length > MAX_BYTES
    )
      throw new Error("Project file exceeds 2 MiB or is not text.");
    let value;
    try {
      value = JSON.parse(text.replace(/^\uFEFF/, ""));
    } catch {
      throw new Error("Project file is not valid JSON.");
    }
    return validate(value);
  }
  function stringify(projectName, documents, entryId) {
    window.Lang.validateProjectDocuments(documents);
    const ids = new Map(documents.map((d, i) => [d.id, `source-${i + 1}`]));
    const snapshot = validate({
      format: FORMAT,
      version: VERSION,
      name: name(projectName),
      entry: ids.get(entryId),
      files: documents.map((d) => ({
        id: ids.get(d.id),
        name: label(d.name.replace(/^\/+/, "")),
        text: d.text,
      })),
    });
    const text = JSON.stringify(snapshot, null, 2) + "\n";
    if (new TextEncoder().encode(text).length > MAX_BYTES)
      throw new Error("Serialized project exceeds 2 MiB.");
    return text;
  }
  window.SourceProject = Object.freeze({
    parse,
    stringify,
    validate,
    MAX_BYTES,
  });
})();
