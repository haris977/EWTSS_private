# xml_to_json — hand-written XML→JSON parser

Standalone, dependency-free C++17 module. Converts CA120/DDF-550/DDF-1GTX-style
XML control messages (`<Request>`/`<Reply>`/`<Event>`/`<RDFS>` envelopes with
attributes, nesting, and repeated sibling tags) into JSON, without pulling in
pugixml or any other library.

**Not yet wired into any production parser.** `ca120_parser.cpp`,
`ddf550_parser.cpp`, and `ddf1gtx_parser.cpp` each still use their own older,
ad-hoc field-by-field XML scanning (`xml_attr`/`xml_text`/`xml_has` helpers) —
this module was built and tested in isolation first. Wiring it in is a
separate, not-yet-made decision.

Full background: [`docs/ewtss/specs/xml-to-json-parser-design.md`](../../../../docs/ewtss/specs/xml-to-json-parser-design.md)
(design rationale, why pugixml was rejected, the 7 supported shapes) and
[`docs/ewtss/plans/xml-to-json-parser-plan.md`](../../../../docs/ewtss/plans/xml-to-json-parser-plan.md)
(how it was built, task-by-task).

---

## 1. File map

```
xml_node.h          Data model: the parsed-XML tree shape. No logic.
xml_parser.h/.cpp    Stage 1: XML text  -> XmlNode tree
json_mirror.h/.cpp   Stage 2: XmlNode tree -> JSON string
xml_to_json.h/.cpp   Stage 3: wires 1+2 together, adds the envelope
tests/
  test_xml_parser.cpp    tests Stage 1 alone
  test_json_mirror.cpp   tests Stage 2 alone (hand-built XmlNode trees, no parsing)
  test_xml_to_json.cpp   tests Stage 3 end-to-end (real XML in, JSON out)
```

Each stage only depends on the one before it. `json_mirror` never includes
`xml_parser.h` — it only knows about `XmlNode` (from `xml_node.h`). This means
you can test/debug the parser and the mirror completely independently of each
other, which is the main reason to keep the boundary in your head when
something breaks: **first figure out which of the two stages is wrong.**

---

## 2. The data model (`xml_node.h`)

```cpp
struct XmlNode {
    std::string name;                                        // "Request", "GUID", "IP", ...
    std::vector<std::pair<std::string, std::string>> attributes;  // [("type","get"), ("id","8")]
    std::vector<XmlNode> children;                            // in document order
    std::string text;                                         // only meaningful if children is empty
};
```

This is the *only* shared vocabulary between Stage 1 and Stage 2. If you're
debugging and unsure whether a bug is "the parser built the wrong tree" or
"the mirror turned a correct tree into the wrong JSON", the fastest way to
find out is to print an `XmlNode` by hand (name / attributes / children.size()
/ text) right after `parse_xml()` returns, before it ever reaches
`node_to_json()`.

---

## 3. Stage 1 — `xml_parser.cpp`: XML text → `XmlNode` tree

**Entry point:** `XmlParseResult parse_xml(const char* xml, size_t len);`

```cpp
struct XmlParseResult {
    bool ok;
    XmlNode root;         // valid only if ok == true
    std::string error;    // valid only if ok == false
    size_t error_offset;  // byte offset into `xml` where the error was detected
};
```

It's a straightforward **recursive-descent parser** — the kind you'd write by
hand for a small, known grammar, no parser-generator, no lookahead tables.
One function per grammar rule:

| Function | Grammar rule | What it does |
|---|---|---|
| `parse_element` | one `<Tag ...>...</Tag>` or `<Tag .../>` | the main recursive function — everything else is called from here |
| `parse_attributes` | `attr="value" attr2='value2'` | loops reading `name=value` pairs until it hits `>` or `/` |
| `parse_content` | whatever's between `>` and `</Tag>` | decides: is this children, or text? (see §3.1 below — this is the trickiest part) |
| `parse_end_tag` | `</Tag>` | must match the opening tag's name, or it's a parse error |
| `read_name` | a tag or attribute name | scans alnum/`_`/`-`/`.`/`:` characters |
| `skip_whitespace` | — | advances past spaces/tabs/newlines |
| `fail` | — | the *only* place that sets `ok=false` — records the message and current byte offset |

