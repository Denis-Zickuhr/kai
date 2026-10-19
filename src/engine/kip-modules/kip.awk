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
# F field, C chip (--chip-*), R row action (--row-action-*), I step item, K kv item, Z stat, A action
function tables() {
  T = " F.required:0 F.default:1 F.placeholder:1 F.description:1 F.group:1 F.watch:0 F.multiple:0 F.search:0 F.no-search:0 F.page-size:1 F.no-remember:0"
  T = T " F.min:1 F.max:1 F.step:1 F.decimals:1 F.mode:1 F.range:0 F.filter:1 F.initial-dir:1 F.path-format:1 F.options:1 F.option:1 F.flag:1 F.column:1 F.row-key:1 F.rows-json:1"
  T = T " F.style:1 F.max-length:1 F.nodes-json:1 F.patch:1 F.patch-file:1 F.select:1"
  T = T " C.description:1 C.icon:1 C.danger:0 C.confirm:0 C.confirm-title:1 C.confirm-text:1 C.confirm-label:1 C.cancel-label:1 C.requires:1 I.state:1 I.detail:1 A.path-format:1"
  T = T " R.description:1 R.icon:1 R.danger:0 R.confirm:0 R.confirm-title:1 R.confirm-text:1 R.confirm-label:1 R.cancel-label:1 K.format:1 K.tone:1 K.copy:0 Z.detail:1 Z.tone:1"
  T = T " table.row-key:1 progress.id:1 step.log:1 diff.id:1 diff.title:1 diff.patch:1 diff.patch-file:1 diff.left-file:1 diff.right-file:1 diff.left-text:1 diff.right-text:1"
  T = T " diff.left-name:1 diff.right-name:1 diff.language:1 diff.view:1 diff.context:1 diff.collapse-after:1 diff.ignore-whitespace:0 kv.id:1 kv.title:1 stats.id:1"
  T = T " code.id:1 code.title:1 code.language:1 code.file:1 code.highlight:1 code.max-lines:1 code.no-line-numbers:0"
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
  S["progress"] = "value:v label:s cancellable=cj:r id:s"
  S["steps"] = "id:q title:s"
  S["step"] = "steps:q id:q state:s detail:s log:s"
  S["chip-result"] = "id:s chip:q state:q title:s text:s"
  S["table"] = "id:s title:s"
  S["notify"] = "title:q text:s level:s"
  S["set-env"] = "name:q value:q"
  S["done"] = "title:s text:s level:s"
  S["F"] = "label:s description:s required:b placeholder:s group:s watch:b remember=no-remember:f"
  S["C"] = "label:s description:s icon:s danger:b"; S["R"] = S["C"]
  S["diff"] = "id:s title:s"; S["kv"] = "id:s title:s"; S["stats"] = "id:s"; S["code"] = "id:s title:s language:s"
  S["I"] = "label:q state:t detail:s"
  P["step"] = "steps id state detail*"; P["message"] = "text*"; P["markdown"] = "text*"; P["progress"] = "value label*"
  P["chip-result"] = "chip state text*"; P["notify"] = "title text*"; P["set-env"] = "name value*"; P["code"] = "text*"
  OP["prompt"] = " field chip row-action"; OP["patch"] = " field chip row-action"; OP["steps"] = " item"; OP["done"] = " action"
  OP["table"] = " row-action"; OP["kv"] = " item"; OP["stats"] = " stat"
}
function kind(c, n,   p) { p = index(T, " " c "." n ":"); return p ? substr(T, p + length(c n) + 3, 1) : "" }
function more() { return i < ARGC }
function isopt() { return more() && substr(ARGV[i], 1, 2) == "--" }
function val(o) { if (!more()) die("option " o " needs a value"); return ARGV[i++] }
function put(c, n, k, v, ls) { X[c, n, k] = (ls && X[c, n, k] != "" ? X[c, n, k] "\001" : "") v }
function cls(name) { return name == "row-action" ? "R" : name == "stat" ? "Z" : verb == "kv" && name == "item" ? "K" : toupper(substr(name, 1, 1)) }
function opener(c,   n, t, s, r) {
  n = ++N[c]
  if (c == "K" || c == "Z") {
    if (!more() || isopt()) die((c == "K" ? "--item" : "--stat") " needs a key and a value")
    X[c, n, "key"] = ARGV[i++]
    if (!more()) die((c == "K" ? "--item" : "--stat") " needs a key and a value")
    X[c, n, "value"] = ARGV[i++]
    return
  }
  if (c == "R") { if (OP[verb] ~ /field/ && !N["F"]) die("--row-action has to come after a --field"); RF[n] = N["F"] }
  if (c == "A") {
    t = val("--action"); s = index(t, ":"); r = s ? index(substr(t, s + 1), ":") : 0; r += s
    if (s < 2 || r == s || substr(t, 1, s - 1) !~ /^(open_url|reveal|copy)$/) die("bad --action " t " (open_url|reveal|copy:label:value)")
    X["A", n, "type"] = substr(t, 1, s - 1); X["A", n, "label"] = substr(t, s + 1, r - s - 1); X["A", n, "value"] = substr(t, r + 1)
    return
  }
  if (c == "F") {
    t = more() ? ARGV[i++] : ""
    if (t !~ /^(text|secret|textarea|number|date|select|list|table|filepick|folderpick|flags|tree|diff)$/) die("bad field type " t)
    X[c, n, "type"] = t
  }
  if (!more() || isopt()) die("--" (c == "F" ? "field" : c == "C" ? "chip" : c == "R" ? "row-action" : "item") " needs an id or a name")
  X[c, n, c == "F" ? "name" : "id"] = ARGV[i++]
  if (more() && !isopt()) X[c, n, "label"] = ARGV[i++]
}
# --name applies to the LAST object of class c that owns it; 1 = consumed
function modifier(c, name,   f, v) {
  if (kind(c, name) == "") return 0
  f = c == "C" ? "--chip-" name : c == "R" ? "--row-action-" name : "--" name
  if (!N[c]) die(f " has to come after a --" (c == "F" ? "field" : c == "C" ? "chip" : c == "R" ? "row-action" : c == "I" || c == "K" ? "item" : c == "Z" ? "stat" : "action"))
  v = kind(c, name) == "1" ? val(f) : 1
  if (c == "F" && (name == "patch" || name == "patch-file")) v = name == "patch" && v != "-" ? v : rd(v)
  put(c, N[c], name, v, (c == "F" && name ~ /^(options|option|flag|column)$/) || (c == "C" && name == "requires"))
  if ((c == "C" || c == "R") && name ~ /^confirm-/) X[c, N[c], "confirm"] = 1
  if (c == "F" && name == "options") X["F", N["F"], "ol"] = "c" (X["F", N["F"], "nc"] += ncsv(v))
  if (c == "F" && name == "option") X["F", N["F"], "ol"] = "o" (++X["F", N["F"], "no"])
  return 1
}
function ncsv(t,   n, p, i, k) { n = split(t, p, ","); for (i = 1; i <= n; i++) if (trim(p[i]) != "") k++; return k }
# text of a file, or of stdin for "-": no trailing newline, and cut at the last whole line under 512 KiB (TR = 1).
# stdin is never closed: close("/dev/stdin") crashes mawk 1.3.4
function rd(f,   ln, o, r, k, sz) {
  if (f == "-") f = "/dev/stdin"
  while ((r = (getline ln < f)) > 0) {
    if (sz + length(ln) + (k ? 1 : 0) > 524288) { TR = 1; break }
    sz += length(ln) + (k ? 1 : 0); o = o (k++ ? "\n" : "") ln
  }
  if (r < 0) die("can not read " f)
  if (f != "/dev/stdin") close(f)
  return o
}
function parse(   t, name, k) {
  while (more()) {
    t = ARGV[i++]
    if (substr(t, 1, 2) != "--") { Q[++NQ] = t; continue }
    name = substr(t, 3)
    if (t ~ /^--(field|chip|item|action|row-action|stat)$/ && OP[verb] ~ (" " name "( |$)")) { opener(cls(name)); continue }
    if (OP[verb] ~ /field/ && name == "option-icon") { if (!N["F"]) die(t " has to come after a --field"); if (X["F", N["F"], "ol"] == "") die(t " has to come after an option"); X["F", N["F"], "oi", X["F", N["F"], "ol"]] = val(t); continue }
    if (OP[verb] ~ /field/ && (N["F"] > 0 && modifier("F", name) || name ~ /^chip-/ && modifier("C", substr(name, 6)))) continue
    if (OP[verb] ~ /row-action/ && name ~ /^row-action-/ && modifier("R", substr(name, 12))) continue
    if (verb == "steps" && modifier("I", name)) continue
    if (verb == "kv" && modifier("K", name)) continue
    if (verb == "stats" && modifier("Z", name)) continue
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
function col(spec,   k, l, s, f, j) {
  s = index(spec, ":"); k = s ? substr(spec, 1, s - 1) : spec; l = s ? substr(spec, s + 1) : ""
  if (match(l, /:(text|mono|muted|number|badge|copy)$/)) { f = substr(l, RSTART + 1); l = substr(l, 1, RSTART - 1) }
  if (l == "") l = k
  if (k == "") die("bad column " spec)
  return "{\"key\":" q(k) (l != k ? ",\"label\":" q(l) : "") (f != "" ? ",\"format\":" q(f) : "") "}"
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
    s = X["F", n, "options"]; gsub(/\001/, ",", s); np = split(s, p, ","); s = ""
    for (k = 1; k <= np; k++) if (trim(p[k]) != "") { d = X["F", n, "oi", "c" (++w)]; s = s (s == "" ? "" : ",") (d == "" ? q(trim(p[k])) : "{\"value\":" q(trim(p[k])) ",\"icon\":" q(d) "}") }
    np = split(X["F", n, "option"], p, "\001")
    for (k = 1; k <= np; k++) {
      z = split(p[k], c, ":"); if (c[1] == "") die("bad option " p[k]); y = ""; d = X["F", n, "oi", "o" k]
      for (w = 3; w <= z; w++) y = y (w > 3 ? ":" : "") c[w]
      s = s (s == "" ? "" : ",") (y == "" && d == "" && (z < 2 || c[2] == "" || c[2] == c[1]) ? q(c[1]) : "{\"value\":" q(c[1]) (z > 1 && c[2] != "" && c[2] != c[1] ? ",\"label\":" q(c[2]) : "") (y != "" ? ",\"description\":" q(y) : "") (d != "" ? ",\"icon\":" q(d) : "") "}")
    }
    o = ad(o, "options", "[" s "]")
    if (t == "select" && X["F", n, "style"] != "") o = ad(o, "style", q(X["F", n, "style"]))
    if (t == "list") o = lv(ad(o, "multiple", X["F", n, "multiple"] ? "true" : "false"), n)
    if (t == "list" && !X["F", n, "multiple"]) o = rm(o, "\"multiple\":false")
  } else if (t == "table") {
    o = ad(ad(o, "columns", cols(X["F", n, "column"])), "rows", rows(X["F", n, "rows-json"]))
    if (X["F", n, "row-key"] != "" && X["F", n, "row-key"] != "id") o = ad(o, "row_key", q(X["F", n, "row-key"]))
    if (X["F", n, "multiple"]) o = ad(o, "multiple", "true")
    o = lv(o, n)
    for (k = 1; k <= N["R"]; k++) if (RF[k] == n) s = s (s == "" ? "" : ",") chp("R", k)
    if (s != "") o = ad(o, "row_actions", "[" s "]")
  } else if (t == "tree") {
    o = ad(o, "nodes", rows(X["F", n, "nodes-json"]))
    if (X["F", n, "multiple"]) o = ad(o, "multiple", "true")
    if (X["F", n, "search"]) o = ad(o, "searchable", "true"); else if (X["F", n, "no-search"]) o = ad(o, "searchable", "false")
  } else if (t == "diff") {
    o = ad(o, "patch", q(X["F", n, "patch-file"] != "" ? X["F", n, "patch-file"] : X["F", n, "patch"]))
    if (X["F", n, "select"] != "" && X["F", n, "select"] != "hunks") o = ad(o, "select", q(X["F", n, "select"]))
    if (TR) o = ad(o, "truncated", "true")
  } else if ((t == "text" || t == "textarea") && X["F", n, "max-length"] != "" && int(nm(X["F", n, "max-length"])) > 0) {
    o = ad(o, "max_length", int(nm(X["F", n, "max-length"])))
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
function many(c, f,   s, k) { for (k = 1; k <= N[c]; k++) s = s (k > 1 ? "," : "") (c == "F" ? fld(k) : c == "C" || c == "R" ? chp(c, k) : c == "I" ? itm(k) : c == "K" ? kvi(k) : c == "Z" ? sti(k) : act(k)); return "[" s "]" }
function chp(g, k,   o, c) {
  o = ad("", "id", q(X[g, k, "id"])); if (X[g, k, "label"] == X[g, k, "id"]) X[g, k, "label"] = ""
  o = emit(g, k, o)
  if (X[g, k, "confirm"]) {
    c = ""
    if (X[g, k, "confirm-title"] != "") c = ad(c, "title", q(X[g, k, "confirm-title"]))
    if (X[g, k, "confirm-text"] != "") c = ad(c, "text", q(X[g, k, "confirm-text"]))
    if (X[g, k, "confirm-label"] != "") c = ad(c, "confirm_label", q(X[g, k, "confirm-label"]))
    if (X[g, k, "cancel-label"] != "") c = ad(c, "cancel_label", q(X[g, k, "cancel-label"]))
    o = ad(o, "confirm", c == "" ? "true" : "{" c "}")
  }
  if (X[g, k, "requires"] != "") o = ad(o, "requires", csv(X[g, k, "requires"]))
  return "{" o "}"
}
function kvi(k,   v, o) {
  v = q(X["K", k, "value"])
  if (X["K", k, "tone"] != "") { X["K", k, "format"] = "badge"; v = "{\"text\":" v ",\"tone\":" q(X["K", k, "tone"]) "}" }
  o = ad(ad("", "key", q(X["K", k, "key"])), "value", v)
  if (X["K", k, "format"] != "") o = ad(o, "format", q(X["K", k, "format"]))
  return "{" (X["K", k, "copy"] ? ad(o, "copy", "true") : o) "}"
}
function sti(k,   o) {
  o = ad(ad("", "label", q(X["Z", k, "key"])), "value", q(X["Z", k, "value"]))
  if (X["Z", k, "detail"] != "") o = ad(o, "detail", q(X["Z", k, "detail"]))
  return "{" (X["Z", k, "tone"] != "" ? ad(o, "tone", q(X["Z", k, "tone"])) : o) "}"
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
  if (!index(T, " " verb ".") && !index(" hello prompt patch confirm invalid message markdown progress steps step table notify set-env done chip-result diff kv stats code ", " " verb " ")) die("unknown verb")
  parse()
  # positionals fill the named slots of the verb (the last one takes the rest)
  np = split(P[verb], pn, " ")
  for (k = 1; k <= np; k++) {
    nmk = pn[k]; rest = nmk ~ /\*$/; gsub(/\*/, "", nmk)
    if (G[nmk] != "" && !(nmk == "label")) continue
    if (k > NQ && !rest && 0) break
    if (rest) { for (z = k; z <= NQ; z++) G[nmk] = G[nmk] (z > k ? (verb == "markdown" || verb == "code" ? "\n" : " ") : "") Q[z]; break }
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
  if (verb ~ /^(step|chip-result|set-env|markdown|message)$/ && (verb == "step" ? NQ < (G["log"] == "" ? 3 : 2) : verb == "chip-result" ? NQ < 2 : verb == "markdown" ? G["text"] == "" : NQ < 1)) die("missing arguments (kip --help)")
  if (verb == "step" && NQ >= 3 && Q[3] !~ /^(pending|running|success|error|skipped)$/) die("bad state " Q[3])
  if (verb == "code") {
    if (G["file"] != "") G["text"] = rd(G["file"]) (G["text"] != "" ? "\n" G["text"] : "")
    if (G["file"] == "" && !NQ) die("missing arguments (kip --help)")
    n = split(G["highlight"], p, ","); s = ""
    for (k = 1; k <= n; k++) if (trim(p[k]) != "") { if (trim(p[k]) !~ /^[0-9]+$/ || trim(p[k]) + 0 < 1) die("not a number: " p[k]); s = s (s == "" ? "" : ",") (trim(p[k]) + 0) }
  }
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
  else if (verb == "table") {
    o = ad(ad(o, "columns", cols(G["column"])), "rows", rows(G["rows-json"]))
    if (G["row-key"] != "" && G["row-key"] != "id") o = ad(o, "row_key", q(G["row-key"]))
    if (N["R"]) o = ad(o, "row_actions", many("R"))
  }
  else if (verb == "diff") {
    if (G["view"] != "" && G["view"] !~ /^(unified|split)$/) die("bad --view " G["view"] " (unified|split)")
    hp = ("patch" in G) || ("patch-file" in G)
    if (G["patch-file"] != "") G["patch"] = rd(G["patch-file"]); else if (G["patch"] == "-") G["patch"] = rd("-")
    if (G["left-file"] != "") { G["left-text"] = rd(G["left-file"]); if (G["left-name"] == "") G["left-name"] = G["left-file"]; G["lt"] = 1 }
    if (G["right-file"] != "") { G["right-text"] = rd(G["right-file"]); if (G["right-name"] == "") G["right-name"] = G["right-file"]; G["rt"] = 1 }
    if (hp) o = ad(o, "patch", q(G["patch"]))
    else if ((G["lt"] || "left-text" in G) && (G["rt"] || "right-text" in G)) {
      o = ad(o, "left", "{" (G["left-name"] != "" ? "\"name\":" q(G["left-name"]) "," : "") "\"text\":" q(G["left-text"]) "}")
      o = ad(o, "right", "{" (G["right-name"] != "" ? "\"name\":" q(G["right-name"]) "," : "") "\"text\":" q(G["right-text"]) "}")
    } else die("needs --patch, --patch-file or both sides (--left-file/--left-text and --right-file/--right-text)")
    if (G["language"] != "") o = ad(o, "language", q(tolower(G["language"])))
    if (G["view"] != "") o = ad(o, "view", q(G["view"]))
    if (G["context"] != "") { k = int(nm(G["context"])); k = k < -1 ? -1 : k > 1000 ? 1000 : k; if (k != 3) o = ad(o, "context", k) }
    if (G["ignore-whitespace"]) o = ad(o, "ignore_whitespace", "true")
    if (G["collapse-after"] != "") { k = int(nm(G["collapse-after"])); k = k < 0 ? 0 : k; if (k != 3) o = ad(o, "collapse_after", k) }
    if (TR) o = ad(o, "truncated", "true")
  }
  else if (verb == "kv") { if (!N["K"]) die("needs --item"); o = ad(o, "items", many("K")) }
  else if (verb == "stats") { if (!N["Z"]) die("needs --stat"); o = ad(o, "items", many("Z")) }
  else if (verb == "code") {
    o = ad(o, "text", q(G["text"]))
    if (s != "") o = ad(o, "highlight", "[" s "]")
    if (G["max-lines"] != "") { k = int(nm(G["max-lines"])); if (k > 0) o = ad(o, "max_lines", k) }
    if (G["no-line-numbers"]) o = ad(o, "line_numbers", "false")
    if (TR) o = ad(o, "truncated", "true")
  }
  else if (verb == "done" && N["A"]) o = ad(o, "actions", many("A"))
  print "{" o "}"
  exit 0
}
