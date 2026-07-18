# XML content-parsing: pugixml migration (CA120 / DDF-550 / DDF-1GTX) — Design

**Status:** design — approved by Haris Manzar, 2026-07-16.
**Supersedes:** the "reject pugixml, hand-roll instead" decision in
[xml-to-json-parser-design.md](xml-to-json-parser-design.md), for the three
production parsers listed below only. That doc's hand-written module
(`drs-bridge/parsers/utils/xml_to_json/`) and its production validation
candidate (`drs-bridge/parsers/ca120/src/CA120_parser_new.cpp`) remain
committed, already tested — but are explicitly **not** adopted by this
change. See §7.

**Update 2026-07-17:** §1's "this is not the same decision as adopting the
generic XML→JSON mirror shape... out of scope here" no longer holds — see
[rdfs-generic-mirror-json-contract.md](rdfs-generic-mirror-json-contract.md).
CA120/DDF-550/DDF-1GTX were subsequently migrated to the generic-mirror
shape after all, for family-wide JSON structural consistency. This doc's
pugixml-as-parsing-engine decision (§1's actual `Decision:` line) is
unaffected — only the "keep the curated JSON output contract unchanged"
clause was overridden.

**Audience:** the C++ developer replacing each parser's ad-hoc XML scanning
with pugixml; whoever reviews the parser-utils layer changes.

**Decision asked:** approve replacing the hand-rolled `xml_attr`/`xml_text`/
`xml_has` scanning helpers in `ca120_parser.cpp`, `ddf550_parser.cpp`, and
`ddf1gtx_parser.cpp` with pugixml-based DOM navigation, while keeping each
file's existing `parse_message` JSON output contract byte-for-byte
unchanged.

---

## 1. Why (and why this is not a wire-format change)

Reassessed against the reasoning in the superseded spec:

- **Performance:** parsing a single XML control message (attributes/nesting
  typical of these ICDs) costs microseconds either way — negligible next to
  Kafka/DB latency elsewhere in the pipeline. Not a real differentiator
  either direction.
- **Licensing:** `parser_api.h` rule 3 / `sdfc_abi.h`'s ABI rule literally
  says "no LGPL dependencies." pugixml is MIT/zlib-licensed and was never
  actually excluded by that rule's text — the "stay fully dependency-free"
  reading was a stricter, self-imposed convention layered on top, and the
  team is now explicitly relaxing it for this one well-understood,
  permissively-licensed library.
- **Maintenance:** today's three ICDs use a small, bounded XML subset (see
  §2 of the superseded spec), which the hand-rolled parser matches exactly
  — but pugixml already handles every construct that spec explicitly
  excludes (comments, CDATA, PIs, namespaces, entities) for free, so a
  future ICD revision that introduces any of those needs zero new parser
  code with pugixml vs. a from-scratch extension of the recursive descent.
- **Decision:** use pugixml as the XML parsing engine for CA120, DDF-550,
  and DDF-1GTX's XML control channels.

**This is explicitly not the same decision as adopting the generic
XML→JSON mirror shape** (`{hw, channel, msg_kind, body: {...full
tree...}}`) that `xml_to_json/` and `CA120_parser_new.cpp` already
validate. Per `CLIENT_STATUS_REPORT_2026-07-06.md` §4.5, CA120/DDF-550's
*current* curated-field decode output is already reported to the client as
"complete and working" — only the encode side (`format_response`) is
flagged as unfinished. Switching to the generic mirror shape would change
that JSON contract for any existing consumer (drs-server, Kafka
consumers, webapp) and is **out of scope here**. This design only replaces
the parsing engine underneath an unchanged output contract.

---

## 2. Scope

**In scope:** `ca120_parser.cpp`, `ddf550_parser.cpp`, `ddf1gtx_parser.cpp`
— `parse_message`'s content-parsing internals only.

**Out of scope:**
- `format_response` — already library-free and unaffected (§5).
- `extract_frame`'s frame-*boundary* detection — stays hand-rolled (§4).
- `drs-bridge/parsers/utils/xml_to_json/` and `CA120_parser_new.cpp` — left
  as committed, tested, unused-in-production artifacts (§7).