**Everything funnels through `fail()`.** If you're debugging a "why did this
input fail to parse" question, grep for the exact message string in
`xml_parser.cpp` — each `fail(...)` call site has a unique, descriptive
message (`"expected '=' after attribute name"`, `"mismatched end tag: expected
</X>, found </Y>"`, etc.), so the message alone usually tells you which
`fail()` call fired without needing a debugger.

**`result.error_offset`** is the byte index into your original XML string
where parsing gave up. `xml + error_offset` points at (or very near) the
offending character — printing `xml[error_offset .. error_offset+20]` is
usually enough to see the problem by eye.

### 3.1 The one non-obvious trick: `parse_content`'s lookahead

An element's content is either child elements *or* text — never a mix (see
§4 of the design spec for why). But you can't know which one you're looking
at without skipping past any leading whitespace first... except if it turns
out to be *real text*, that whitespace was part of the text and must NOT be
thrown away.

The fix: `parse_content` does a **non-destructive lookahead** — it computes
where the whitespace ends (`peek_pos`) without moving the real cursor, checks
what character is there:
- if it's `<` → that whitespace was just pretty-print indentation between
  tags, safe to discard. Commit the cursor forward (`c.pos = peek_pos`) and
  parse children.
- if it's anything else → that whitespace was part of real text. Reset the
  cursor to where content started (`c.pos = content_start`, undoing nothing
  since we never moved it for real) and capture everything up to the next
  `<` **verbatim**, spaces and all.

This is why `<Tag>  hello  </Tag>` correctly parses to `text == "  hello  "`
(both spaces kept) while `<Root>\n  <Child>x</Child>\n</Root>` correctly
parses to `Root.text == ""` (the indentation is discarded, `Child` is a real
child). If a debugging session ever shows text with unexpected/missing
whitespace, this is the function to re-read.

### 3.2 Safety guards

- **Null check** (`parse_xml`, very first thing): `xml == nullptr` returns a
  parse error immediately rather than crashing.
- **Recursion depth** (`parse_element`, first line): `depth > MAX_DEPTH` (32)
  fails immediately rather than letting a maliciously/accidentally
  deeply-nested document blow the C++ call stack. `depth` is threaded through
  every recursive call — there is exactly one place `parse_element` recurses
  into itself (via `parse_content`'s child loop), so this can't be bypassed.
- **Comments/CDATA/`<?xml?>` are rejected** — checked the instant `parse_element`
  sees `<!` or `<?`.
- **Namespaces (`<ns:Tag>`) and entities (`&amp;`) are deliberately NOT
  rejected** — confirmed absent from every real ICD we checked (CA120,
  DDF-550, DDF-1GTX), so the parser doesn't spend complexity guarding against
  input that can't occur. If a future hardware variant's ICD turns out to use
  either, this is the place to revisit (see design spec §2).

---

## 4. Stage 2 — `json_mirror.cpp`: `XmlNode` tree → JSON string

**Entry point:** `std::string node_to_json(const XmlNode& node, int depth, MirrorBudget& budget);`

This is a straight **tree walk**, not a parser — there's no XML syntax here
at all, just a `XmlNode` in memory and the question "what JSON does this
node's shape correspond to?" `node_to_json` answers that with 4 cases,
checked in this order:

1. **Guard first:** `budget.nodes_visited` incremented; if `depth > MAX_DEPTH`
   (32) or `nodes_visited > MAX_NODES` (2000), return a truncation marker
   (`{"truncated":"true","node_count":N}`) instead of recursing further. This
   is a soft limit (not an error) — a huge but well-formed document isn't
   malformed, it's just too big to fully mirror.
2. **No attributes, no children, no non-whitespace text** → `"{}"` (empty object).
3. **No attributes, no children, but real text** → the text as a plain quoted
   JSON string (e.g. `"sukSEs-JTO"`) — NOT wrapped in `{}`.
4. **Anything else** (has attributes, and/or has children) → delegates to
   `node_object_body`, which builds a real JSON object.

`node_object_body` is where the "attributes and children merge into one flat
object" logic lives:
- Every attribute becomes a key (`to_snake_case`'d name → `json_quote`'d value).
- Children are grouped by (snake_cased) tag name. A tag that appears once
  becomes a scalar/object key; a tag that repeats (2+ times) becomes a JSON
  array of that many mirrored values.
- If there were zero child elements AND the node has non-whitespace text, a
  synthetic `"#text"` key carries it (this is the case for e.g.
  `<Input sourceType="tuner">{guid}</Input>` → `{"source_type":"tuner","#text":"{guid}"}`).
- **Key collisions are last-write-wins**: attributes are added first, then
  children — so if a child element happens to share a snake_case name with
  an attribute (never observed in real ICDs, but handled), the child's value
  silently overwrites the attribute's. Look at `set_field`'s `index` map if
  you ever need to trace this.

**`to_snake_case`** deserves its own mental model: it's *not* "insert `_`
before every uppercase letter" (that would turn `"GUID"` into `"g_u_i_d"`,
which nobody wants). It inserts `_` only at a genuine word-boundary
transition — lowercase/digit → uppercase, or the *last* uppercase letter
before a run drops into lowercase (e.g. `"DFSelect"` → `"df_select"`, the
underscore lands between `F` and `S` because `S` is followed by lowercase
`e`). If a snake_case key ever looks wrong, trace this function character by
character against the table in its own inline comments — it's a small
function but the boundary logic is genuinely fiddly to eyeball.

