# From-scratch XML→JSON parser (`drs-bridge/parsers/utils/xml_to_json/`) — Design

**Status:** superseded for CA120/DDF-550/DDF-1GTX production `parse_message`
by [xml-parsing-pugixml-migration-design.md](xml-parsing-pugixml-migration-design.md)
(2026-07-16) — those three parsers now use pugixml for content parsing
instead of the hand-written module below. The module itself
(`drs-bridge/parsers/utils/xml_to_json/`) remains committed and tested but
is not used in production; this doc is kept for historical rationale.
**Audience:** the C++ developer building the CA120 / DDF-550 / DDF-1GTX XML control-message support; whoever reviews the parser-utils layer.
**Decision asked:** approve the module boundaries, parsing scope, and test corpus below before implementation starts.

---

## 1. Why this exists

Three drs-bridge parsers speak XML control messages on the wire: CA120, DDF-550, DDF-1GTX. All three ICDs use the same shape — a `<Request>`/`<Reply>`/`<Event>` (or `<RDFS>`) envelope with attributes, nested elements, and repeated sibling tags. Today each parser hand-rolls its own ad-hoc tag-scanning helpers (`xml_closing_end`/`xml_attr`/`xml_text`/`xml_has`), duplicated per file, each only pulling a handful of known fields rather than mirroring the full document.

`drs-bridge/parsers/utils/proto_ca120_generic_mirror.cpp` already validated a generic XML→JSON *mirror* design against real CA120 examples (see its header comment for the accepted trade-offs: snake_case keys, array-on-repeated-tag, `{}` for empty elements, no `raw_xml` fallback). That prototype uses pugixml (a full ~13k-line third-party DOM library) purely to get a tree to walk. Every other XML-touching parser in this repo deliberately avoids pulling in a library (see the "Helpers: XML scanning (no LGPL)" comments in `ddf550_parser.cpp` / `ddf1gtx_parser.cpp`) — production parser DLLs stay dependency-free.

This module replaces the pugixml dependency with a small hand-written parser scoped to exactly what real ICD messages use, while keeping the already-validated JSON mirror design unchanged.

**Scope note:** this is built and tested standalone first. Whether/how it gets wired into `ca120_parser.cpp`, `ddf550_parser.cpp`, or `ddf1gtx_parser.cpp` (replacing their duplicated ad-hoc helpers) is a separate decision, made after this module proves itself.

---

## 2. What the parser must handle

Confirmed against real samples in `proto_ca120_generic_mirror.cpp`'s test cases and the DDF-550 / DDF-1GTX ICDs — three structural primitives (attributes, nesting, repeated-tag-as-array), which combine into seven concrete shapes:

| # | Shape | Example |
|---|---|---|
| 1 | Attribute-only element (no text/children) | `<Register type="processingUnit"/>` |
| 2 | Text-only leaf (no attributes) | `<Hostname>sukSEs-JTO</Hostname>` |
| 3 | Attribute + text together (`#text` case) | `<Input sourceType="tuner">{guid}</Input>` |
| 4 | Nested element, single child → JSON object | `<Register><GUID>...</GUID></Register>` |
| 5 | Repeated sibling tag → JSON array | 3× `<IP>` under `<ProcessingUnitStatusChange>`; 2× `<Control>` under `<Request>` |
| 6 | Empty element → `{}` | `<Lock/>` and `<Lock></Lock>` (self-closing is sugar for immediate open+close — both must parse identically) |
| 7 | Malformed / mismatched tags | `<Request...><Tuner></Request>` — must return an error, never crash |

**Explicitly out of scope** — none of these appear in any real sample across CA120, DDF-550, or DDF-1GTX:

- CDATA sections, comments (`<!--`), `<?xml ?>` / processing-instruction declarations — actively rejected as parse errors (checked at the start of every tag).
- XML namespaces (`<ns:Tag>`) and entity references (`&amp;` etc.) — **not** actively rejected. Confirmed absent from every reviewed ICD, so the parser is permissive here rather than spending complexity guarding against input that cannot occur: a namespaced tag name is accepted with the prefix as a literal part of the name, and a bare `&` is copied through as a literal character rather than erroring. This is a deliberate scope reduction (decided during Task 1 review), not an oversight — revisit only if a future hardware variant's ICD is found to use either.

---

## 3. Module structure

```
drs-bridge/parsers/utils/xml_to_json/
  xml_node.h          struct XmlNode { name, attributes (ordered pairs), children, text }
  xml_parser.h/.cpp   XML text -> XmlNode tree (hand-written recursive descent)
  json_mirror.h/.cpp  XmlNode tree -> JSON string (ports node_to_json/node_object_body
                      from proto_ca120_generic_mirror.cpp onto XmlNode instead of
                      pugi::xml_node; same validated design, unchanged)
  xml_to_json.h       single public entry point
  tests/
    test_xml_to_json.cpp   standalone harness, all 7 shapes + malformed case,
                            real samples from CA120 + DDF-550 + DDF-1GTX ICDs
```

Two independently testable stages: text→tree, tree→JSON. This mirrors the prototype's own split (parse vs. mirror) closely enough that `json_mirror.cpp` is close to a direct port, minimizing risk of accidentally changing the already-validated JSON shape.

### Entry point

```cpp
// hw: the value to stamp into the envelope's "hw" field (e.g. "ca120",
// "ddf550", "ddf1gtx") -- supplied by the caller, not hardcoded here. Each
// hardware variant's own parser file (ca120_parser.cpp, ddf550_parser.cpp,
// ddf1gtx_parser.cpp) knows its own name and passes it in; this shared
// utility stays hardware-agnostic rather than hardcoding one variant's name,
// consistent with the module's goal of not being CA120-specific.
//
// frame_kind_hint: "request" or "reply" -- selects msg_kind when the root
// tag isn't <Event> (an <Event> root always yields msg_kind "event"
// regardless of the hint).
//
// Returns the JSON string directly (envelope on success, or a
// msg_kind:"malformed"/"internal_error" record on failure) -- errors are
// folded into the JSON shape itself rather than a separate result struct,
// matching the prototype's parse_xml_to_json() convention.
std::string parse_xml_to_json(const char* xml, size_t len, const char* frame_kind_hint, const char* hw);
```

No exceptions escape this function (matches the ABI rule already documented in the prototype: "no C++ types cross the boundary, no exceptions escape"). Internally it uses only `std::string`/`std::vector` — no manual `new`/`delete` — so the only possible exception is `std::bad_alloc`, same risk profile as the rest of the codebase; the eventual production caller wraps the call in `try/catch(...)` exactly as the prototype's header comment already specifies for the real `ca120_parser.cpp` integration. A null `xml` pointer (with `len > 0`) is also handled without crashing — checked explicitly before any dereference, since a null-pointer dereference is a SIGSEGV, not a C++ exception, and would not be caught by the `try/catch`.

---

## 4. Parsing rules

- Recursive-descent, single forward pass over the input bytes, tracking a byte offset for error reporting.
- Start tag: `<Name attr="value" attr2="value2">` — attribute values may use `"` or `'` as the quote character (ICDs are consistent but this costs nothing to support).
- Self-closing tag: `<Name .../>` — produces the same `XmlNode` as `<Name ...></Name>` with no children/text.
- End tag: `</Name>` — must match the innermost open tag by name; mismatch is a parse error at the offset of the offending tag.
- Text content: raw bytes between `>` and the next `<`, preserved **verbatim** — no trimming of any kind. Matches the prototype exactly: it never trims text, it only checks whether the text is entirely whitespace (to decide `{}` vs. a real string in the mirror stage) while keeping the original bytes intact either way.
- Parser-side nesting guard: the recursive-descent parser itself tracks nesting depth and returns a parse error (not a crash) once it exceeds `MAX_DEPTH = 32` — this guards the tree-construction recursion (`parse_element`/`parse_content`) against a stack overflow on a pathological, even well-formed, deeply-nested document. This is distinct from and in addition to the mirroring-pass guard below: no ICD documents a maximum XML nesting depth, so `32` was chosen for consistency with the mirror's existing constant, not derived from a spec value.
- Mirror-side nesting guard: `MAX_DEPTH = 32` / node-count guard `MAX_NODES = 2000` on the JSON-mirroring pass (same constants as the prototype) — exceeding either is treated the same way the prototype treats it (truncated marker), not a hard error, since a well-formed-but-huge document isn't malformed.
- Comments/CDATA/`<?xml?>` declarations are parse errors at the offset encountered, not silently passed through (see §2's out-of-scope list for the full list of what is and isn't actively rejected).