- Any other hardware variant (MONVUHF/ESME, the binary `cpp/rdfs/`
  parser, dp_ecm, etc.) — none of these have an XML channel today.

---

## 3. What changes per file

For each of the three files:

- **Delete:** the file's `Helpers: XML scanning (no LGPL)` block —
  `xml_attr()`, `xml_text()`, `xml_has()` (content-scanning helpers only;
  see §4 for `xml_closing_end()`).
- **Replace:** the content-parsing function (`parse_xml()` in
  `ca120_parser.cpp`, `parse_xml_ddf550()` / `parse_xml_ddf1gtx()` in the
  other two) — instead of `xml_attr(xml, frame_len, "type")`, parse the
  frame once with `pugi::xml_document doc; doc.load_buffer(xml,
  frame_len);` and navigate the tree: `doc.first_child().attribute("type").value()`,
  `.child("IP").text().get()`, `doc.first_child().child(tag)` (truthy) in
  place of `xml_has`.
- **Keep unchanged:** every JSON field name/shape currently emitted
  (`msg_type`, `subsystems`, `datastream_action`, `stream_ip`,
  `frequency_hz`, `app_guid`, `status`, `bandwidth_hz`, `raw_xml`, …), the
  `JsonWriter` usage, the `msg_kind` detection logic (root tag →
  request/reply/event), and the table-driven "which subsystem tags are
  present" check (`kSubsystems` in CA120) — this becomes a `doc.first_child().child(tag)`
  truthiness check per table entry instead of `xml_has`.
- **ABI safety:** pugixml's `load_buffer` returns a result object and does
  not throw on malformed XML, but the surrounding `std::string`/`JsonWriter`
  work could theoretically raise `std::bad_alloc`. Wrap the rewritten
  content-parsing call in `try { ... } catch (...) { return empty/-1
  path; }` per the no-exceptions-across-the-ABI-boundary rule already
  documented in `sdfc_abi.h` and applied in `xml_to_json.cpp` — add this
  explicitly if the current file doesn't already have an equivalent guard
  around its content-parsing call.

### 3.1 Policy: pre-existing scoping bugs uncovered by the migration

CA120's migration surfaced a real, pre-existing bug this way: the old
`xml_attr(xml, frame_len, "type")` call for the `DataStream` block searched
the **entire raw buffer** for the first `type="..."` substring, not scoped
to the `DataStream` node — since the root element's own `type` attribute
appears earlier in the byte stream than `DataStream`'s, `datastream_type`
had always silently returned the root message's own type ("get"/"set")
instead of DataStream's actual type ("IFData"). pugixml's node-scoped
`.attribute("type")` fixes this as a natural side effect of correct
scoping, breaking the literal "byte-identical" goal in this one case (see
`ca120_parser.cpp`, `impl_parse_xml`, the `DataStream` block, for the
disclosure comment; `test_xml_datastream_reply` locks in the corrected
value with an explicit assertion).

