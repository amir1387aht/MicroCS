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
    // Split only syntactic commas; literal/comment payloads retain exact defaults.
    const masked = /["']|\/[*\/]/.test(s) ? blank(s, true) : s;
    const out = []; let depth = 0, start = 0;
    for (let i = 0; i < masked.length; i++) {
      const ch = masked[i];
      if ("<([{".includes(ch)) depth++;
      else if (">)]}".includes(ch)) depth--;
      if (ch === "," && depth === 0) { out.push(s.slice(start, i)); start = i + 1; }
    }
    out.push(s.slice(start));
    return out.map((p) => {
      p = p.trim();
      const m = /^([\s\S]*?)\s*=\s*([\s\S]+)$/.exec(p); let def = null;
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
  /* One bounded, offset-preserving lexical model for all editor consumers.
   * Ranges cover non-code; interpolation expressions remain code. Cache only
   * the current document, so serial handling never waits for repeated scans. */
  let lexSource = null, lexCache = null;
  function lexical(src) {
    if (src === lexSource) return lexCache;
    const ranges = [], strings = [], holes = [];
    const lineEnd = /[\r\n]/g;
    let eof = "code";
    const mark = (start, end, kind) => {
      if (end <= start) return null;
      const range = { start, end, kind }; ranges.push(range); return range;
    };
    const prefixAt = i => /^(\$@"|@\$"|@"|\$"|")/.exec(src.slice(i, i + 3));
    function comment(i) {
      const line = src[i + 1] === "/";
      let j;
      if (line) {
        lineEnd.lastIndex = i + 2;
        const ending = lineEnd.exec(src); j = ending ? ending.index : -1;
      } else j = src.indexOf("*/", i + 2);
      const end = j < 0 ? src.length : j + (line ? 0 : 2);
      const range = mark(i, end, "comment");
      if (range) range.lineComment = line;
      if (j < 0) eof = "comment";
      return end;
    }
    function quoted(i, level) {
      const m = prefixAt(i);
      if (!m) return i;
      const start = i, verbatim = m[0].includes("@"), interp = m[0].includes("$");
      let chunk = i, closed = false;
      i += m[0].length;
      while (i < src.length) {
        const c = src[i];
        if (!verbatim && (c === "\n" || c === "\r")) break;
        if (!verbatim && c === "\\") { i += Math.min(2, src.length - i); continue; }
        if (c === '"') {
          if (verbatim && src[i + 1] === '"') { i += 2; continue; }
          i++; closed = true; break;
        }
        if (interp && c === "{") {
          if (src[i + 1] === "{") { i += 2; continue; }
          mark(chunk, i, "string");
          if (level >= 32) { mark(i, src.length, "string"); eof = "string"; i = src.length; chunk = i; break; }
          const h = hole(i, level + 1);
          i = h.end; chunk = i;
          if (!h.closed) { strings.push({ start, end: i, closed }); return i; }
          continue;
        }
        if (interp && c === "}" && src[i + 1] === "}") { i += 2; continue; }
        i++;
      }
      const tail = mark(chunk, i, "string");
      if (tail) tail.unterminated = !closed;
      if (i === src.length) eof = closed ? "code" : "string";
      strings.push({ start, end: i, closed });
      return i;
    }
    function character(i) {
      const start = i++;
      let closed = false;
      while (i < src.length && src[i] !== "\n" && src[i] !== "\r") {
        if (src[i] === "\\") { i += Math.min(2, src.length - i); continue; }
        if (src[i++] === "'") { closed = true; break; }
      }
      const range = mark(start, i, "char"); if (range) range.unterminated = !closed;
      if (i === src.length && !closed) eof = "char";
      return i;
    }
    function hole(start, level) {
      mark(start, start + 1, "delimiter");
      let i = start + 1, braces = 0, parens = 0, brackets = 0, ternary = 0, formatStart = null;
      eof = "code";
      while (i < src.length) {
        const c = src[i];
        if (formatStart !== null) {
          if (c === "}") break;
          i++; continue;
        }
        if (src.startsWith("//", i) || src.startsWith("/*", i)) { i = comment(i); continue; }
        const end = quoted(i, level);
        if (end > i) { i = end; continue; }
        if (c === "'") { i = character(i); continue; }
        if (c === "{") braces++;
        else if (c === "}") { if (!braces) break; braces--; }
        else if (c === "(") parens++;
        else if (c === ")") parens = Math.max(0, parens - 1);
        else if (c === "[") brackets++;
        else if (c === "]") brackets = Math.max(0, brackets - 1);
        else if (!braces && !parens && !brackets) {
          if (c === "?" && !/[?.\[]/.test(src[i + 1] || " ") && src[i - 1] !== "?") ternary++;
          else if (c === ":" && src[i + 1] !== ":" && src[i - 1] !== ":") {
            if (ternary) ternary--; else formatStart = i;
          }
        }
        i++;
      }
      const closed = i < src.length;
      if (formatStart !== null) mark(formatStart, i, "format");
      if (closed) { mark(i, i + 1, "delimiter"); i++; eof = "string"; }
      else if (formatStart !== null) eof = "format";
      const result = { start, end: i, closed, formatStart };
      holes.push(result);
      return result;
    }
    for (let i = 0; i < src.length;) {
      if (src.startsWith("//", i) || src.startsWith("/*", i)) { i = comment(i); continue; }
      const end = quoted(i, 0);
      if (end > i) { i = end; continue; }
      if (src[i] === "'") { i = character(i); continue; }
      i++; eof = "code";
    }
    ranges.sort((a, b) => a.start - b.start);
    strings.sort((a, b) => a.start - b.start);
    holes.sort((a, b) => a.start - b.start);
    lexSource = src;
    lexCache = { ranges, strings, holes, eof };
    return lexCache;
  }
  function contextAt(src, p) {
    p = Math.max(0, Math.min(src.length, p));
    const lex = lexical(src);
    if (p === src.length) return lex.eof;
    // Context describes an insertion point, not the character to its right.
    // An auto-paired outer quote is lexed as the start of a nested string while
    // a hole is unfinished: the caret BEFORE that quote is still expression code.
    let lo = 0, hi = lex.strings.length;
    while (lo < hi) { const mid = (lo + hi) >>> 1; if (lex.strings[mid].start < p) lo = mid + 1; else hi = mid; }
    if (lex.strings[lo]?.start === p) return "code";
    // Binary search keeps moving the caret independent of the document size.
    lo = 0; hi = lex.ranges.length;
    while (lo < hi) { const mid = (lo + hi) >>> 1; if (lex.ranges[mid].end <= p) lo = mid + 1; else hi = mid; }
    const r = lex.ranges[lo], previous = lex.ranges[lo - 1];
    if ((src[p] === "\n" || src[p] === "\r") && previous?.end === p &&
        (previous.lineComment || previous.unterminated)) return previous.kind;
    return r && r.start <= p ? (r.kind === "delimiter" ? "code" : r.kind) : "code";
  }
  /* Same UTF-16 length and line endings; expressions inside strings stay visible
   * to symbols, call context and hover. Formatter opts out of expression holes. */
  function blank(src, wholeStrings = false) {
    const lex = lexical(src), chars = src.split("");
    const mask = (a, b) => { for (let i = a; i < b; i++) if (chars[i] !== "\n" && chars[i] !== "\r") chars[i] = " "; };
    for (const r of lex.ranges) if (r.kind !== "delimiter") mask(r.start, r.end);
    if (wholeStrings) for (const r of lex.strings) mask(r.start, r.end);
    return chars.join("");
  }
  function matchBrace(b, open) {
    let d = 0;
    for (let i = open; i < b.length; i++) { if (b[i] === "{") d++; else if (b[i] === "}" && --d === 0) return i; }
    return b.length;
  }
  const TYPE_RE = String.raw`(?:[A-Za-z_][\w.]*(?:\s*<[^<>;(){}=]*(?:<[^<>;(){}=]*>[^<>;(){}=]*)*>)?(?:\[[,\s]*\])*\??|\([^()]*\))`;
  let cacheSrc = null, cache = null;
  /* Cached lexical declaration intervals, not a promise of a full C# parser.
   * Strings/comments use the same offset-preserving lexer; incomplete delimiters
   * keep ancestor scopes live to EOF. Unknown inference stays unknown. */
  function scopeModel(src, b, user) {
    const structural = blank(src, true), pairs = new Map(), stack = [];
    const scopes = [{ start: 0, end: src.length, open: -1, parent: null }];
    const brace = [scopes[0]];
    for (let i = 0; i < structural.length; i++) {
      const ch = structural[i];
      if ('([{'.includes(ch)) {
        stack.push({ ch, at: i });
        if (ch === '{') {
          const s = { start: i + 1, end: src.length, open: i, parent: brace[brace.length - 1] };
          scopes.push(s); brace.push(s);
        }
      } else if (')]}'.includes(ch)) {
        const expected = { ')': '(', ']': '[', '}': '{' }[ch];
        if (stack.length && stack[stack.length - 1].ch === expected) {
          const open = stack.pop(); pairs.set(open.at, i);
        }
        if (ch === '}' && brace.length > 1) brace.pop().end = i;
      }
    }
    const containing = p => {
      let lo = 0, hi = scopes.length;
      while (lo < hi) { const mid = (lo + hi) >>> 1; if (scopes[mid].start <= p) lo = mid + 1; else hi = mid; }
      let found = scopes[Math.max(0, lo - 1)];
      while (found.parent && p > found.end) found = found.parent;
      return found;
    };
    const skip = p => { while (p < structural.length && /\s/.test(structural[p])) p++; return p; };
    const statementEnd = (p, depth = 0) => {
      p = skip(p);
      if (depth > 32) return src.length;
      if (structural[p] === '{') return pairs.get(p) ?? src.length;
      const control = /^(?:if|for|foreach|while|using|lock)\b/.exec(structural.slice(p));
      if (control) {
        const open = skip(p + control[0].length);
        if (structural[open] === '(' && pairs.has(open)) {
          const end = statementEnd(pairs.get(open) + 1, depth + 1);
          const next = skip(end + 1);
          return /^else\b/.test(structural.slice(next)) ? statementEnd(next + 4, depth + 1) : end;
        }
      }
      for (let i = p; i < structural.length; i++) {
        if (structural[i] === ';') return i;
        if (structural[i] === '}') return i - 1;
        if ('([{'.includes(structural[i]) && pairs.has(i)) i = pairs.get(i);
      }
      return src.length;
    };
    const callable = [], controls = [], declarations = [];
    const forbidden = /^(return|new|else|case|throw|in|is|as|await|yield|using|static|readonly|const|public|private|protected|internal|out|ref|params|this|namespace|class|struct|record|interface|enum|goto)$/;
    const reservedName = name => KEYW.has(name) && !/^(get|set|value|when|where|init)$/.test(name);
    const validType = t => !forbidden.test(t) && (!KEYW.has(t) || /^(void|int|bool|string|double|float|long|byte|char|object|short|uint|ushort|ulong|sbyte|var)$/.test(t));
    const registerCallable = (open, close, tail, paramText) => {
      tail = skip(tail);
      if (structural[tail] !== '{' && structural.slice(tail, tail + 2) !== '=>') return;
      const body = structural[tail] === '{' ? tail : skip(tail + 2);
      const region = { start: open, end: statementEnd(body), open, close };
      callable.push(region);
      const params = parseParams(paramText);
      for (const p of params) if (p.name && /^[A-Za-z_]\w*$/.test(p.name)) {
        const relative = blank(paramText, true).search(new RegExp(String.raw`\b${esc(p.name)}\b(?=\s*(?:=|,|$))`));
        declarations.push({ name: p.name, t: p.type ? p.type.replace(/^(?:(?:ref|out|in|params|this)\s+)+/, "") : null, at: open + 1 + Math.max(0, relative),
          visible: open, end: region.end, kind: 'parameter' });
      }
    };
    const functionRe = new RegExp(String.raw`\b(${TYPE_RE})\s+([A-Za-z_]\w*)\s*\(`, 'g');
    let m;
    while ((m = functionRe.exec(structural))) {
      if (!validType(m[1]) || reservedName(m[2])) continue;
      const open = functionRe.lastIndex - 1, close = pairs.get(open);
      if (close != null) registerCallable(open, close, close + 1, src.slice(open + 1, close));
    }
    for (const c of Object.values(user)) {
      const re = new RegExp(String.raw`\b${esc(c.name)}\s*\(`, 'g');
      while ((m = re.exec(structural))) {
        const open = re.lastIndex - 1, close = pairs.get(open);
        if (close == null) continue;
        let tail = skip(close + 1);
        const base = /^:\s*(?:base|this)\s*\(/.exec(structural.slice(tail));
        if (base) { const call = tail + base[0].length - 1; tail = (pairs.get(call) ?? src.length) + 1; }
        registerCallable(open, close, tail, src.slice(open + 1, close));
      }
    }
    const controlRe = /\b(for|foreach|catch|using)\s*\(/g;
    while ((m = controlRe.exec(structural))) {
      const open = controlRe.lastIndex - 1, close = pairs.get(open);
      if (close != null) {
        let body = skip(close + 1);
        const filter = /^when\s*\(/.exec(structural.slice(body));
        if (m[1] === 'catch' && filter) {
          const filterOpen = body + filter[0].length - 1;
          body = (pairs.get(filterOpen) ?? src.length) + 1;
        }
        controls.push({ start: open, close, end: statementEnd(body), kind: m[1] });
      }
    }
    // Typed parenthesized lambda parameters; implicit ones retain unknown type.
    const lambdaRe = /\(([^()]*)\)\s*=>/g;
    while ((m = lambdaRe.exec(structural))) {
      const open = m.index, close = pairs.get(open);
      if (close == null) continue;
      const text = src.slice(open + 1, close), tail = skip(lambdaRe.lastIndex);
      const params = text.split(',').map(x => x.trim()).filter(Boolean);
      const region = { start: open, close, end: statementEnd(tail) };
      callable.push(region);
      for (const text of params) {
        const parsed = /^[A-Za-z_]\w*$/.test(text) ? { name: text, type: null } : parseParams(text)[0];
        if (parsed && /^[A-Za-z_]\w*$/.test(parsed.name)) declarations.push({ name: parsed.name,
          t: parsed.type, at: open + 1 + Math.max(0, blank(src.slice(open + 1, close), true).search(new RegExp(String.raw`\b${esc(parsed.name)}\b(?=\s*(?:=|,|$))`))), visible: open, end: region.end, kind: 'parameter' });
      }
    }
    const bareLambda = /(?:[=(,:\[])\s*([A-Za-z_]\w*)\s*=>/g;
    while ((m = bareLambda.exec(structural))) {
      const at = m.index + m[0].indexOf(m[1]);
      declarations.push({ name: m[1], t: null, at, visible: at,
        end: statementEnd(bareLambda.lastIndex), kind: 'parameter' });
    }
    const re = new RegExp(String.raw`(?:^|[^\w.])(${TYPE_RE})\s+([A-Za-z_]\w*)\s*(?=[=;,)]|\bin\b)`, 'g');
    while ((m = re.exec(b))) {
      if (!validType(m[1]) || reservedName(m[2])) continue;
      const at = m.index + m[0].lastIndexOf(m[2]);
      if (callable.some(r => r.start < at && at < r.close)) continue;
      const owner = containing(at);
      if (Object.values(user).some(c => c.open >= 0 && c.open === owner.open)) continue; // class fields aren't locals
      let end = owner.end, kind = 'local', collection = null;
      const control = controls.filter(r => r.start < at && at < r.close).pop();
      if (control) {
        end = control.end;
        if (control.kind === 'foreach') {
          const inMatch = /\bin\b/.exec(b.slice(re.lastIndex, control.close));
          if (inMatch) { kind = 'each'; collection = src.slice(re.lastIndex + inMatch.index + 2, control.close).trim(); }
        }
      }
      declarations.push({ name: m[2], t: m[1], at, visible: at, end, kind, collection,
        initializerAt: re.lastIndex, initializerEnd: statementEnd(re.lastIndex) });
    }
    declarations.sort((a, b) => a.at - b.at);
    return { scopes, declarations, containing,
      visible: p => declarations.filter(d => d.visible <= p && p <= d.end) };
  }

  function documentSymbols(src) {
    if (src === cacheSrc) return cache;
    const b = blank(src), strings = lexical(src).strings;
    const user = Object.create(null), funcs = Object.create(null), enums = Object.create(null);
    /* classes / structs / records / interfaces */
    const cre = /\b(class|struct|record|interface)\s+([A-Za-z_]\w*)(?:\s*<[^>{]*>)?(?:\s*\(([^)]*)\))?(?:\s*:\s*([\w<>,.\s]+?))?\s*(?=\{|;)/g;
    let m;
    while ((m = cre.exec(b))) {
      const name = m[2], open = b.indexOf("{", m.index + m[0].length - 1);
      const end = open >= 0 && b[m.index + m[0].length] !== ";" ? matchBrace(b, open) : m.index + m[0].length;
      const c = user[name] = { name, kind: m[1], ctors: [], s: Object.create(null), i: Object.create(null), base: m[4] ? m[4].split(",").map((x) => x.trim().replace(/<.*/, "")) : [], start: m.index, end, open: open >= 0 && open < end ? open : -1, doc: `${m[1]} ${name} (this file)`, at: m.index + m[1].length + /^\s*/.exec(m[0].slice(m[1].length))[0].length };
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
            // Restore literal defaults into the masked declaration without
            // restoring comments or changing offsets used for scope parsing.
            const absolute = open + 1 + stmtStart, chars = stmt.split("");
            for (const r of strings) if (r.start < open + 1 + i && r.end > absolute) {
              for (let at = Math.max(r.start, absolute); at < Math.min(r.end, open + 1 + i); at++) chars[at - absolute] = src[at];
            }
            const before = new Set([...c.ctors, ...Object.values(c.s).flat(), ...Object.values(c.i).flat()]);
            memberDecl(c, chars.join("").trim(), name);
            for (const member of [...c.ctors, ...Object.values(c.s).flat(), ...Object.values(c.i).flat()]) if (!before.has(member)) {
              const re = new RegExp(String.raw`\b${esc(member.name)}\b(?=\s*${member.kind === "m" || member.kind === "ctor" ? "\\(" : "(?:,|$)"})`, "g");
              let hit, last = null; while ((hit = re.exec(stmt))) last = hit;
              if (last) member.at = absolute + last.index;
            }
            stmt = "";
            if (ch === "=") { while (i < body.length && body[i] !== ";" && body[i] !== "{") i++; if (body[i] === "{") { const e = matchBrace(body, i); i = e; } }
            else if (ch === "{") { depth = 1; }
            continue;
          }
          if (ch === "}") { stmt = ""; continue; }
          if (!stmt) stmtStart = i;
          stmt += ch;
        } else {
          if (ch === "{") depth++; else if (ch === "}") { depth--; if (depth === 0) stmt = ""; }
        }
      }
    }
    /* enums */
    const ere = /\benum\s+([A-Za-z_]\w*)\s*(?::\s*\w+\s*)?\{([^}]*)\}/g;
    while ((m = ere.exec(b))) enums[m[1]] = m[2].split(",").map((x) => x.trim().replace(/\s*=.*$/, "")).filter((x) => /^[A-Za-z_]\w*$/.test(x));
    const enumLocations = Object.create(null);
    ere.lastIndex = 0;
    while ((m = ere.exec(b))) enumLocations[m[1]] = m.index + /^enum\s+/.exec(m[0])[0].length;
    const scope = scopeModel(src, b, user);
    /* top-level / local functions: Type Name(params) { or => */
    const fre = new RegExp(String.raw`(?:^|[;{}\n])\s*((?:(?:static|public|private|internal|protected|async|unsafe|override|virtual|abstract|extern|new)\s+)*)(${TYPE_RE})\s+([A-Za-z_]\w*)\s*\(([^()]*(?:\([^()]*\)[^()]*)*)\)\s*(?=\{|=>)`, "g");
    while ((m = fre.exec(b))) {
      const type = m[2], name = m[3];
      if (KEYW.has(type) && !/^(void|int|bool|string|double|float|long|byte|char|object|short|uint|var)$/.test(type) || KEYW.has(name)) continue;
      if (/^(if|while|for|foreach|switch|catch|using|lock|return|new|else)$/.test(type)) continue;
      const owner = scope.containing(fre.lastIndex - 1);
      if (Object.values(user).some(c => c.open >= 0 && c.open === owner.open)) continue;
      const paramsAt = m.index + m[0].lastIndexOf("(" + m[4] + ")") + 1;
      (funcs[name] || (funcs[name] = [])).push({ name, kind: "m", params: parseParams(src.slice(paramsAt, paramsAt + m[4].length)), ret: type, doc: "local function (this file)", cls: "", at: m.index + b.slice(m.index, paramsAt - 1).search(new RegExp(String.raw`\b${esc(name)}\b\s*$`)), scopeStart: owner.start, scopeEnd: owner.end });
    }
    cacheSrc = src; cache = { user, funcs, enums, enumLocations, b, scope };
    return cache;
  }
  /* Explicit session projects: independently cached documents, no concatenated
   * lexical model and no implicit import of unrelated open tabs. */
  let project = null;
  const projectDocuments = new Map();
  function setProjectDocuments(documents, activeId) {
    if (!documents) { project = null; projectDocuments.clear(); return; }
    if (project && project.activeId === activeId && project.documents.length === documents?.length && project.documents.every((d, i) => d.id === documents[i]?.id && d.name === documents[i]?.name && d.text === documents[i]?.text)) return;
    validateDocuments(documents);
    const live = new Set(documents.map(d => d.id));
    for (const id of projectDocuments.keys()) if (!live.has(id)) projectDocuments.delete(id);
    for (const d of documents) {
      const old = projectDocuments.get(d.id);
      if (!old || old.text !== d.text) projectDocuments.set(d.id, { text: d.text, sy: documentSymbols(d.text) });
    }
    if (!live.has(activeId)) { project = null; return; }
    project = { documents: documents.map(d => ({ id: d.id, name: d.name, text: d.text })), activeId, merged: null };
  }
  function validateDocuments(documents) {
    if (!Array.isArray(documents) || !documents.length || documents.length > 32) throw new Error("A project needs 1–32 source files.");
    const ids = new Set(); let chars = 0;
    for (const d of documents) {
      if (!d || typeof d.id !== "string" || ids.has(d.id) || typeof d.text !== "string" || typeof d.name !== "string" || !/\.cs$/i.test(d.name)) throw new Error("Project source IDs must be unique and every member must be a C# text file.");
      ids.add(d.id); chars += d.text.length;
      if (chars > 262144 || d.text.includes("\0")) throw new Error("Project text exceeds 256 Ki UTF-16 units or contains a NUL.");
    }
  }
  function symbols(src) {
    const current = project && projectDocuments.get(project.activeId);
    if (!current || current.text !== src) return documentSymbols(src);
    if (project.merged) return project.merged;
    const own = current.sy, user = Object.create(null), enums = Object.create(null), funcs = Object.create(null);
    const candidates = new Map(), enumCandidates = new Map(), externalEnumLocations = Object.create(null);
    for (const d of project.documents) if (d.id !== project.activeId) {
      const sy = projectDocuments.get(d.id).sy;
      for (const [name, cls] of Object.entries(sy.user)) {
        const all = candidates.get(name) || []; all.push({ ...cls, external: true, documentId: d.id, doc: `${cls.kind} ${name} (project: ${d.name})` }); candidates.set(name, all);
      }
      for (const [name, values] of Object.entries(sy.enums)) {
        const all = enumCandidates.get(name) || []; all.push({ values, at: sy.enumLocations[name], documentId: d.id }); enumCandidates.set(name, all);
      }
      for (const [name, values] of Object.entries(sy.funcs)) {
        const exported = values.filter(f => f.scopeStart === 0 && f.scopeEnd === d.text.length).map(f => ({ ...f, external: true, documentId: d.id, doc: `project function (${d.name})`, scopeStart: 0, scopeEnd: src.length }));
        if (exported.length) (funcs[name] || (funcs[name] = [])).push(...exported);
      }
    }
    for (const [name, values] of candidates) if (values.length === 1) user[name] = values[0];
    for (const [name, values] of enumCandidates) if (values.length === 1) {
      enums[name] = values[0].values; externalEnumLocations[name] = values[0];
    }
    Object.assign(user, own.user); Object.assign(enums, own.enums);
    for (const [name, values] of Object.entries(own.funcs)) funcs[name] = [...values, ...(funcs[name] || [])];
    return project.merged = { ...own, user, enums, funcs, externalEnumLocations };
  }
  function sourcePosition(src, offset) {
    const before = src.slice(0, offset), lines = before.split(/\r\n|\r|\n/);
    return { line: lines.length, column: lines[lines.length - 1].length + 1 };
  }
  function definitionsAt(src, pos) {
    if (contextAt(src, pos) !== "code") return [];
    let a = pos, end = pos;
    while (a > 0 && /\w/.test(src[a - 1])) a--;
    while (end < src.length && /\w/.test(src[end])) end++;
    const name = src.slice(a, end); if (!/^[A-Za-z_]\w*$/.test(name)) return [];
    const activeId = project?.activeId || null, sy = symbols(src);
    const locations = (items, defaultId = activeId) => items.filter(x => Number.isInteger(x.at)).map(x => {
      const id = x.documentId || (x.cls && classInfo(x.cls, src)?.documentId) || defaultId;
      const text = id && projectDocuments.get(id)?.text || src;
      return { documentId: id, name, ...sourcePosition(text, x.at), offset: x.at };
    });
    let dot = a; while (dot > 0 && /\s/.test(src[dot - 1])) dot--;
    if (src[dot - 1] === ".") {
      const recv = receiverBefore(src, dot - 1), type = typeOfExpr(recv, src, dot - 1);
      if (!type) return [];
      const stat = isClassName(recv, src) && !varType(recv, src, dot - 1);
      const cls = classInfo(type, src), entries = membersOf(type, stat, src)[name] || [];
      return locations(entries, cls?.documentId || activeId);
    }
    const local = sy.scope.visible(a).filter(d => d.name === name).pop();
    if (local) return locations([local]);
    for (const c of Object.values(sy.user)) if (!c.external && a > c.start && a < c.end && (c.i[name] || c.s[name])) return locations(c.i[name] || c.s[name]);
    const functions = functionsAt(src, a)[name]; if (functions) return locations(functions);
    if (sy.user[name]) return locations([sy.user[name]]);
    if (Number.isInteger(sy.enumLocations[name])) return locations([{ at: sy.enumLocations[name] }]);
    if (sy.externalEnumLocations?.[name]) return locations([sy.externalEnumLocations[name]]);
    return [];
  }
  function projectBundle(documents, entryId) {
    validateDocuments(documents);
    const entry = documents.find(d => d.id === entryId); if (!entry) throw new Error("The project entry file is missing.");
    const ordered = [...documents.filter(d => d.id !== entryId), entry];
    let text = "", line = 1; const files = [];
    for (const d of ordered) {
      const model = lexical(d.text);
      if (model.strings.some(r => !r.closed) || model.holes.some(r => !r.closed) || model.ranges.some(r => r.kind === "char" && r.unterminated || r.kind === "comment" && !r.lineComment && !d.text.slice(r.start, r.end).endsWith("*/"))) throw new Error(`${d.name}: incomplete literal or block comment.`);
      const stack = [], pairs = { ")": "(", "]": "[", "}": "{" };
      for (const c of blank(d.text)) {
        if ("([{".includes(c)) stack.push(c);
        else if (pairs[c] && stack.pop() !== pairs[c]) throw new Error(`${d.name}: unmatched delimiter.`);
      }
      if (stack.length) throw new Error(`${d.name}: unmatched delimiter.`);
      const count = (d.text.match(/\n/g) || []).length;
      files.push({ id: d.id, name: d.name, text: d.text, firstLine: line, lastLine: line + count });
      text += d.text + "\n"; line += count + 1;
    }
    const bytes = new TextEncoder().encode(text).length;
    if (bytes > 524288) throw new Error("The bundled project exceeds 512 KiB UTF-8.");
    return { text, files, bytes };
  }
  function projectLocation(bundle, line) {
    if (!Number.isInteger(line) || line < 1) return null;
    const file = bundle?.files.find(f => line >= f.firstLine && line <= f.lastLine);
    return file ? { documentId: file.id, name: file.name, line: line - file.firstLine + 1, text: file.text } : null;
  }

  function memberDecl(c, d, clsName) {
    d = d.trim();
    // Attributes can precede a declaration; [] inside types/default strings
    // are not attributes. Preserve all literal whitespace in displayed defaults.
    while (/^\[[^\]]*\]\s*/.test(d)) d = d.replace(/^\[[^\]]*\]\s*/, "");
    const quoted = lexical(d).strings.slice().sort((a, b) => a.start - b.start);
    let normalized = "", at = 0;
    for (const r of quoted) if (r.start >= at) {
      normalized += d.slice(at, r.start).replace(/\s+/g, " ") + d.slice(r.start, r.end);
      at = r.end;
    }
    d = (normalized + d.slice(at).replace(/\s+/g, " ")).trim();
    if (!d) return;
    const isStatic = /\b(static|const)\b/.test(d);
    const mods = /^((?:(?:static|public|private|internal|protected|readonly|const|virtual|override|abstract|async|sealed|new|extern|unsafe|volatile|event)\s+)*)/.exec(d)[1];
    const rest = d.slice(mods.length);
    let m;
    if ((m = new RegExp(String.raw`^(${clsName})\s*\(([\s\S]*)\)\s*(?::\s*(?:base|this)\s*\(.*\))?$`).exec(rest))) {
      c.ctors.push({ name: clsName, kind: "ctor", params: parseParams(m[2]), ret: clsName, doc: "", cls: clsName }); return;
    }
    if ((m = new RegExp(String.raw`^(${TYPE_RE})\s+([A-Za-z_]\w*)\s*(?:<[^>]*>)?\s*\(([\s\S]*)\)(?:\s*where .*)?$`).exec(rest))) {
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

  function functionsAt(src, pos) {
    const all = symbols(src).funcs, out = Object.create(null);
    for (const name of Object.keys(all)) {
      const visible = all[name].filter(f => f.scopeStart <= pos && pos <= f.scopeEnd);
      if (visible.length) out[name] = visible;
    }
    return out;
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
    let best = null;
    for (const d of sy.scope.visible(upto)) if (d.name === name) best = d;
    if (!best) {
      /* fields of the enclosing class */
      for (const c of Object.values(sy.user).sort((a, b) => b.start - a.start)) if (!c.external && upto > c.start && upto < c.end && (c.i[name] || c.s[name])) return (c.i[name] || c.s[name])[0].ret;
      return null;
    }
    if (best.kind === "each") {
      if (best.t !== "var") return best.t;
      const coll = typeOfExpr(best.collection, src, best.at, depth + 1);
      return coll ? elemType(coll) : null;
    }
    if (best.t !== "var") return best.t || null;
    const after = src.slice(best.initializerAt, best.initializerEnd);
    const em = /^\s*=\s*([\s\S]*)$/.exec(after);
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
          const f = functionsAt(src, pos == null ? src.length : pos)[p.name]; if (!f) return null;
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
    const funcs = functionsAt(src, ctx.open);
    if (funcs[ctx.name]) return funcs[ctx.name];
    for (const c of Object.values(sy.user)) if (!c.external && ctx.open > c.start && ctx.open < c.end && (c.i[ctx.name] || c.s[ctx.name])) return (c.i[ctx.name] || c.s[ctx.name]).filter((x) => x.kind === "m");
    return [];
  }
  /* identifiers declared in the file before p, with their types (for completion) */
  function locals(src, p) {
    const sy = symbols(src), out = new Map();
    for (const d of sy.scope.visible(p)) out.set(d.name, d.t === "var" ? null : d.t);
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

  parseDocs(window.MCS_DOCS_SRC);

  window.Lang = { setProjectDocuments, definitionsAt, projectBundle, projectLocation, classes, NAMESPACES, parseDocs, splitType, elemType, bindRet, lexical, contextAt, blank, symbols, functionsAt, classInfo, isClassName, membersOf, ctorsOf,
    varType, typeOfExpr, splitChain, receiverBefore, callContext, signaturesFor, locals, signature, fmtParam, KEYW };
})();
