# RDFS-family JSON contract: the generic XML→JSON mirror (CA120 / DDF-550 / DDF-1GTX)

**Status:** implemented — decided by Haris Manzar, 2026-07-17.
**Supersedes:** the "keep each parser's existing curated JSON output contract
unchanged" clause in
[xml-parsing-pugixml-migration-design.md](xml-parsing-pugixml-migration-design.md)
§1, for CA120/DDF-550/DDF-1GTX only. That doc's "use pugixml as the parsing
engine" decision still stands unchanged — this doc only changes what JSON
gets built *from* the parsed tree.

**Audience:** drs-server engineers, Kafka-consumer authors, DRS webapp
developers — anyone reading `parse_message()` output from `ca120.dll`,
`ddf550.dll`, or `ddf1gtx.dll` on their XML control channel.

---

## 1. Why this changed

CA120, DDF-550, and DDF-1GTX are grouped together as the "RDFS" hardware
family, but until this change their `parse_message` JSON shapes had nothing
in common: CA120 hand-picked ~10 named fields (`subsystems`,
`datastream_action`, `frequency_hz`, `raw_xml`...), DDF-550 used a
`command_name` + generic `params{}` map, and DDF-1GTX individually
hardcoded ~15 named fields per known ICD parameter
(`operation_mode`, `frequency_hz`, `att_select`, ...) plus its own
`raw_xml`. Three different, independently-maintained shapes for three
members of the same family — every consumer had to special-case each
device instead of writing one shared code path.

All three now emit **the same generic tree-mirror algorithm** — same
snake_case rule, same attribute/child merge rule, same array rule, same
malformed-XML shape — implemented once in
[`drs-bridge/parsers/utils/pugixml_generic_mirror.h/.cpp`](../../../drs-bridge/parsers/utils/pugixml_generic_mirror.h)
and called from all three parsers' `impl_parse_xml_*` functions. Nothing
in the wire XML is dropped or hand-curated anymore — the entire parsed
tree is mirrored into JSON, so a future ICD revision that adds a new
`<Param>` or subsystem element shows up automatically, with no parser code
change required.

**Trade-off, stated plainly:** this breaks the JSON contract every existing
consumer of these three parsers currently reads. There is no
backward-compatible field mapping — `subsystems`, `datastream_action`,
`command_name`, `params`, `operation_mode`, `raw_xml`, etc. do not exist
anywhere in the new output. §5 below gives the old→new mapping to help
migrate existing consumer code.

---

## 2. The envelope

Every one of the three parsers' `parse_message()` returns one of two
shapes on the XML control channel:

**Success:**
```json
{"hw": "<ca120|ddf550|ddf1gtx>", "channel": "<device-specific, §4>",
 "msg_kind": "<device-specific, §4>",
 "body": {"<snake_case_root_tag>": { /* mirrored tree, §3 */ }}}
```

**Malformed XML** (well-framed — `extract_frame` found a matching closing
tag — but not well-formed inside, e.g. a mismatched tag):
```json
{"hw": "<...>", "channel": "<...>", "msg_kind": "malformed",
 "parse_error": "<pugixml's error description>",
 "parse_offset": <byte offset into the frame where parsing gave up>}
```
Note: **no `"body"` key at all** on the malformed path — parsing never got
far enough to have a tree to mirror. `channel` and `msg_kind` are still
correct even here (they're derived from the root tag name found by
byte-scanning in `extract_frame`/`parse_message`, before pugixml ever
runs) — but do not assume `body` exists without checking `msg_kind` first.

`parse_message` itself still only returns `-1` for input that isn't
recognizable as this device's XML at all (unknown root tag). Once the
root tag is recognized, the call always succeeds (`0`) and any error
shows up as the `msg_kind: "malformed"` record above — a message is never
silently dropped just because its content is invalid.

---

## 3. The mirror algorithm — how one XML node becomes JSON

This is the part that's now **identical** across all three parsers. Given
one `pugi::xml_node`:

1. **Empty element** (no attributes, no child elements, no non-whitespace
   text) → `{}`.
2. **Pure leaf** (no attributes, no child elements, but has non-whitespace
   text) → the text as a **plain JSON string** — not wrapped in an object.
   E.g. `<EmitterClass>Burst</EmitterClass>` → `"Burst"`.