**`json_quote`** is plain JSON string escaping — `"`, `\`, `\n`, `\r`, `\t`,
and control characters below `0x20` (as `\u00XX`). Nothing else is special:
literal `{`, `}`, `-`, etc. pass through unescaped, which is correct JSON.

---

## 5. Stage 3 — `xml_to_json.cpp`: wiring + the envelope

**Public entry point (the only thing outside code should call):**

```cpp
std::string parse_xml_to_json(const char* xml, size_t len,
                               const char* frame_kind_hint,  // "request" or "reply"
                               const char* hw);              // e.g. "ca120", "ddf550", "ddf1gtx"
```

This is a thin function — it does not know anything about XML syntax or JSON
mirroring itself. It just:

1. Calls `parse_xml(xml, len)` (Stage 1).
2. If that failed → builds the **malformed envelope**:
   `{"hw":<hw>,"channel":"xml","msg_kind":"malformed","parse_error":<message>,"parse_offset":<offset>}`
   and returns immediately (no `body` key at all on this path).
3. If it succeeded → decides `msg_kind`:
   - root tag is literally `"Event"` → always `"event"`, **regardless of
     `frame_kind_hint`**
   - otherwise → `"request"` if `frame_kind_hint == "request"`, else `"reply"`
4. Calls `node_to_json(root, 0, budget)` (Stage 2) to mirror the whole tree.
5. Wraps it: `{"hw":<hw>,"channel":"xml","msg_kind":<kind>,"body":{<snake_root_tag>:<mirrored_value>}}`.

**Never crashes, never throws out of this function.** The *entire* body of
`parse_xml_to_json` is a `try { ... } catch (...) { return an
"internal_error" record; }`. In practice nothing in Stages 1-2 should ever
throw (no manual `new`, only `std::string`/`std::vector`, so the only
theoretical exception is `std::bad_alloc`) — this catch-all exists purely as
a last-resort ABI guarantee, matching how the eventual production
`ca120_parser.cpp` integration is required to never let a C++ exception
escape an `extern "C"` boundary.

Both `frame_kind_hint` and `hw` are null-safe (`hw ? hw : "unknown"`,
`frame_kind_hint && ...`) — a caller passing `nullptr` for either gets a
sane fallback, not a crash.

---

## 6. Data flow, end to end

```
   raw XML bytes
        |
        v
  parse_xml()                     <- Stage 1 (xml_parser.cpp)
        |
        v
  XmlParseResult { ok, root, error, error_offset }
        |
   ok? -+-- no --> build malformed envelope, DONE
        |
       yes
        |
        v
  node_to_json(root, 0, budget)    <- Stage 2 (json_mirror.cpp)
        |
        v
  JSON string (the mirrored body, no envelope yet)
        |
        v
  wrap in {"hw":...,"channel":"xml","msg_kind":...,"body":{...}}
        |
        v
  final JSON string returned to caller
```

---

## 7. Building and running the tests

No CMake, no test framework — matches the existing `proto_ca120_generic_mirror.cpp`
convention: plain `g++`, standalone `main()`-based harnesses that print
`ALL TESTS PASSED` (exit 0) or `FAIL: <message>` lines + `N FAILURE(S)` (exit 1).

From the **repo root** (`g++` from MSYS2/MinGW, confirmed C++17-capable):

```bash
# Stage 1 alone
g++ -std=c++17 -Wall -Wextra -I drs-bridge/parsers/utils/xml_to_json \
    -o drs-bridge/parsers/utils/xml_to_json/test_xml_parser.exe \
    drs-bridge/parsers/utils/xml_to_json/xml_parser.cpp \
    drs-bridge/parsers/utils/xml_to_json/tests/test_xml_parser.cpp
