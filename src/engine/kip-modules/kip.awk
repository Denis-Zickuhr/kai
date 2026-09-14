# kip.awk: the whole `kip` helper of bash/sh commands in one POSIX awk program (build + get). The same
# messages as `kai kip` (src/core/kip-cli-builder.cpp; tests/test_kip_shell_helper.cpp compares them).
# No single quotes in this file (it lives in a single-quoted shell string) and table driven on purpose: it
# travels inside the command line, which Windows cuts at 8191 characters.
#   LC_ALL=C awk "$_KIP" <verb> <args...>   build: ONE line on stdout; usage error: message on stderr, exit 2
#   LC_ALL=C awk "$_KIP" get <json> <path>   exit 1 when the path is missing

function die(m) { printf "kip %s: %s\n", verb, m > "/dev/stderr"; exit 2 }
function esc(s,   o, i, n, c) {
  n = length(s)
  for (i = 1; i <= n; i++) {
    c = substr(s, i, 1)
    o = o (c == "\\" ? "\\\\" : c == "\"" ? "\\\"" : c == "\n" ? "\\n" : c == "\t" ? "\\t" : c == "\r" ? "\\r" : (c in OD) ? sprintf("\\u%04x", OD[c]) : c)
  }
  return o
}
function q(s) { return "\"" esc(s) "\"" }
function ad(o, k, v) { return o (o == "" ? "" : ",") "\"" k "\":" v }
function isn(t) { return t ~ /^[ \t]*[+-]?([0-9]+[.]?[0-9]*|[.][0-9]+)([eE][+-]?[0-9]+)?[ \t]*$/ }
function nm(t) { if (!isn(t)) die("not a number: " t); return sprintf("%.15g", t + 0) }
function cl(t, hi,   v) { v = nm(t) + 0; return int(v < 0 ? 0 : v > hi ? hi : v) }
function trim(t) { gsub(/^[ \t]+|[ \t]+$/, "", t); return t }
function csv(t,   n, p, i, s) {
  n = split(t, p, /[,\001]/)
  for (i = 1; i <= n; i++) if (trim(p[i]) != "") s = s (s == "" ? "" : ",") q(trim(p[i]))
  return "[" s "]"
}