3. **Everything else** (has attributes and/or child elements) → a JSON
   object, built as:
   - every **attribute** becomes a key: `snake_case(attr_name)` →
     `attr_value` (always a string).
   - every **child element** becomes a key: `snake_case(tag_name)` → the
     mirrored value of that child (recursing into rule 1/2/3 for it).
     - a tag that appears **once** under this parent → a scalar/object key.
     - a tag that appears **2 or more times** under this parent → a JSON
       **array** of that many mirrored values. *A single occurrence is
       never wrapped in an array* — see §6.1, this is the one gotcha every
       consumer must handle.
   - attributes are inserted first, then children — if a child element's
     snake_case name ever collided with an attribute's (not observed in
     any real ICD sample across the family), the child's value wins.
   - if the node has attributes but **no** child elements, and it also has
     non-whitespace direct text, a synthetic `"#text"` key carries that
     text. E.g. `<Param name="eOperationMode">DFMODE_FFM</Param>` →
     `{"name": "eOperationMode", "#text": "DFMODE_FFM"}`.
4. **snake_case conversion**: applies to XML tag and attribute *names*
   only, never to values. `DataStream` → `data_stream`, `GUID` → `guid`
   (an underscore is only inserted at a genuine word-boundary transition,
   not before every capital letter — so acronyms don't get mangled).
   Non-alphanumeric characters (e.g. the hyphens in DDF-550's
   `DDF-CL-ID` attribute) are preserved as-is: `DDF-CL-ID` → `ddf-cl-id`
   (case changes, hyphens don't move).
5. **All values are JSON strings.** There is no int/bool coercion by
   naming convention or content-sniffing anymore (DDF-550 used to type
   `iFrequency`-style names as JSON numbers and `bXxx`-style names as JSON
   booleans — that's gone). `"#text": "145000000"`, not
   `"#text": 145000000`. Cast on the reading side if you need to compute
   with a value.
6. **No whitespace trimming.** If the wire XML wraps a value onto its own
   line (common for long enum names in some ICD samples), the leading
   newline/indentation is part of the text and survives into `"#text"`
   verbatim, e.g. `"#text": "\nIFPAN_FREQ_RANGE_80000"`. Trim on the
   reading side if you need the clean value.
7. **Size guard:** beyond 32 levels of nesting or 2000 total nodes visited
   in one message, the mirror returns `{"truncated": "true", "node_count":
   <N>}` at that point instead of recursing further — a soft limit for
   pathological input, not a parse error.

---

## 4. Per-device specifics — `channel` / `msg_kind` vocabularies

These stay genuinely device-specific (each ICD has its own root-tag
vocabulary), computed from the frame's root tag name:

| Root tag | CA120 `channel` | DDF-550 `channel` | DDF-1GTX `channel` |
|---|---|---|---|
| `Request` | `xml` | `control` | `control` |
| `Reply` | `xml` | `control` | `control` |
| `Event` | `xml` | `control` | *(not supported)* |
| `DDFCLRequest` / `DDFCLReply` | *(not supported)* | `preclassifier` | `preclassifier` |
| `DFSelect` | *(not supported)* | `preclassifier` | *(not supported)* |
| `DFData` | *(not supported)* | `preclassifier_output` | `preclassifier_output` |

`msg_kind` follows the frame direction plus root tag: `request` (frame
came in as a command), `reply` (frame came in as a response and isn't one
of the special cases below), `event` (root tag is literally `Event` —
CA120/DDF-550 only), `dfdata` (root tag is `DFData` — DDF-550/DDF-1GTX
only), or `malformed` (§2).

---

## 5. Migrating existing consumer code — old field → new path

| Old field (any of the three) | New path |
|---|---|
| `msg_type` (top-level) | `body.<root>.type` |
| `msg_id` (top-level) | `body.<root>.id` |
| `command_name` (DDF-550/DDF-1GTX) | `body.<root>.command.name` |
| `command_value` (DDF-550) | `body.<root>.command.#text` |
| `params.<X>` (DDF-550 flat map) | `body.<root>.command.param` — a single object `{"name":"X","#text":"..."}` if there's one `<Param>`, or an **array** of those objects if there's more than one (see §6.1) |
| `operation_mode`, `frequency_hz`, `freq_begin_hz`, `att_select`, `demodulation`, `af_bandwidth`, `audio_mode_str`, `df_pan_step`, `trace_tag_str`, `trace_ip`, `trace_port` (DDF-1GTX individually-named fields) | same as above — every one of these was a `<Param>` value, now uniformly under `body.<root>.command.param` |
| `ddf_cl_id` | `body.dfdata.ddf-cl-id` (DDF-550/DDF-1GTX DFData root; note the hyphens) |
| `fields.<X>` / `units.<X>` (DDF-550 DFData) | `body.dfdata.<snake_case_tag>` directly — e.g. `<CenterFrequency Unit="Hz">433920000</CenterFrequency>` → `body.dfdata.center_frequency` = `{"unit":"Hz","#text":"433920000"}` |
| `emitter_class`, `center_freq_hz`, `bearing_avg_deg`, `level_avg_dbuv` (DDF-1GTX DFData) | same pattern as above, snake_cased tag name directly under `body.dfdata` |
| `subsystems` (CA120 array of present tag names) | no equivalent — check for the presence of the relevant key directly, e.g. `"resource_manager" in body["request"]` |
| `datastream_action`, `datastream_type`, `stream_ip`, `stream_port` (CA120) | `body.<root>...data_stream.{action,type,ip,port}` wherever `<DataStream>` actually sits in the tree (nesting varies by message) |
| `raw_xml` (CA120 previously, DDF-1GTX previously) | removed entirely — no consumer depended on it (verified by repo-wide grep before removal); the mirrored `body` is now the only representation |

---

## 6. Reading this in code (Python, drs-server side)

### 6.1 The one-or-many gotcha

The single most important thing to get right: a repeated child tag is an
array, a single occurrence is **not**. Always normalize to a list before
iterating:

```python
def as_list(value):
    """A repeated XML child comes back as a list; a single occurrence
    comes back as the bare object/string. Normalize before iterating."""
    if value is None:
        return []
    return value if isinstance(value, list) else [value]

params = as_list(body["request"].get("command", {}).get("param"))
for p in params:
    print(p["name"], p.get("#text", ""))
```

### 6.2 Generic param lookup by name

```python
def get_param(command, name):
    """Find a <Param name="X">'s value under a mirrored `command` dict,
    or None if absent. Values are always strings -- cast at the call site."""
    for p in as_list(command.get("param")):
        if p.get("name") == name:
            return p.get("#text")
    return None

op_mode = get_param(body["request"]["command"], "eOperationMode")
freq_hz = int(get_param(body["request"]["command"], "iFrequency") or 0)
```

### 6.3 Always check `msg_kind` before touching `body`

```python
msg = json.loads(parsed_output)
if msg["msg_kind"] == "malformed":
    log.warning("malformed %s frame: %s (offset %d)",
                msg["hw"], msg["parse_error"], msg["parse_offset"])
    return
root_tag, root_value = next(iter(msg["body"].items()))
```

---

## 7. Worked examples (real, verified output — not hand-derived)

**CA120** — `<Request type="get" id="8" time="6000000"><ResourceManager><FaultReferenceList/></ResourceManager></Request>`:
```json
{"hw":"ca120","channel":"xml","msg_kind":"request","body":{"request":{"type":"get","id":"8","time":"6000000","resource_manager":{"fault_reference_list":{}}}}}
```

**CA120** — two sibling `<Control><DataStream>` blocks under one `<Request>` (the case that proves the array rule and fixes the old "second DataStream silently dropped" bug):
```json
{"hw":"ca120","channel":"xml","msg_kind":"request","body":{"request":{"type":"set","id":"1419","time":"6000000","control":[{"data_stream":{"action":"start","type":"processorSpectrum","protocol":"tcp","ip":"192.168.100.123","port":"49526"}},{"data_stream":{"action":"start","type":"timeDomain","protocol":"tcp","ip":"192.168.100.123","port":"49527"}}]}}}
```

**CA120** — malformed XML (mismatched closing tag):
```json
{"hw":"ca120","channel":"xml","msg_kind":"malformed","parse_error":"Start-end tags mismatch","parse_offset":36}
```

**DDF-550** — `<Request type="set" id="123"><Command name="DfMode"><Param name="eOperationMode">DFMODE_FFM</Param></Command></Request>`:
```json
{"hw":"ddf550","channel":"control","msg_kind":"request","body":{"request":{"type":"set","id":"123","command":{"name":"DfMode","param":{"name":"eOperationMode","#text":"DFMODE_FFM"}}}}}
```

**DDF-550** — `TraceDelete` with 2 sibling `<Param>`s (the array case; note
every value is a string, including `iPort`, which used to be typed as a
JSON number):
```json
{"hw":"ddf550","channel":"control","msg_kind":"request","body":{"request":{"type":"set","id":"123","command":{"name":"TraceDelete","param":[{"name":"zIP","#text":"10.11.12.13"},{"name":"iPort","#text":"12345"}]}}}}
```
(A command with more `<Param>` children, e.g. `DemodulationSettings`'s 13,
produces the same shape — `param` just holds that many array entries.)

**DDF-550** — `<DFSelect><EmitterClass>Burst</EmitterClass><EmitterClass>Hopper</EmitterClass></DFSelect>` (previously entirely invisible — the old curated code only looked for `<Param>` tags):
```json
{"hw":"ddf550","channel":"preclassifier","msg_kind":"request","body":{"df_select":{"emitter_class":[" Burst "," Hopper "]}}}
```

**DDF-1GTX** — `<Request type="set" id="1"><Command name="DfMode"><Param name="eOperationMode">DFMODE_FFM</Param></Command></Request>` (identical shape to DDF-550's equivalent message — this is the point of the family-wide contract):
```json
{"hw":"ddf1gtx","channel":"control","msg_kind":"request","body":{"request":{"type":"set","id":"1","command":{"name":"DfMode","param":{"name":"eOperationMode","#text":"DFMODE_FFM"}}}}}
```

---

## 8. What did NOT change

- `extract_frame` (frame-boundary detection) in all three parsers — still
  hand-rolled byte scanning, untouched.
- `format_response` (JSON → XML encode direction) in all three parsers —
  untouched; it already only template-wraps a caller-supplied XML
  fragment and never attempted to reverse the (lossy) decode-side field
  extraction.
- The AMMOS binary channel (CA120, ports 9200–9400) and the EB200 binary
  channel (DDF-550/DDF-1GTX, port 9152) — pure binary decoding, unrelated
  to this XML-content change, unaffected.
- pugixml as the parsing engine — decided separately in
  [xml-parsing-pugixml-migration-design.md](xml-parsing-pugixml-migration-design.md);
  this doc only changes what JSON is built from the already-parsed tree.

---

## 9. References

- [`drs-bridge/parsers/utils/pugixml_generic_mirror.h`](../../../drs-bridge/parsers/utils/pugixml_generic_mirror.h) / [`.cpp`](../../../drs-bridge/parsers/utils/pugixml_generic_mirror.cpp) — the shared implementation.
- [`drs-bridge/parsers/ca120/src/ca120_parser.cpp`](../../../drs-bridge/parsers/ca120/src/ca120_parser.cpp), [`drs-bridge/parsers/ddf550/src/ddf550_parser.cpp`](../../../drs-bridge/parsers/ddf550/src/ddf550_parser.cpp), [`drs-bridge/parsers/ddf1gtx/src/ddf1gtx_parser.cpp`](../../../drs-bridge/parsers/ddf1gtx/src/ddf1gtx_parser.cpp) — each variant's `impl_parse_xml_*`, which computes `channel`/`msg_kind` and calls the shared mirror.
- [xml-to-json-parser-design.md](xml-to-json-parser-design.md) — the original hand-rolled (non-pugixml) generic-mirror design this one's algorithm is ported from; that module remains committed/tested but unused in production.
- [xml-parsing-pugixml-migration-design.md](xml-parsing-pugixml-migration-design.md) — the pugixml-adoption decision this doc partially supersedes (§1 only, for these three variants' JSON *shape*; the pugixml-as-parser-engine decision itself stands).