./drs-bridge/parsers/utils/xml_to_json/test_xml_parser.exe

# Stage 2 alone
g++ -std=c++17 -Wall -Wextra -I drs-bridge/parsers/utils/xml_to_json \
    -o drs-bridge/parsers/utils/xml_to_json/test_json_mirror.exe \
    drs-bridge/parsers/utils/xml_to_json/json_mirror.cpp \
    drs-bridge/parsers/utils/xml_to_json/tests/test_json_mirror.cpp
./drs-bridge/parsers/utils/xml_to_json/test_json_mirror.exe

# Stage 3, full end-to-end (needs all 3 .cpp files)
g++ -std=c++17 -Wall -Wextra -I drs-bridge/parsers/utils/xml_to_json \
    -o drs-bridge/parsers/utils/xml_to_json/test_xml_to_json.exe \
    drs-bridge/parsers/utils/xml_to_json/xml_parser.cpp \
    drs-bridge/parsers/utils/xml_to_json/json_mirror.cpp \
    drs-bridge/parsers/utils/xml_to_json/xml_to_json.cpp \
    drs-bridge/parsers/utils/xml_to_json/tests/test_xml_to_json.cpp
./drs-bridge/parsers/utils/xml_to_json/test_xml_to_json.exe
```

The built `.exe` files are gitignored (see `.gitignore` in this folder) —
safe to rebuild and delete freely, never commit them.

---

## 8. Debugging checklist

**"My XML didn't parse the way I expected."**
1. Call `parse_xml()` directly on just the fragment you care about (not the
   whole message) — smaller input, smaller search space.
2. Check `result.ok` first. If `false`, `result.error` + `result.error_offset`
   tell you exactly where and why (see §3's `fail()` note above).
3. If `result.ok == true` but the tree looks wrong, print `result.root.name`,
   `.attributes`, `.children.size()`, `.text` — and recurse into
   `.children[i]` by hand. This isolates whether the bug is in Stage 1.

**"The JSON output looks wrong but the XML parsed fine."**
1. That means the bug is in Stage 2. Build the exact `XmlNode` you're seeing
   (either print it from Stage 1, or construct one by hand in a scratch test
   like `tests/test_json_mirror.cpp` does) and call `node_to_json` on it
   directly — no XML text involved at all.
2. Most common gotchas: is a tag name colliding after `to_snake_case`
   (two differently-named XML tags reducing to the same snake_case key)?
   Is text non-whitespace but you expected `{}` (or vice versa)?

**"The envelope's `hw`/`msg_kind`/`channel` fields look wrong."**
That's entirely in `xml_to_json.cpp` — a ~50-line file, read the whole thing,
the logic is a few `if`s, not a search problem.

**"A large/complex real-world message got truncated."**
Check `MAX_DEPTH`/`MAX_NODES` in `json_mirror.cpp` (currently 32 / 2000) —
a `{"truncated":"true","node_count":N}` fragment anywhere in the output means
the mirror gave up at that point. This is a soft limit, not a bug; raise the
constants if a real message legitimately needs more (no ICD documents a
required maximum, these were chosen for parity with the original prototype).

**"Something crashed instead of returning `ok=false`."**
This should be impossible through `parse_xml_to_json` (the public entry
point) — if you find a real crash, it's a genuine bug worth fixing, since
"never crashes" is this module's core contract. `parse_xml()` called
directly has the same guarantee (confirmed via `tests/test_xml_parser.cpp`'s
`test_null_xml_does_not_crash` and `test_depth_guard_no_crash`).

---

## 9. Known, deliberate limitations (not bugs)

- Namespaces (`<ns:Tag>`) and entity references (`&amp;`) are silently
  accepted rather than rejected as parse errors, even though the design spec
  originally called for rejecting them. Confirmed neither construct appears
  in any real ICD (CA120, DDF-550, DDF-1GTX) — see design spec §2 for the
  full reasoning.
- No CMake/build-system integration yet — this is intentional for the
  standalone validation phase (§1 of the design spec).
- Not wired into any production parser DLL (see the top of this file).