# ---- JSON text scanner (--rows-json check, get) -----------------------------------------------------------
function ws(s, p) { while (substr(s, p, 1) ~ /[ \t\r\n]/) p++; return p }
function sks(s, p,   c) {
  for (p++; p <= length(s); p++) { c = substr(s, p, 1); if (c == "\\") p++; else if (c == "\"") return p + 1 }
  return 0
}
function skv(s, p,   c, d) {
  p = ws(s, p); c = substr(s, p, 1)
  if (c == "\"") return sks(s, p)
  if (c == "{" || c == "[") {
    for (; p <= length(s); p++) {
      c = substr(s, p, 1)
      if (c == "\"") { p = sks(s, p) - 1; if (p < 0) return 0 }
      else if (c == "{" || c == "[") d++
      else if ((c == "}" || c == "]") && !--d) return p + 1
    }
    return 0
  }
  return match(substr(s, p), /^(-?[0-9]+([.][0-9]+)?([eE][+-]?[0-9]+)?|true|false|null)/) ? p + RLENGTH : 0
}
function compact(s,   o, i, c, st) {
  for (i = 1; i <= length(s); i++) {
    c = substr(s, i, 1)
    if (st) { o = o c; if (c == "\\") o = o substr(s, ++i, 1); else if (c == "\"") st = 0 }
    else if (c == "\"") { st = 1; o = o c }
    else if (c !~ /[ \t\r\n]/) o = o c
  }
  return o
}
function hex(h,   i, n) { h = tolower(h); for (i = 1; i <= length(h); i++) n = n * 16 + index("0123456789abcdef", substr(h, i, 1)) - 1; return n }
function u8(c) {
  if (c < 128) return c ? sprintf("%c", c) : ""
  if (c < 2048) return sprintf("%c%c", 192 + int(c / 64), 128 + c % 64)
  if (c < 65536) return sprintf("%c%c%c", 224 + int(c / 4096), 128 + int(c / 64) % 64, 128 + c % 64)
  return sprintf("%c%c%c%c", 240 + int(c / 262144), 128 + int(c / 4096) % 64, 128 + int(c / 64) % 64, 128 + c % 64)
}
function unesc(s,   o, i, c, d, u, l) {
  for (i = 1; i <= length(s); i++) {
    c = substr(s, i, 1)
    if (c != "\\") { o = o c; continue }
    d = substr(s, ++i, 1)
    if (d == "u") {
      u = hex(substr(s, i + 1, 4)); i += 4
      if (u >= 55296 && u < 56320 && substr(s, i + 1, 2) == "\\u") { l = hex(substr(s, i + 3, 4)); if (l >= 56320 && l < 57344) { u = 65536 + (u - 55296) * 1024 + l - 56320; i += 6 } }
      o = o u8(u)
    } else o = o (d == "n" ? "\n" : d == "t" ? "\t" : d == "r" ? "\r" : d == "b" ? "\b" : d == "f" ? "\f" : d)
  }
  return o
}
function rend(s, p,   e, c) {
  p = ws(s, p); e = skv(s, p); c = substr(s, p, 1)
  return c == "\"" ? unesc(substr(s, p + 1, e - p - 2)) : (c == "{" || c == "[") ? compact(substr(s, p, e - p)) : substr(s, p, e - p)
}
function child(s, p, g,   c, k, e, x, n) {
  p = ws(s, p); c = substr(s, p, 1)
  if (c == "{") {
    for (p = ws(s, p + 1); substr(s, p, 1) == "\""; ) {
      e = sks(s, p); k = unesc(substr(s, p + 1, e - p - 2)); p = ws(s, e)
      if (substr(s, p, 1) != ":") return 0
      p = ws(s, p + 1); x = skv(s, p)
      if (k == g) return p
      p = ws(s, x); if (substr(s, p, 1) != ",") return 0
      p = ws(s, p + 1)
    }
  } else if (c == "[" && g ~ /^[0-9]+$/) {
    for (p = ws(s, p + 1); p <= length(s) && substr(s, p, 1) != "]"; n++) {
      if (n == g + 0) return p
      x = skv(s, p); if (!x) return 0
      p = ws(s, x); if (substr(s, p, 1) != ",") return 0
      p = ws(s, p + 1)
    }
  }
  return 0
}
function get(s, path,   p, e, n, g, x) {
  p = ws(s, 1); e = skv(s, p)
  if (!e || ws(s, e) <= length(s) || substr(s, p, 1) !~ /[\[{]/) { print "Not valid JSON" > "/dev/stderr"; exit 2 }
  n = split(path, g, ".")
  for (x = 1; x <= n; x++) if (g[x] != "") { p = child(s, p, g[x]); if (!p) exit 1 }
  p = ws(s, p)
  if (substr(s, p, 1) == "[") {
    for (p = ws(s, p + 1); substr(s, p, 1) != "]" && p <= length(s); ) {
      x = skv(s, p); print rend(s, p); p = ws(s, x); if (substr(s, p, 1) == ",") p = ws(s, p + 1)
    }
  } else print rend(s, p)
  exit 0
}

# ---- option tables: " <class>.<name>:<1 takes a value|0 flag>"; class = verb for verb options ---------------
# F field, C chip (--chip-*), I step item, A action
function tables() {
  T = " F.required:0 F.default:1 F.placeholder:1 F.description:1 F.group:1 F.watch:0 F.multiple:0 F.search:0 F.no-search:0 F.page-size:1 F.no-remember:0"
  T = T " F.min:1 F.max:1 F.step:1 F.decimals:1 F.mode:1 F.range:0 F.filter:1 F.initial-dir:1 F.path-format:1 F.options:1 F.option:1 F.flag:1 F.column:1 F.row-key:1 F.rows-json:1"
  T = T " C.description:1 C.icon:1 C.danger:0 C.confirm:0 C.confirm-title:1 C.confirm-text:1 C.confirm-label:1 C.cancel-label:1 C.requires:1 I.state:1 I.detail:1 A.path-format:1"
  T = T " hello.title:1 hello.version:1 prompt.id:1 prompt.title:1 prompt.submit-label:1 prompt.back:0 prompt.no-cancel:0 prompt.description:1 prompt.no-remember:0"
  T = T " patch.id:1 patch.seq:1 patch.remove:1 patch.no-chips:0 confirm.id:1 confirm.title:1 confirm.text:1 confirm.danger:0 confirm.confirm-label:1 confirm.cancel-label:1 confirm.back:0 confirm.no-cancel:0"
  T = T " invalid.id:1 invalid.message:1 invalid.error:1 message.level:1 markdown.file:1 progress.no-cancel:0 progress.cancel:0 steps.id:1 steps.title:1 table.id:1 table.title:1 table.column:1 table.rows-json:1"
  T = T " notify.level:1 notify.title:1 notify.text:1 done.title:1 done.text:1 done.level:1 chip-result.title:1 chip-result.id:1 step. set-env."
  # emit specs: json[=source]:kind  (s string if set, q string, b true if set, f false if set, n number, i int, p string unless native, m unless date)
  S["hello"] = "title:s version:s"
  S["prompt"] = "id:q title:s description:s submit_label:s back:b cancellable=no-cancel:f remember=no-remember:f"
  S["patch"] = "id:q seq:n"
  S["confirm"] = "id:q title:s text:q danger:b confirm_label:s cancel_label:s back:b cancellable=no-cancel:f"
  S["invalid"] = "id:q message:s"
  S["message"] = "level:q text:q"
  S["markdown"] = "text:q"
  S["progress"] = "value:v label:s cancellable=cj:r"
  S["steps"] = "id:q title:s"
  S["step"] = "steps:q id:q state:q detail:s"
  S["chip-result"] = "id:s chip:q state:q title:s text:s"
  S["table"] = "id:s title:s"
  S["notify"] = "title:q text:s level:s"
  S["set-env"] = "name:q value:q"
  S["done"] = "title:s text:s level:s"
  S["F"] = "label:s description:s required:b placeholder:s group:s watch:b remember=no-remember:f"
  S["C"] = "label:s description:s icon:s danger:b"
  S["I"] = "label:q state:t detail:s"
  P["step"] = "steps id state detail*"; P["message"] = "text*"; P["markdown"] = "text*"; P["progress"] = "value label*"
  P["chip-result"] = "chip state text*"; P["notify"] = "title text*"; P["set-env"] = "name value*"
  OP["prompt"] = " field chip"; OP["patch"] = " field chip"; OP["steps"] = " item"; OP["done"] = " action"
}
function kind(c, n,   p) { p = index(T, " " c "." n ":"); return p ? substr(T, p + length(c n) + 3, 1) : "" }
function more() { return i < ARGC }
function isopt() { return more() && substr(ARGV[i], 1, 2) == "--" }
function val(o) { if (!more()) die("option " o " needs a value"); return ARGV[i++] }
function put(c, n, k, v, ls) { X[c, n, k] = (ls && X[c, n, k] != "" ? X[c, n, k] "\001" : "") v }
function opener(c,   n, t, s, r) {
  n = ++N[c]
  if (c == "A") {
    t = val("--action"); s = index(t, ":"); r = s ? index(substr(t, s + 1), ":") : 0; r += s
    if (s < 2 || r == s || substr(t, 1, s - 1) !~ /^(open_url|reveal|copy)$/) die("bad --action " t " (open_url|reveal|copy:label:value)")
    X["A", n, "type"] = substr(t, 1, s - 1); X["A", n, "label"] = substr(t, s + 1, r - s - 1); X["A", n, "value"] = substr(t, r + 1)
    return
  }
  if (c == "F") {
    t = more() ? ARGV[i++] : ""
    if (t !~ /^(text|secret|textarea|number|date|select|list|table|filepick|folderpick|flags)$/) die("bad field type " t)
    X[c, n, "type"] = t
  }
  if (!more() || isopt()) die("--" (c == "F" ? "field" : c == "C" ? "chip" : "item") " needs an id or a name")
  X[c, n, c == "F" ? "name" : "id"] = ARGV[i++]
  if (more() && !isopt()) X[c, n, "label"] = ARGV[i++]
}
# --name applies to the LAST object of class c that owns it; 1 = consumed
function modifier(c, name,   f, v) {
  if (kind(c, name) == "") return 0
  f = c == "C" ? "--chip-" name : "--" name
  if (!N[c]) die(f " has to come after a --" (c == "F" ? "field" : c == "C" ? "chip" : c == "I" ? "item" : "action"))
  v = kind(c, name) == "1" ? val(f) : 1
  put(c, N[c], name, v, (c == "F" && name ~ /^(options|option|flag|column)$/) || (c == "C" && name == "requires"))
  if (c == "C" && name ~ /^confirm-/) X["C", N[c], "confirm"] = 1
  return 1
}
function parse(   t, name, k) {
  while (more()) {
    t = ARGV[i++]
    if (substr(t, 1, 2) != "--") { Q[++NQ] = t; continue }
    name = substr(t, 3)
    if (t ~ /^--(field|chip|item|action)$/ && OP[verb] ~ (" " name)) { opener(toupper(substr(name, 1, 1))); continue }
    if (OP[verb] ~ /field/ && (N["F"] > 0 && modifier("F", name) || name ~ /^chip-/ && modifier("C", substr(name, 6)))) continue
    if (verb == "steps" && modifier("I", name)) continue
    if (verb == "done" && name == "path-format") { modifier("A", name); continue }
    if (verb == "prompt" && !N["F"] && (name == "description" || name == "no-remember")) { G[name] = kind(verb, name) == "1" ? val(t) : 1; continue }
    k = kind(verb, name)
    if (k == "") die(kind("F", name) != "" ? t " has to come after a --field" : "unknown option " t)
    v = k == "1" ? val(t) : 1
    if (name ~ /^(column|error)$/) G[name] = (G[name] == "" ? "" : G[name] "\001") v
    else if (name == "remove") G[name] = (G[name] == "" ? "" : G[name] ",") v
    else G[name] = v
  }
}
function src(c, n, k) { return c == verb ? G[k] : X[c, n, k] }
# one JSON object body from a spec
function emit(c, n, o,   a, t, k, j, v, s) {
  t = split(S[c], a, " ")
  for (j = 1; j <= t; j++) {
    split(a[j], k, /[=:]/)
    s = a[j] ~ /=/ ? k[2] : k[1]; gsub(/_/, "-", s); v = src(c, n, s)
    if (k[length(k)] == "q" || (k[length(k)] == "s" && v != "")) o = ad(o, k[1], q(v))
    else if (k[length(k)] == "b" && v != "") o = ad(o, k[1], "true")
    else if (k[length(k)] == "f" && v != "") o = ad(o, k[1], "false")
    else if (k[length(k)] == "t" && v != "") o = ad(o, k[1], v == "pending" ? "" : q(v))
    else if (k[length(k)] == "n" && v != "") o = ad(o, k[1], nm(v))
    else if (k[length(k)] == "r" && v != "") o = ad(o, k[1], v)
    else if (k[length(k)] == "v") o = ad(o, k[1], v == "" ? "null" : nm(v) + 0 < 0 ? 0 : nm(v) + 0 > 100 ? 100 : nm(v))
  }
  return o
}
function col(spec,   k, l, s) {
  s = index(spec, ":"); k = s ? substr(spec, 1, s - 1) : spec; l = (s && s < length(spec)) ? substr(spec, s + 1) : k
  if (k == "") die("bad column " spec)
  return "{\"key\":" q(k) (l != k ? ",\"label\":" q(l) : "") "}"
}
function cols(t,   n, p, i, s) { n = split(t, p, "\001"); for (i = 1; i <= n; i++) s = s (i > 1 ? "," : "") col(p[i]); return "[" s "]" }
function rows(t,   e) {
  if (t == "") return "[]"
  e = skv(t, 1)
  if (!e || ws(t, e) <= length(t) || substr(compact(t), 1, 1) != "[") die("--rows-json must be a JSON array")
  return compact(t)
}
function fld(n,   t, o, s, k, p, c, np, d, on, it, y, z, w) {
  t = X["F", n, "type"]
  o = ad(ad(emit("F", n), "name", q(X["F", n, "name"])), "type", q(t))
  if (("F", n, "default") in X) {
    d = X["F", n, "default"]
    if (t == "number") o = ad(o, "default", nm(d))
    else if ((t == "list" || t == "table") && X["F", n, "multiple"]) o = ad(o, "default", csv(d))
    else if (t == "flags") {
      for (k = split(d, it, ","); k > 0; k--) on[trim(it[k])] = 1
      np = split(X["F", n, "flag"], p, "\001")
      for (k = 1; k <= np; k++) { split(p[k], c, ":"); s = s (k > 1 ? "," : "") q(c[1]) ":" ((c[1] in on) ? "true" : "false") }
      o = ad(o, "default", "{" s "}"); s = ""
    } else o = ad(o, "default", q(d))
  }
  if (t == "number") {
    for (k = split("min max step", p, " "); k > 0; k--) if (X["F", n, p[k]] != "") o = ad(o, p[k], nm(X["F", n, p[k]]))
    if (X["F", n, "decimals"] != "" && cl(X["F", n, "decimals"], 10)) o = ad(o, "decimals", cl(X["F", n, "decimals"], 10))
  } else if (t == "date") {
    if (X["F", n, "mode"] != "" && X["F", n, "mode"] != "date") o = ad(o, "mode", q(X["F", n, "mode"]))
    if (X["F", n, "range"]) o = ad(o, "range", "true")
  } else if (t == "select" || t == "list") {
    s = X["F", n, "options"]; gsub(/\001/, ",", s); s = substr(csv(s), 2); s = substr(s, 1, length(s) - 1)
    np = split(X["F", n, "option"], p, "\001")
    for (k = 1; k <= np; k++) {
      z = split(p[k], c, ":"); if (c[1] == "") die("bad option " p[k]); y = ""
      for (w = 3; w <= z; w++) y = y (w > 3 ? ":" : "") c[w]
      s = s (s == "" ? "" : ",") (y == "" && (z < 2 || c[2] == "" || c[2] == c[1]) ? q(c[1]) : "{\"value\":" q(c[1]) (z > 1 && c[2] != "" && c[2] != c[1] ? ",\"label\":" q(c[2]) : "") (y != "" ? ",\"description\":" q(y) : "") "}")
    }
    o = ad(o, "options", "[" s "]")
    if (t == "list") o = lv(ad(o, "multiple", X["F", n, "multiple"] ? "true" : "false"), n)
    if (t == "list" && !X["F", n, "multiple"]) o = rm(o, "\"multiple\":false")
  } else if (t == "table") {
    o = ad(ad(o, "columns", cols(X["F", n, "column"])), "rows", rows(X["F", n, "rows-json"]))
    if (X["F", n, "row-key"] != "" && X["F", n, "row-key"] != "id") o = ad(o, "row_key", q(X["F", n, "row-key"]))
    if (X["F", n, "multiple"]) o = ad(o, "multiple", "true")
    o = lv(o, n)
  } else if (t == "filepick" || t == "folderpick") {
    for (k = split("filter initial_dir path_format", p, " "); k > 0; k--) {
      d = X["F", n, substr(p[k], 1, 1) == "i" ? "initial-dir" : substr(p[k], 1, 1) == "p" ? "path-format" : "filter"]
      if (d != "" && d != "native") o = ad(o, p[k], q(d))
    }
  } else if (t == "flags") {
    np = split(X["F", n, "flag"], p, "\001")
    for (k = 1; k <= np; k++) {
      z = split(p[k], c, ":"); if (c[1] == "") die("bad flag " p[k]); d = tolower(c[3])
      s = s (s == "" ? "" : ",") "{\"name\":" q(c[1]) (z > 1 && c[2] != "" && c[2] != c[1] ? ",\"label\":" q(c[2]) : "") (d ~ /^(true|1|yes|on|default)$/ ? ",\"default\":true" : "") "}"
    }
    o = ad(o, "options", "[" s "]")
  }
  return "{" o "}"
}
function rm(o, part,   i) { i = index(o, part); return i ? substr(o, 1, i - 1 - (i > 1)) substr(o, i + length(part)) : o }
function lv(o, n) {
  if (X["F", n, "search"]) o = ad(o, "searchable", "true"); else if (X["F", n, "no-search"]) o = ad(o, "searchable", "false")
  return X["F", n, "page-size"] != "" && cl(X["F", n, "page-size"], 200) > 0 ? ad(o, "page_size", cl(X["F", n, "page-size"], 200)) : o
}
function many(c, f,   s, k) { for (k = 1; k <= N[c]; k++) s = s (k > 1 ? "," : "") (c == "F" ? fld(k) : c == "C" ? chp(k) : c == "I" ? itm(k) : act(k)); return "[" s "]" }
function chp(k,   o, c) {
  o = ad("", "id", q(X["C", k, "id"])); if (X["C", k, "label"] == X["C", k, "id"]) X["C", k, "label"] = ""
  o = emit("C", k, o)
  if (X["C", k, "confirm"]) {
    c = ""
    if (X["C", k, "confirm-title"] != "") c = ad(c, "title", q(X["C", k, "confirm-title"]))
    if (X["C", k, "confirm-text"] != "") c = ad(c, "text", q(X["C", k, "confirm-text"]))
    if (X["C", k, "confirm-label"] != "") c = ad(c, "confirm_label", q(X["C", k, "confirm-label"]))
    if (X["C", k, "cancel-label"] != "") c = ad(c, "cancel_label", q(X["C", k, "cancel-label"]))
    o = ad(o, "confirm", c == "" ? "true" : "{" c "}")
  }
  if (X["C", k, "requires"] != "") o = ad(o, "requires", csv(X["C", k, "requires"]))
  return "{" o "}"
}
function itm(k,   o) {
  if (X["I", k, "label"] == "") X["I", k, "label"] = X["I", k, "id"]
  if (X["I", k, "state"] != "" && X["I", k, "state"] !~ /^(pending|running|success|error|skipped)$/) die("bad state " X["I", k, "state"])
  return "{" emit("I", k, ad("", "id", q(X["I", k, "id"]))) "}"
}
function act(k,   t, o) {
  t = X["A", k, "type"]
  o = ad(ad("", "type", q(t)), t == "open_url" ? "url" : t == "reveal" ? "path" : "value", q(X["A", k, "value"]))
  if (t == "reveal" && X["A", k, "path-format"] != "" && X["A", k, "path-format"] != "native") o = ad(o, "path_format", q(X["A", k, "path-format"]))
  return "{" ad(o, "label", q(X["A", k, "label"])) "}"
}
function lvl(t) { if (t !~ /^(info|success|warning|error)$/) die("bad level " t); return t }

BEGIN {
  for (j = 1; j < 32; j++) OD[sprintf("%c", j)] = j
  verb = ARGV[1]; i = 2
  if (verb == "get") get(ARGV[2], ARGV[3])
  tables()
  if (!index(T, " " verb ".") && !index(" hello prompt patch confirm invalid message markdown progress steps step table notify set-env done chip-result ", " " verb " ")) die("unknown verb")
  parse()
  # positionals fill the named slots of the verb (the last one takes the rest)
  np = split(P[verb], pn, " ")
  for (k = 1; k <= np; k++) {
    nmk = pn[k]; rest = nmk ~ /\*$/; gsub(/\*/, "", nmk)
    if (G[nmk] != "" && !(nmk == "label")) continue
    if (k > NQ && !rest && 0) break
    if (rest) { for (z = k; z <= NQ; z++) G[nmk] = G[nmk] (z > k ? (verb == "markdown" ? "\n" : " ") : "") Q[z]; break }
    if (k <= NQ) G[nmk] = Q[k]
  }
  if (verb == "message") {
    if (G["level"] == "" && NQ > 1 && Q[1] ~ /^(info|success|warning|error)$/) { G["level"] = Q[1]; G["text"] = ""; for (z = 2; z <= NQ; z++) G["text"] = G["text"] (z > 2 ? " " : "") Q[z] }
    G["level"] = G["level"] == "" ? "info" : lvl(G["level"])
  }
  if (verb == "markdown" && G["file"] != "") { while ((getline ln < G["file"]) > 0) { fl = fl (f2++ ? "\n" : "") ln }; if (!f2) die("can not read " G["file"]); close(G["file"]); G["text"] = fl }
  if (verb == "progress") { v = tolower(Q[1]); G["value"] = v ~ /^(null|indeterminate|-)$/ ? "" : Q[1]; if (!NQ) die("needs a value"); G["cj"] = G["no-cancel"] ? "false" : G["cancel"] ? "true" : "" }
  if (G["level"] != "") lvl(G["level"])
  if (verb == "notify" && (G["title"] == "" && G["text"] == "")) die("needs a title")
  if (verb == "done" && G["level"] == "success") G["level"] = ""
  if (verb == "notify" && G["level"] == "info") G["level"] = ""
  nreq = split("id text name title chip state steps", rq, " ")
  if (verb ~ /^(prompt|patch|confirm|invalid|steps)$/ && G["id"] == "") die("needs --id")
  if (verb == "confirm" && G["text"] == "") die("needs --text")
  if (verb == "table" && G["column"] == "") die("needs --column")
  if (verb ~ /^(step|chip-result|set-env|markdown|message)$/ && (verb == "step" ? NQ < 3 : verb == "chip-result" ? NQ < 2 : verb == "markdown" ? G["text"] == "" : NQ < 1)) die("missing arguments (kip --help)")
  if (verb == "step" && Q[3] !~ /^(pending|running|success|error|skipped)$/) die("bad state " Q[3])
  if (verb == "chip-result" && Q[2] !~ /^(running|success|error)$/) die("bad chip state " Q[2])
  t = verb; gsub(/-/, "_", t)
  o = emit(verb, 0, "\"kip\":1,\"type\":\"" t "\"")
  if (verb == "prompt") { o = ad(o, "fields", many("F")); if (N["C"]) o = ad(o, "chips", many("C")) }
  else if (verb == "patch") { o = ad(o, "fields", many("F")); if (G["remove"] != "") o = ad(o, "remove", csv(G["remove"])); if (G["no-chips"] || N["C"]) o = ad(o, "chips", many("C")) }
  else if (verb == "invalid") {
    n = split(G["error"], p, "\001")
    for (k = 1; k <= n; k++) { e = index(p[k], "="); if (e < 2) die("bad --error " p[k]); s = s (k > 1 ? "," : "") q(substr(p[k], 1, e - 1)) ":" q(substr(p[k], e + 1)) }
    o = ad(o, "errors", "{" s "}")
  }
  else if (verb == "steps") o = ad(o, "items", many("I"))
  else if (verb == "table") o = ad(ad(o, "columns", cols(G["column"])), "rows", rows(G["rows-json"]))
  else if (verb == "done" && N["A"]) o = ad(o, "actions", many("A"))
  print "{" o "}"
  exit 0
}