**Decided policy (2026-07-16, Haris Manzar):** where the migration to
node-scoped pugixml lookups incidentally fixes this class of pre-existing
unscoped-search bug (the old code's "first match anywhere in the buffer"
happened to read a different element's attribute/tag than intended),
**accept the corrected behavior** rather than deliberately reproducing the
old bug — but every such case must be (a) called out with an inline code
comment explaining the old-vs-new discrepancy and why it's accepted, and
(b) locked in with an explicit test assertion on the corrected value, so
it's a disclosed, verified fix rather than a silent, accidental one. This
applies to DDF-550 and DDF-1GTX's migrations too, if the same
unscoped-vs-scoped pattern surfaces there (e.g. any `xml_attr`/`xml_text`
call reading an attribute/tag name that could plausibly collide with the
same name used elsewhere in the same document, ahead of the intended
element in document order).

### 3.2 Disclosed deviation: pugixml decodes entities/EOL/attribute whitespace, the old scanning never did

Found by the final whole-branch review (2026-07-16), across all three
files, not scoped to one parser: pugixml's default parse flags
(`pugi::parse_default` includes `parse_escapes`, `parse_eol`,
`parse_wconv_attribute`) decode XML entities (`&amp;` → `&`, `&#nn;` →
the corresponding character), normalize `\r\n`/`\r` line endings to `\n`,
and normalize whitespace in attribute values. The deleted hand-rolled
helpers (`xml_attr`/`xml_text`/`xml_all_params`/`xml_all_dfdata_fields`/
`xml_param_value`/`tag_or_param`) returned **raw, undecoded substring
bytes** — no entity decoding, no EOL normalization. So for any field or
attribute value that happens to contain an entity reference or an
embedded CR, the new JSON differs from the old.

**Accepted per the same policy as §3.1**, for the same reason: this is a
side effect of using a real, correct XML parser instead of ad-hoc byte
scanning, not a mistake. Impact is low in practice — every real sample
across CA120/DDF-550/DDF-1GTX's ICDs is entity-free (enums, integers,
IPs, GUIDs, bearings) — but it is a genuine, previously-undisclosed
fourth exception to the "byte-identical" invariant, on top of the three
already documented (CA120 `datastream_type`, DDF-550's DFData
`node_element` filter, DDF-1GTX's Command-name `strstr` overrun). Note
also that `raw_xml` (kept in CA120/DDF-1GTX's output) still stores the
original *encoded* bytes verbatim — it is not run through pugixml — so a
curated field and `raw_xml` can disagree on encoding for the same
underlying value if a future ICD sample ever contains an entity.

Two narrower, related, low-impact scoping differences noted at the same
time (not requiring their own §3.1-style disclosure/test pair, since
they're refinements of the *same* class of "unscoped byte-scan vs
node-scoped pugixml" difference already covered by that policy, just
without a fixture that demonstrates a concrete wrong-vs-right value the
way `datastream_type` did):
- DDF-550: a self-closing `<Tag/>` direct child of `DFData` now emits an
  empty-string field (`"Tag":""`) via the `node_element`-filtered loop,
  whereas the old `xml_all_dfdata_fields` explicitly skipped self-closing
  children (no field added at all). No current DFData fixture uses a
  self-closing field, so this is unexercised in practice.
- CA120: `DataStream`'s `IP`/`Port` lookup narrowed from
  `xml_text(xml, frame_len, "IP")` (whole-buffer search) to
  `ds.child("IP")` (direct-child-only) — more correct, matches every
  current fixture, but would differ from the old behavior if a future
  message ever nested `IP`/`Port` deeper than a direct child of
  `DataStream`.

---

## 4. `extract_frame`: frame-boundary detection stays hand-rolled

`xml_closing_end()` (byte-search for the literal `</Tag>` sequence) is
**frame-boundary detection**, a different concern from content parsing —
`extract_frame` is called repeatedly as bytes accumulate on a partial
frame, and needs a cheap "is this frame complete yet, or do we need more
bytes" check that doesn't require a wasted full pugixml parse attempt per
partial chunk, and pugixml's parse-result status codes don't cleanly
distinguish "incomplete input" from "genuinely malformed." Recommendation:
keep `xml_closing_end()` / root-tag detection exactly as-is in all three
files for `extract_frame`; only the content-parsing step inside
`parse_message` moves to pugixml. This mirrors the split already
documented in `CA120_parser_new.cpp`'s own header comment ("Frame
*boundary* detection is a separate concern from frame *content* parsing"),
just with pugixml as the content side instead of `xml_to_json`.

---

## 5. `format_response` — no change

All three files' `format_response` already follow the same safe pattern:
the caller (drs-server) supplies a pre-built, JSON-escaped XML fragment as
an `xml_body` string field, and `format_response` only template-wraps it
(`snprintf` + envelope tags + binary frame markers where applicable). No
XML or JSON library is involved, and it never attempts to generically
reverse the (lossy) decode-side field extraction. Confirmed identical in
`ca120_parser.cpp`, `ddf550_parser.cpp`, and `ddf1gtx_parser.cpp`. Nothing
here needs to change.

---

## 6. Build system

There is no top-level `drs-bridge/parsers/CMakeLists.txt` aggregating the
variant subdirectories — each of `ca120/`, `ddf550/`, `ddf1gtx/` is built
as its own standalone CMake project. Two options, given that:

- **Option A (recommended):** add `pugixml-1.16/pugixml.cpp` directly to
  each variant's `add_library(... SHARED ...)` and `add_executable(test_...
  ...)` source lists (same duplication pattern already used for
  `target_include_directories(... PRIVATE ../dp_ecm/include)` across these
  files). Simplest, no new CMake subdirectory wiring, at the cost of
  compiling `pugixml.cpp` up to 6 times total (3 variants × DLL + test
  executable) instead of once.
- **Option B:** a shared `add_library(pugixml STATIC ...)` target under
  `parsers/utils/`, pulled in via `add_subdirectory(../utils/pugixml
  ${CMAKE_CURRENT_BINARY_DIR}/pugixml)` from each variant's CMakeLists
  (needs an explicit out-of-tree binary dir since these are independent
  projects, not a shared parent build). Builds `pugixml.cpp` once per
  variant project invocation still (no cross-project artifact caching
  without a top-level aggregator), so the real saving over Option A is
  organizational (one place to bump the pugixml version), not build time.

Recommend **Option A** given there's no existing shared-build
infrastructure to hook into cleanly, and the existing convention in this
part of the repo already favors per-variant duplication over shared CMake
targets. Either way: `target_include_directories(<variant> PRIVATE
../utils/pugixml-1.16)` for both the DLL and its test executable, and the
existing non-MSVC static-linking flags (`-static-libgcc -static-libstdc++
-static`, needed for `ctypes.CDLL()` loadability) continue to apply
unchanged — pugixml adds no new runtime DLL dependency.

---

## 7. Docs/ABI comments to update in the same change

- `parser_api.h` rule 3 and the equivalent comment in `sdfc_abi.h` ("No
  LGPL dependencies linked into this DLL") — clarify that pugixml
  (MIT/zlib) is an explicit, approved exception; the rule's intent is "no
  copyleft/LGPL," not "zero third-party code."
- Each of the three files' `Helpers: XML scanning (no LGPL)` header
  comment — replace with a note that content parsing now uses pugixml
  (linked per §6), while `extract_frame`'s frame-boundary detection
  remains hand-rolled byte scanning (§4).
- [xml-to-json-parser-design.md](xml-to-json-parser-design.md) — add a
  status note at the top pointing to this doc as superseding it for
  CA120/DDF-550/DDF-1GTX production `parse_message`; the hand-rolled
  `xml_to_json/` module remains committed and tested but is not used in
  production.
- `drs-bridge/parsers/utils/xml_to_json/README.md` — same superseded note.
- `CA120_parser_new.cpp`'s header comment — clarify it remains a
  validation candidate for the generic-mirror shape only, not adopted;
  since the real `ca120_parser.cpp` now also uses pugixml (for curated
  fields, not the mirror), note the distinction so a future reader doesn't
  conflate the two.
- [docs/ewtss/README.md](../README.md)'s index entry for
  `xml-to-json-parser-design.md` — update in the same change per this
  repo's "keep README indexes current" convention, and add this new spec
  alongside it.

---

## 8. Testing

`test_frames_ca120.cpp`, `test_frames_ddf550.cpp`, and
`test_frames_ddf1gtx.cpp` already assert expected JSON output for real ICD
sample frames against the *current* ad-hoc-scanning implementation. Since
the output contract is unchanged by this design, these become
parity/regression tests for the pugixml rewrite:

1. Run the existing suites as a baseline (all passing today).
2. Rewrite each file's content-parsing internals to pugixml.
3. Re-run the same suites — expect byte-identical JSON output for every
   existing fixture, no test changes needed.
4. Add new fixtures only for pugixml-specific edge cases not already
   covered (e.g. attribute-order independence, whitespace-only text
   nodes, self-closing tags) if the current suite has gaps there.

No new test framework — matches the existing plain-assert, no-dependency
convention already used by these three test files.

---

## 9. Open items for the implementation plan (not resolved here)

- Whether `pugi::xml_node::text().get()` semantics exactly match each
  file's `xml_text()` behavior (first match between open/close tags) for
  every tag actually read (`IP`, `Port`, `Status`, `Frequency`,
  `Bandwidth`, etc.) — verify per-tag during implementation, not assumed
  here.
- `CA120`'s `raw_xml` cap (16384 bytes) is unaffected by this change and is
  not revisited here — no evidence it needs to change now that pugixml
  makes full-tree access cheap.
- Final choice between CMake Option A/B (§6) — recommend A, confirm during
  implementation.