---

## 5. JSON mirror (unchanged from the validated design)

Ported as-is from `proto_ca120_generic_mirror.cpp`:

- Envelope: `{hw, channel, msg_kind, body: {<root_tag_snake>: {...mirror...}}}`
- Keys: XML tag/attribute names → snake_case
- Attributes merge into the same object as plain snake_case keys (no `@` prefix); a child element sharing the same key name overwrites the attribute (last-write-wins), matching the prototype's documented trade-off
- Repeated siblings → array only when count > 1
- Values are always JSON strings, never coerced to numbers
- Empty element → `{}` (plus any attribute keys)
- `hw` in the envelope is the caller-supplied value (see §3), not a hardcoded literal — the mirror logic itself has no notion of which hardware variant it's mirroring
- Malformed XML → error record (`msg_kind`/`parse_error`/`parse_offset`), no `raw_xml` fallback field
- Internal exception caught at the `parse_xml_to_json` boundary → `msg_kind:"internal_error"` record (see §3) — not expected in normal operation since the module has no code path that throws for well-formed C++ execution, but present as a defense-in-depth ABI-boundary guarantee

---

## 6. Testing

`tests/test_xml_to_json.cpp` is a standalone harness (no test framework dependency, matching the prototype's own `main()`-based harness). It asserts:

1. All 7 shapes from §2 produce the expected JSON, using the actual XML strings already in `proto_ca120_generic_mirror.cpp`'s `cases` vector (kept byte-for-byte identical, so this doubles as a parity check against the pugixml-based prototype's output).
2. At least one real `<Request>`/`<Reply>` sample transcribed from `icd-ddf550.md` and `icd-ddf1gtx.md`, proving the parser isn't CA120-specific.
3. The malformed-XML case returns `ok == false` with a non-empty `error` and a plausible `error_offset` (pointing at or near the mismatched tag), not a crash or empty output.

No regression framework (pytest/gtest) is introduced for this standalone phase — matches the prototype's own build/run convention (a `.exe` you build and read console output from). If/when this module gets wired into a real parser DLL, it picks up that parser's existing test framework (see `drs-bridge/parsers/ca120/tests/test_frames_ca120.cpp` for the pattern).

---

## 7. Open questions

- Duplicate attribute on the same element (e.g. `<Tag a="1" a="2">`) — confirmed absent from every reviewed ICD (CA120, DDF-550, DDF-1GTX). Resolved: last-value-wins (same policy as the attribute/child-key collision rule in §5), not treated as an error. No further action needed.
- `hw` parameterization — resolved during the final whole-branch review: `parse_xml_to_json` takes `hw` as an explicit parameter rather than hardcoding `"ca120"`, since this is a shared utility and each hardware variant's own parser file (which already has its own `parse_xml`/`parse_message` per §1) knows its own name and passes it in. No further action needed.
- Null `xml` pointer with `len > 0` — a genuine crash path found during the final review (`parse_xml(nullptr, N)` would dereference null before any length check caught it). Resolved: guarded explicitly at the top of `parse_xml`, returning a parse error instead of crashing. No further action needed.
