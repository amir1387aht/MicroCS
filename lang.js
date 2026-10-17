/* MicroCS Studio - C# language service: class/member signatures (docs.js), symbols
 * of the open file, simple type inference, call context for signature help.
 * Pure functions on the source text; studio.js draws the popups.   window.Lang */
(function () {
  "use strict";
  const ALIAS = { String: "string", Int32: "int", Int64: "int", long: "int", short: "int", byte: "int", sbyte: "int", uint: "int", ushort: "int", ulong: "int",
    Int16: "int", UInt32: "int", Byte: "int", float: "double", Single: "double", Double: "double", decimal: "double", Boolean: "bool", Char: "char", Object: "object",
    IList: "List", IEnumerable: "IEnumerable", ICollection: "List", IDictionary: "Dictionary", ISet: "HashSet", IReadOnlyList: "List", Span: "Array_i" };
  const NAMESPACES = ["System", "System.Collections", "System.Collections.Generic", "System.Diagnostics", "System.IO", "System.Linq", "System.Text",
    "System.Threading", "System.Threading.Tasks", "System.Globalization", "System.Buffers.Binary", "MicroCS", "MicroCS.Hal"];
  const KEYW = new Set(("abstract as base bool break byte case catch char checked class const continue decimal default delegate do double else enum event " +
    "explicit extern false finally fixed float for foreach goto if implicit in int interface internal is lock long namespace new null object operator out " +
    "override params private protected public readonly ref return sbyte sealed short sizeof stackalloc static string struct switch this throw true try " +
    "typeof uint ulong unchecked unsafe ushort using var virtual void volatile while get set value yield async await nameof when where record init").split(" "));

  /* ---------------------------------------------------------------- docs.js */
  const classes = Object.create(null);
  function parseParams(s) {
    s = s.trim(); if (!s) return [];
    const out = []; let depth = 0, cur = "";
    for (const ch of s) {
      if (ch === "<" || ch === "(") depth++; else if (ch === ">" || ch === ")") depth--;
      if (ch === "," && depth === 0) { out.push(cur); cur = ""; } else cur += ch;
    }
    out.push(cur);
    return out.map((p) => {
      p = p.trim();
      const m = /^(.*?)\s*=\s*(.+)$/.exec(p); let def = null;
      if (m) { p = m[1]; def = m[2]; }
      const k = p.lastIndexOf(" ");
      return { type: k > 0 ? p.slice(0, k).trim() : "object", name: k > 0 ? p.slice(k + 1) : p, def };
    });
  }
  function parseDocs(src) {
    let cls = null;
    for (let line of (src || "").split("\n")) {
      line = line.trim();
      if (!line) continue;
      if (line[0] === "@") {
        const m = /^@(\w+)(?:\s*\|\s*(.*?))?(?:\s*<\s*(\w+))?\s*$/.exec(line.replace(/\s*<\s*(\w+)\s*$/, " < $1"));
        const name = /^@(\w+)/.exec(line)[1];
        let doc = (line.split("|")[1] || "").trim(), base = null;
        const b = /\s<\s*(\w+)\s*$/.exec(doc); if (b) { base = b[1]; doc = doc.slice(0, b.index).trim(); }
        cls = classes[name] || (classes[name] = { name, doc, ctors: [], s: Object.create(null), i: Object.create(null), base: [] });
        cls.doc = doc || cls.doc; if (base) cls.base.push(base);
        void m;
        continue;
      }
      if (!cls) continue;
      const [sig, ...rest] = line.split(" | "); const doc = rest.join(" | ").trim();
      let s = sig.trim(), inst = false, kind = "m", isConst = false;
      if (s.startsWith("const ")) { isConst = true; s = s.slice(6); }
      if (s[0] === ".") { inst = true; s = s.slice(1); }
      let m;
      if ((m = /^new\((.*)\)$/.exec(s))) { cls.ctors.push({ name: cls.name, kind: "ctor", params: parseParams(m[1]), ret: cls.name, doc, cls: cls.name }); continue; }
      if ((m = /^(\w+)\((.*)\)\s*:\s*(.+)$/.exec(s))) kind = "m";
      else if ((m = /^(\w+)\s*:\s*(.+)$/.exec(s))) kind = isConst ? "c" : "p";
      else continue;
      const mem = kind === "m" ? { name: m[1], kind, params: parseParams(m[2]), ret: m[3].trim(), doc, cls: cls.name, inst }
                               : { name: m[1], kind, ret: m[2].trim(), doc, cls: cls.name, inst };
      const tbl = inst ? cls.i : cls.s;
      (tbl[mem.name] || (tbl[mem.name] = [])).push(mem);
    }
  }
  parseDocs(window.MCS_DOCS_SRC);

  /* ---------------------------------------------------------------- types */
  /* "List<int>" -> { base: "List", args: ["int"] }   "byte[]" -> { base: "Array_i", args: ["byte"] } */
  function splitType(t) {
    if (!t) return null;
    t = t.trim().replace(/\?$/, "");
    if (/\[[,\s]*\]$/.test(t)) return { base: "Array_i", args: [t.replace(/\[[,\s]*\]$/, "")], raw: t };
    const m = /^([\w.]+)\s*(?:<(.*)>)?$/.exec(t);
    if (!m) return { base: t, args: [], raw: t };
    let base = m[1].replace(/^System\.(?:Collections\.Generic\.|Text\.|IO\.)?/, "");
    base = ALIAS[base] || base;
    const args = m[2] ? parseParams(m[2].replace(/(\w)\s*(,|$)/g, "$1 _$2")).map((p) => p.type) : [];
    return { base, args, raw: t };
  }
  /* element type of a collection type string */
  function elemType(t) {
    const s = splitType(t); if (!s) return null;
    if (s.base === "Array_i" || s.base === "List" || s.base === "HashSet" || s.base === "Queue" || s.base === "Stack" || s.base === "IEnumerable") return s.args[0] || null;
    if (s.base === "Dictionary") return `KeyValuePair<${s.args.join(", ")}>`;
    if (s.base === "string") return "char";
    return null;
  }
  /* substitute T/K/V/R of a member's return type with the receiver's type arguments */
  function bindRet(ret, recv) {
    if (!ret) return null;
    const s = splitType(recv || ""); const map = {};
    if (s && s.args.length) {
      if (s.base === "Dictionary") { map.K = s.args[0]; map.V = s.args[1]; map.T = `KeyValuePair<${s.args.join(", ")}>`; }
      else map.T = s.args[0];
    }
    return ret.replace(/\b[TKV]\b/g, (x) => map[x] || x);
  }

  /* ---------------------------------------------------------------- user code */
  /* strip comments and string contents (same length, so offsets stay valid) */
  function blank(src) {
    return src.replace(/\/\/[^\n]*|\/\*[\s\S]*?(?:\*\/|$)|\$?@"(?:[^"]|"")*"?|\$?"(?:[^"\\\n]|\\.)*"?|'(?:[^'\\\n]|\\.)*'?/g,
      (m) => (m[0] === "/" ? m.replace(/[^\n]/g, " ") : m[0] + m.slice(1, -1).replace(/[^\n]/g, " ") + (m.length > 1 ? m[m.length - 1] : "")));
  }
  function matchBrace(b, open) {
    let d = 0;
    for (let i = open; i < b.length; i++) { if (b[i] === "{") d++; else if (b[i] === "}" && --d === 0) return i; }
    return b.length;
  }
  const TYPE_RE = String.raw`(?:[A-Za-z_][\w.]*(?:\s*<[^<>;(){}=]*(?:<[^<>;(){}=]*>[^<>;(){}=]*)*>)?(?:\[[,\s]*\])*\??|\([^()]*\))`;
  let cacheSrc = null, cache = null;
  function symbols(src) {
    if (src === cacheSrc) return cache;
    const b = blank(src);
    const user = Object.create(null), funcs = Object.create(null), enums = Object.create(null);
    /* classes / structs / records / interfaces */
    const cre = /\b(class|struct|record|interface)\s+([A-Za-z_]\w*)(?:\s*<[^>{]*>)?(?:\s*\(([^)]*)\))?(?:\s*:\s*([\w<>,.\s]+?))?\s*(?=\{|;)/g;
    let m;
    while ((m = cre.exec(b))) {
      const name = m[2], open = b.indexOf("{", m.index + m[0].length - 1);
      const end = open >= 0 && b[m.index + m[0].length] !== ";" ? matchBrace(b, open) : m.index + m[0].length;
      const c = user[name] = { name, kind: m[1], ctors: [], s: Object.create(null), i: Object.create(null), base: m[4] ? m[4].split(",").map((x) => x.trim().replace(/<.*/, "")) : [], start: m.index, end, doc: `${m[1]} ${name} (this file)` };
      if (m[3] != null) {                                  // primary constructor (record / class)
        const ps = parseParams(m[3]); c.ctors.push({ name, kind: "ctor", params: ps, ret: name, doc: "", cls: name });
        for (const p of ps) (c.i[p.name] = [{ name: p.name, kind: "p", ret: p.type, doc: "", cls: name, inst: true }]);
      }
      if (open < 0 || end <= open) continue;
      /* members at depth 1 */
      const body = b.slice(open + 1, end);
      let depth = 0, stmt = "", stmtStart = 0, par = 0;
      for (let i = 0; i < body.length; i++) {
        const ch = body[i];
        if (depth === 0) {
          if (ch === "(") par++; else if (ch === ")") par = Math.max(0, par - 1);
          if (par === 0 && (ch === "{" || ch === ";" || ch === "=" && body[i + 1] !== ">" && body[i + 1] !== "=" && !/[=!<>]/.test(body[i - 1]))) {
            memberDecl(c, stmt.trim(), name);
            stmt = "";
            if (ch === "=") { while (i < body.length && body[i] !== ";" && body[i] !== "{") i++; if (body[i] === "{") { const e = matchBrace(body, i); i = e; } }
            else if (ch === "{") { depth = 1; }
            continue;
          }
          if (ch === "}") { stmt = ""; continue; }
          stmt += ch;
        } else {
          if (ch === "{") depth++; else if (ch === "}") { depth--; if (depth === 0) stmt = ""; }
        }
      }
      void stmtStart;
    }
    /* enums */
    const ere = /\benum\s+([A-Za-z_]\w*)\s*(?::\s*\w+\s*)?\{([^}]*)\}/g;
    while ((m = ere.exec(b))) enums[m[1]] = m[2].split(",").map((x) => x.trim().replace(/\s*=.*$/, "")).filter((x) => /^[A-Za-z_]\w*$/.test(x));
    /* top-level / local functions: Type Name(params) { or => */
    const fre = new RegExp(String.raw`(?:^|[;{}\n])\s*((?:(?:static|public|private|internal|protected|async|unsafe|override|virtual|abstract|extern|new)\s+)*)(${TYPE_RE})\s+([A-Za-z_]\w*)\s*\(([^()]*(?:\([^()]*\)[^()]*)*)\)\s*(?=\{|=>)`, "g");
    while ((m = fre.exec(b))) {
      const type = m[2], name = m[3];
      if (KEYW.has(type) && !/^(void|int|bool|string|double|float|long|byte|char|object|short|uint|var)$/.test(type) || KEYW.has(name)) continue;
      if (/^(if|while|for|foreach|switch|catch|using|lock|return|new|else)$/.test(type)) continue;
      const inClass = Object.values(user).some((c) => m.index > c.start && m.index < c.end);
      if (inClass) continue;
      (funcs[name] || (funcs[name] = [])).push({ name, kind: "m", params: parseParams(m[4]), ret: type, doc: "local function (this file)", cls: "" });
    }
    cacheSrc = src; cache = { user, funcs, enums, b };
    return cache;
  }
  function memberDecl(c, d, clsName) {
    d = d.replace(/\[[^\]]*\]\s*/g, (x) => (/^\[\s*\w/.test(x) && /\]\s*$/.test(x) && !/\w\s*\[/.test(d) ? "" : x)).replace(/\s+/g, " ").trim();
    if (!d) return;
    const isStatic = /\b(static|const)\b/.test(d);
    const mods = /^((?:(?:static|public|private|internal|protected|readonly|const|virtual|override|abstract|async|sealed|new|extern|unsafe|volatile|event)\s+)*)/.exec(d)[1];
    const rest = d.slice(mods.length);
    let m;
    if ((m = new RegExp(String.raw`^(${clsName})\s*\((.*)\)\s*(?::\s*(?:base|this)\s*\(.*\))?$`).exec(rest))) {
      c.ctors.push({ name: clsName, kind: "ctor", params: parseParams(m[2]), ret: clsName, doc: "", cls: clsName }); return;
    }
    if ((m = new RegExp(String.raw`^(${TYPE_RE})\s+([A-Za-z_]\w*)\s*(?:<[^>]*>)?\s*\((.*)\)(?:\s*where .*)?$`).exec(rest))) {
      const mem = { name: m[2], kind: "m", params: parseParams(m[3]), ret: m[1], doc: `${isStatic ? "static " : ""}method of ${clsName}`, cls: clsName, inst: !isStatic };
      const t = isStatic ? c.s : c.i; (t[mem.name] || (t[mem.name] = [])).push(mem); return;
    }
    if ((m = new RegExp(String.raw`^(${TYPE_RE})\s+([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s*(?:=>.*)?$`).exec(rest))) {
      for (const nm of m[2].split(",").map((x) => x.trim())) {
        const mem = { name: nm, kind: /\bconst\b/.test(mods) ? "c" : "p", ret: m[1], doc: `${/\bconst\b/.test(mods) ? "const" : "field / property"} of ${clsName}`, cls: clsName, inst: !isStatic };
        const t = isStatic ? c.s : c.i; t[nm] = [mem];
      }
    }
  }

  /* ---------------------------------------------------------------- lookup */
  function classInfo(name, src) {
    if (!name) return null;
    const sy = src != null ? symbols(src) : null;
    if (sy && sy.user[name]) return sy.user[name];
    const s = splitType(name); const base = s ? s.base : name;
    if (sy && sy.user[base]) return sy.user[base];
    return classes[base] || null;
  }
  function isClassName(name, src) {
    const sy = src != null ? symbols(src) : null;
    return !!(classes[name] && name !== "Array_i" && name !== "IEnumerable" || window.MCS_API?.[name] && name !== "*" || sy && (sy.user[name] || sy.enums[name]));
  }
  /* members of a type: map name -> overloads; static or instance side */
  function membersOf(type, isStatic, src) {
    const out = Object.create(null), seen = new Set();
    const s = splitType(type); if (!s) return out;
    const sy = src != null ? symbols(src) : null;
    if (sy && sy.enums[s.base] && isStatic) { for (const v of sy.enums[s.base]) out[v] = [{ name: v, kind: "c", ret: s.base, doc: `enum ${s.base}`, cls: s.base }]; return out; }
    const walk = (name) => {
      if (seen.has(name)) return; seen.add(name);
      const c = (sy && sy.user[name]) || classes[ALIAS[name] || name];
      if (c) {
        const t = isStatic ? c.s : c.i;
        for (const k in t) if (!out[k]) out[k] = t[k];
        for (const b of c.base || []) walk(b);
      }
      /* names known to the runtime but without docs */
      const api = window.MCS_API?.[name === "Array_i" ? "*" : name];
      if (api) for (const n of (isStatic ? api.s : api.i) || []) if (!out[n]) out[n] = [{ name: n, kind: "m", params: null, ret: null, doc: "", cls: name, inst: !isStatic }];
    };
    walk(s.base);
    if (!isStatic && s.base !== "object") {
      if (/^(List|Array_i|Dictionary|HashSet|Queue|Stack)$/.test(s.base)) walk("IEnumerable");
      for (const n of ["ToString", "Equals", "GetHashCode", "GetType"]) if (!out[n]) out[n] = [{ name: n, kind: "m", params: n === "Equals" ? [{ type: "object", name: "obj" }] : [], ret: n === "ToString" ? "string" : n === "Equals" ? "bool" : n === "GetType" ? "Type" : "int", doc: "", cls: "object", inst: true }];
    }
    return out;
  }
  function ctorsOf(type, src) {
    const c = classInfo(type, src);
    if (c && c.ctors && c.ctors.length) return c.ctors;
    return [];
  }

  /* ---------------------------------------------------------------- inference */
  function esc(s) { return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"); }
  /* type of a variable / parameter / field named `name` (nearest declaration before pos) */
  function varType(name, src, pos, depth = 0) {
    if (depth > 4) return null;
    const sy = symbols(src), b = sy.b;
    const upto = pos == null ? b.length : pos;
    const n = esc(name);
    const res = [
      [new RegExp(String.raw`\bforeach\s*\(\s*(${TYPE_RE}|var)\s+${n}\s+in\s+([^)]*)\)`, "g"), "each"],
      [new RegExp(String.raw`\bout\s+(${TYPE_RE})\s+${n}\b`, "g"), "decl"],
      [new RegExp(String.raw`(?:^|[^\w.])(${TYPE_RE})\s+${n}\s*(?=[=;,)]|\bin\b)`, "g"), "decl"],
    ];
    let best = null;
    for (const [re, k] of res) {
      let m;
      while ((m = re.exec(b))) {
        if (m.index > upto) break;
        const t = m[1];
        if (/^(return|new|else|case|throw|in|is|as|await|yield|using|static|readonly|const|public|private|out|ref|params|this)$/.test(t)) continue;
        if (!best || m.index > best.at) best = { at: m.index, t, k, m };
      }
    }
    if (!best) {
      /* fields of the enclosing class */
      for (const c of Object.values(sy.user)) if (upto > c.start && upto < c.end && (c.i[name] || c.s[name])) return (c.i[name] || c.s[name])[0].ret;
      return null;
    }
    if (best.k === "each") {
      const coll = typeOfExpr(src.slice(best.m.index + best.m[0].indexOf(" in ") + 4, best.m.index + best.m[0].length - 1).trim(), src, best.at, depth + 1);
      if (best.t !== "var") return best.t;
      return coll ? elemType(coll) : null;
    }
    if (best.t !== "var") return best.t;
    /* var x = <expr>; */
    const after = src.slice(best.m.index + best.m[0].length);
    const em = /^\s*=\s*([^;]*)/.exec(after);
    return em ? typeOfExpr(em[1].trim(), src, best.at, depth + 1) : null;
  }
  /* type of an expression like `new List<int>()`, `I2C.Scan(0)`, `name.Trim().Split(',')`, `"x"`, `12` */
  function typeOfExpr(expr, src, pos, depth = 0) {
    if (!expr || depth > 5) return null;
    expr = expr.trim();
    let m;
    if ((m = new RegExp(String.raw`^new\s+(${TYPE_RE})\s*([({[])`).exec(expr))) return m[2] === "[" ? m[1].replace(/\[.*$/, "") + "[]" : m[1];
    if (/^\$?@?"/.test(expr)) return chainType(expr.replace(/^\$?@?"(?:[^"\\]|\\.)*"/, "S"), src, pos, depth, "string");
    if (/^'/.test(expr)) return "char";
    if (/^-?\d+(\.\d*)?([eE][-+]?\d+)?[fFdDmM]$|^-?\d*\.\d+/.test(expr)) return "double";
    if (/^-?(0x[\da-fA-F_]+|\d[\d_]*)[uUlL]*$/.test(expr)) return "int";
    if (/^(true|false)$/.test(expr)) return "bool";
    if ((m = /^\(\s*(int|double|float|long|byte|string|char|bool|short|uint)\s*\)/.exec(expr))) return ALIAS[m[1]] || m[1];
    if (/^\$?"|^string\.(Format|Join|Concat)/.test(expr)) return "string";
    return chainType(expr, src, pos, depth, null);
  }
  /* walk a.b(…).c[…] left to right */
  function chainType(expr, src, pos, depth, startType) {
    const parts = splitChain(expr);
    if (!parts) return null;
    let type = startType, isStatic = false;
    for (let i = 0; i < parts.length; i++) {
      const p = parts[i];
      if (i === 0 && !startType) {
        if (p.name === "S") return null;
        if (p.call) {                                    // local function call
          const f = symbols(src).funcs[p.name]; if (!f) return null;
          type = f[0].ret; isStatic = false;
        } else if (/^(string|int|double|bool|char|long|float|byte)$/.test(p.name)) { type = ALIAS[p.name] || p.name; isStatic = true; }
        else {
          const v = varType(p.name, src, pos, depth + 1);
          if (v) { type = v; isStatic = false; }
          else if (isClassName(p.name, src)) { type = p.name; isStatic = true; }
          else return null;
        }
      } else if (i === 0) { /* literal start */ }
      else {
        const mem = membersOf(type, isStatic, src)[p.name];
        if (!mem) {
          /* Encoding.UTF8 etc are static properties of instance type */
          return null;
        }
        const pick = mem.find((x) => x.ret) || mem[0];
        type = bindRet(pick.ret, type); isStatic = false;
        if (!type) return null;
      }
      for (let k = 0; k < p.index; k++) type = elemType(type) || (splitType(type)?.base === "Dictionary" ? splitType(type).args[1] : null);
      if (!type) return null;
    }
    return type && type !== "void" ? type : null;
  }
  /* "a.b(x, y).c[0]" -> [{name:a},{name:b,call},{name:c,index:1}] ; null if not a plain chain */
  function splitChain(expr) {
    const out = []; let i = 0;
    const s = expr.trim();
    while (i < s.length) {
      const m = /^\s*(@?[A-Za-z_]\w*)\s*(?:<[\w\s,<>\[\]]*>(?=\s*\())?/.exec(s.slice(i));
      if (!m) return out.length ? out : null;
      const part = { name: m[1].replace(/^@/, ""), call: false, index: 0 };
      i += m[0].length;
      for (;;) {
        const c = s[i];
        if (c === "(" || c === "[") {
          const close = c === "(" ? ")" : "]"; let d = 0, j = i;
          for (; j < s.length; j++) { if (s[j] === c) d++; else if (s[j] === close && --d === 0) break; }
          if (c === "(") part.call = true; else part.index++;
          i = j + 1;
        } else if (c === " ") i++;
        else break;
      }
      out.push(part);
      if (s[i] === "?" && s[i + 1] === ".") i++;
      if (s[i] === "!" ) i++;
      if (s[i] === ".") { i++; continue; }
      if (i < s.length) return null;
    }
    return out;
  }
  /* the expression in front of a "." at offset p (the receiver), e.g. "dev.Read(3)" */
  function receiverBefore(text, p) {
    let i = p - 1, depth = 0;
    for (; i >= 0; i--) {
      const c = text[i];
      if (c === ")" || c === "]") depth++;
      else if (c === "(" || c === "[") { if (depth === 0) break; depth--; }
      else if (depth === 0 && !/[\w.@?!\s]/.test(c) && !(c === '"' )) break;
      else if (depth === 0 && c === "\n") break;
    }
    let r = text.slice(i + 1, p).trim();
    r = r.replace(/^(?:return|await|new\s+\w+|case|throw|in|else|=)\s+/, "");
    return r.replace(/^.*\b(?:return|await|else|throw)\s+/, "");
  }

  /* ---------------------------------------------------------------- call context */
  /* innermost unclosed "(" before p (string/comment aware) with the argument index */
  function callContext(src, p) {
    const from = Math.max(0, p - 3000);
    const b = blank(src.slice(0, p)).slice(from);
    const stack = [];
    for (let i = 0; i < b.length; i++) {
      const c = b[i];
      if (c === "(" || c === "[" || c === "{") stack.push({ c, at: from + i, args: 0 });
      else if (c === ")" || c === "]" || c === "}") stack.pop();
      else if (c === "," && stack.length) stack[stack.length - 1].args++;
      else if (c === ";" && stack.length && stack[stack.length - 1].c === "{") { /* statement end */ }
    }
    for (let k = stack.length - 1; k >= 0; k--) {
      const t = stack[k];
      if (t.c === "{") return null;
      if (t.c === "(") {
        /* callee: [new] [receiver.]Name[<T>] ( */
        let e = t.at;
        while (e > 0 && /\s/.test(src[e - 1])) e--;
        if (src[e - 1] === ">") { let d = 0; for (let j = e - 1; j >= 0; j--) { if (src[j] === ">") d++; else if (src[j] === "<" && --d === 0) { e = j; break; } } }
        while (e > 0 && /\s/.test(src[e - 1])) e--;
        let s0 = e;
        while (s0 > 0 && /[\w@]/.test(src[s0 - 1])) s0--;
        const name0 = src.slice(s0, e);
        if (!/^@?[A-Za-z_]\w*$/.test(name0) || /^(if|while|for|foreach|switch|catch|using|lock|return|typeof|sizeof|nameof|when|default)$/.test(name0)) return null;
        let d0 = s0; while (d0 > 0 && /\s/.test(src[d0 - 1])) d0--;
        let recv = null, name = name0, isNew = false;
        if (src[d0 - 1] === "." ) recv = receiverBefore(src, d0 - 1) || null;
        else isNew = /\bnew\s*$/.test(src.slice(Math.max(0, d0 - 10), d0));
        const m = [null, isNew];
        return { isNew: !!m[1], name: name.replace(/^@/, ""), recv, open: t.at, arg: t.args };
      }
    }
    return null;
  }
  /* overloads for a call context */
  function signaturesFor(ctx, src) {
    if (!ctx) return [];
    if (ctx.isNew) return ctorsOf(ctx.name, src);
    if (ctx.recv) {
      const recvType = typeOfExpr(ctx.recv, src, ctx.open);
      const stat = !!(recvType && isClassName(ctx.recv, src) && !varType(ctx.recv, src, ctx.open));
      const t = recvType;
      const mem = t ? membersOf(t, stat, src)[ctx.name] : null;
      if (mem) return mem.filter((x) => x.kind === "m" && x.params).map((x) => ({ ...x, ret: bindRet(x.ret, t), params: x.params.map((p) => ({ ...p, type: bindRet(p.type, t) })) }));
      return [];
    }
    const sy = symbols(src);
    if (sy.funcs[ctx.name]) return sy.funcs[ctx.name];
    for (const c of Object.values(sy.user)) if (ctx.open > c.start && ctx.open < c.end && (c.i[ctx.name] || c.s[ctx.name])) return (c.i[ctx.name] || c.s[ctx.name]).filter((x) => x.kind === "m");
    return [];
  }
  /* identifiers declared in the file before p, with their types (for completion) */
  function locals(src, p) {
    const sy = symbols(src), b = sy.b.slice(0, p);
    const out = new Map();
    const re = new RegExp(String.raw`(?:^|[^\w.])(${TYPE_RE}|var)\s+([A-Za-z_]\w*)\s*(?=[=;,)]|\bin\b)`, "g");
    let m;
    while ((m = re.exec(b))) {
      if (KEYW.has(m[2]) || /^(return|new|else|case|throw|in|is|as|await|using|namespace|class|struct|enum|goto)$/.test(m[1])) continue;
      out.set(m[2], m[1] === "var" ? null : m[1]);
    }
    return out;
  }
  /* format a member as a signature string; active = parameter to bold (index) */
  function fmtParam(p) { return `${p.type} ${p.name}${p.def ? " = " + p.def : ""}`; }
  function signature(mem, owner) {
    if (!mem) return "";
    const own = owner || mem.cls || "";
    if (mem.kind === "ctor") return `new ${own}(${(mem.params || []).map(fmtParam).join(", ")})`;
    if (mem.kind === "m") return `${mem.ret ? mem.ret + " " : ""}${own && own !== "object" ? own.replace("Array_i", "T[]").replace("IEnumerable", "IEnumerable<T>") + "." : ""}${mem.name}(${mem.params ? mem.params.map(fmtParam).join(", ") : "…"})`;
    if (mem.kind === "c") return `const ${mem.ret} ${own}.${mem.name}`;
    return `${mem.ret || ""} ${own}.${mem.name} { get; }`.trim();
  }

  window.Lang = { classes, NAMESPACES, parseDocs, splitType, elemType, bindRet, blank, symbols, classInfo, isClassName, membersOf, ctorsOf,
    varType, typeOfExpr, splitChain, receiverBefore, callContext, signaturesFor, locals, signature, fmtParam, KEYW };
})();
