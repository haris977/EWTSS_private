# ICD Open Questions — drs-bridge Parser Library

**Purpose:** Tracks unresolved questions per hardware ICD that block or risk live integration.  
**Owner:** Person C (C++ parser developer)  
**Last updated:** 2026-06-22

Questions are grouped by device. Each question carries a **Risk** tag:

- `🔴 BLOCKER` — will produce silent wrong output or a crash in live integration without this answer  
- `🟡 VERIFY` — assumption made in the parser; needs a live-capture or client confirmation before sign-off  
- `🟢 NICE-TO-HAVE` — parser works without this, but the answer improves completeness

---

## 1. DP-ECM-1071 HF (JHF) — `dp_ecm_hf_parser.dll`

ICD used: `DP-ECM-1074-6000-V1-ICD-0V04`

| # | Question | Risk | Notes |
|---|---|---|---|
| H1 | **Endianness is inferred LITTLE-ENDIAN — never explicitly stated in the ICD.** Is this correct? | 🔴 BLOCKER | All `load_*le` helpers in the parser depend on this. Wrong endianness means every multi-byte field is silently garbage. |
| H2 | **How are IQ streaming ports 10021–10028 activated?** Is there a specific command the bridge must send first, or does the device start pushing once a TCP connection is made? | 🔴 BLOCKER | Stream-socket entry (frame type 3) is not yet wired in the parser. Need the activation sequence to implement it correctly. |
| H3 | **Status field in the response frame header** — the ICD notes a 16-bit signed status at byte offset 4. Is this always present even in non-error responses, or is it only populated on error? | 🟡 VERIFY | The parser reads it unconditionally. If it is reserved/zero in success responses, the output JSON is fine but redundant. If it is absent, offset calculations shift. |
| H4 | **HF and VU both use ports 10014 and 10015.** If both devices are live simultaneously, are they always on different IP addresses? Is the IP per-unit fixed at the rack level, or is it configurable? | 🟡 VERIFY | The bridge config will need one YAML profile per device instance. Need to confirm the IP assignment scheme so profiles can be written. |
| H5 | **IQ channels 10021–10028 — are all 8 channels always active, or only the channels the device is currently configured for?** | 🟢 NICE-TO-HAVE | Determines whether the bridge should try all 8 or only the active subset. |
| H6 | **Group 200 unit IDs 200/17, 200/19, 200/21 (Immediate Jam, Ext Modulation, Prog Exciter) are listed as "not yet assigned" in the ICD.** Parser falls through to `raw_hex` for these. What are the actual assigned unit IDs in the deployed firmware? | 🟡 VERIFY | Until confirmed, any jamming command using these units will produce `raw_hex` output instead of a decoded response — bridge-layer logic cannot act on it. |
| H7 | **Group 200 unit IDs 200/23 (Stop Responsive Sweep Jam), 200/24, and 200/51 (Stop Sweep Jam ACK) are assumed — they are not explicitly listed in the ICD unit table.** Are these IDs correct? | 🟡 VERIFY | A wrong unit ID means the parser silently misidentifies these command/response frames. The DRS would never correctly echo a stop-jam ACK back to the system. |

---

## 2. DP-ECM-1074 VU (JVU) — `dp_ecm_vu_parser.dll`

ICD used: `DP-ECM-1074-6000-V1-ICD-0V04`

| # | Question | Risk | Notes |
|---|---|---|---|
| V1 | **Endianness is inferred LITTLE-ENDIAN — never explicitly stated in the ICD.** Same question as H1 above. | 🔴 BLOCKER | Shares the same risk as the HF variant. |
| V2 | **No IQ streaming ports are defined in the VU ICD section.** Is the VU hardware physically incapable of IQ streaming, or was it simply omitted from this ICD version? | 🟡 VERIFY | If VU supports IQ streaming on the same 10021–10028 ports, the parser needs to handle frame type 3 on those ports. Currently it does not. |
| V3 | **Same port collision as H4** — VU uses TCP 10014 and 10015, same as HF. IP-based addressing confirmed? | 🟡 VERIFY | See H4. |
| V4 | **Group 200 (VU Jamming) — ICD defines 4 ACK responses (200/2, 200/4, 200/6, 200/8). Are there more jamming command groups not yet decoded?** | 🟢 NICE-TO-HAVE | Parser currently falls back to `raw_hex` for any unrecognised unit. Non-blocking but limits operational visibility. |
| V5 | **Group 200 jamming response payload is NOT decoded — parser treats 200/2, 200/4, 200/6, 200/8 as 0-byte ACK-only frames.** If the real device returns a body in these responses (status codes, jam parameters), the DRS response will be empty/wrong. What is the full response body layout for each? | 🔴 BLOCKER | The DRS must echo back a correctly formed jamming ACK. If the response has a body and the parser emits nothing, the system will receive a malformed frame or silence. |
| V6 | **ICD sequence numbering ambiguity in the VU section** — internal parser notes flag that "response ID is not in the sequence manner" for certain Group 200 entries. Is there a correct request→response ID mapping table for VU jamming commands? | 🟡 VERIFY | A wrong response ID means the system cannot match ACKs to commands, breaking the command/response handshake. |

---

## 3. DDF-550 — `ddf550_parser.dll`

ICD used: `R&S-DDF-550-ICD-V13`

| # | Question | Risk | Notes |
|---|---|---|---|
| D1 | **XML wrapper magic words (`MagicWordStart` / `MagicWordEnd`) are not specified in the ICD.** The values `XML_MAGIC_START` and `XML_MAGIC_END` in the parser are set to `0x00000000` (placeholder). The real values must come from a live packet capture or the R&S SCIF system manual. | 🔴 BLOCKER | `format_response` builds outgoing XML frames using these constants. Sending a frame with wrong magic bytes will be rejected by the device. `extract_frame` uses a length-field heuristic so parsing inbound frames is unaffected, but sending will fail. |
| D2 | **EB200 trace data push model** — the ICD says the client specifies its own listener IP and port inside the `TraceEnable` XML command, and the DDF-550 pushes EB200 frames there. In deployment, what IP and port range does the bridge use for this listener? Is there a fixed convention? | 🟡 VERIFY | The parser handles the EB200 frames correctly once they arrive; this is a bridge-layer configuration question, not a parser question. But it must be answered before writing the `dp_ecm_hf.yaml` profile equivalent for DDF-550. |
| D2a | **EB200 endianness confirmed BIG-ENDIAN** by ESME User Manual §7.6 (same EB200 protocol used by DDF-550). Parser is correctly coded big-endian. `TRAC:UDP:FLAG "SWAP"` can switch to little-endian — same question as E2: will SWAP be used? | 🟡 VERIFY | See E2. |
| D3 | **Preclassifier (ports 9153/9154) — is the DDFCL always present in the deployed configuration, or is it an optional accessory?** If absent, the bridge will get a connection refused on 9153. | 🟢 NICE-TO-HAVE | Parser handles both paths independently. Operational topology affects whether the bridge should treat a 9153 connection failure as a fatal error. |
| D4 | **EB200 periodic data (PeriodicTraceData) is NOT decoded for any trace tag.** The parser reports `n_items`, `sel_flags`, and `periodic_data_bytes` but never unpacks the actual measurement values. For DFPScan (5301) this means LEVEL and AZIMUTH pairs are present in the frame but never extracted into JSON. What is the exact PeriodicTraceData item layout (field widths, units, scaling) for each tag the deployment uses — DFPScan, IFPan, PScan? | 🔴 BLOCKER | DFPScan azimuth/bearing is the primary direction-finding output. The system receives a frame saying "3 items, 12 bytes" but never sees the actual bearing values. The DRS is blind to the measurement content it is supposed to mimic. |
| D5 | **EB200 `extract_frame` return value for incomplete TCP data is `-1` (corrupt) instead of `0` (wait).** On TCP, a large EB200 frame often arrives in multiple reads. The parser currently returns `-1` for any EB200 frame whose `DataSize` header field exceeds bytes currently in the buffer — declaring it corrupt and discarding it. Should the bridge always buffer until the full `DataSize` is received before calling `extract_frame`? | 🔴 BLOCKER | Without a clear buffering contract, any EB200 packet larger than the first TCP segment will be silently discarded. This affects IFPan (spectrum data), DFPScan, and any tag with large periodic payloads. |
| D6 | **`<Event>` XML root tag is not recognised by the parser.** The DDF-550 sends asynchronous `<Event>` frames for status changes, alarms, and scan-complete notifications. The parser's root-tag classifier only recognises `<Request>`, `<Reply>`, `<DDFCLRequest>`, `<DDFCLReply>`, and `<DFData>`. An `<Event>` frame is currently discarded as corrupt. Does the deployment rely on any DDF-550 event types that the DRS must echo or respond to? | 🟡 VERIFY | If the system expects event acknowledgements, or if events gate subsequent commands (e.g. scan-complete before next TraceEnable), dropping them silently breaks the flow. |
| D7 | **DFData `<Hopper>` emitter class — `StartFrequency` and `StopFrequency` fields are not extracted.** The parser only extracts `CenterFrequency` for all emitter classes. Frequency-hopping emitters reported as `Hopper` include a frequency range instead of (or in addition to) a center frequency. What fields does the system expect in the JSON output for Hopper-class detections? | 🟡 VERIFY | Hopper frequency range is operationally distinct from a fixed-frequency emitter. If the downstream consumer uses the frequency to cue a response, a missing range means the cue is incomplete. |
| D8 | **EB200 optional headers for non-Audio tags** — The parser decodes the Audio (tag 401) optional header (Table 12) in full. For all other tags (IFPan, DFPScan, SigP etc.), if `SEL_OPTIONAL_HEADER` is set in `sel_flags`, the optional header bytes are counted but their content is ignored. Do any of the tags used in this deployment carry optional headers with fields the DRS must decode? | 🟡 VERIFY | If DFPScan optional headers carry per-measurement metadata (e.g. timestamp, antenna ID), those fields are silently dropped. |
| D9 | **EB200 trace tag 101 (FScan / Frequency Scan) is not in the parser tag table** — it falls through to `"unknown"`. Are there other EB200 trace tags defined in the ICD (or used by the deployed firmware) that are not in the current tag table: 401, 501, 801, 901, 1001, 1101, 1201, 1301, 1801, 1901, 5301, 5501, 5601? | 🟢 NICE-TO-HAVE | Unknown tags are passed through with `"tag_name":"unknown"` — functional but produces untyped output. If FScan or other tags are in the data stream, they will not be labelled correctly. |

---

## 4. DDF-1GTX — `ddf1gtx_parser.dll`

ICD used: `ICD-DDF1GTX_22_08_25`

| # | Question | Risk | Notes |
|---|---|---|---|
| G1 | **XML magic words same issue as D1.** Parser has `XML_MAGIC_START = 0x00000000` placeholder. The ICD states the magic exists but does not give the values. | 🔴 BLOCKER | Same severity as D1 — outgoing `format_response` frames will be malformed. |
| G1a | **EB200 endianness confirmed BIG-ENDIAN** by ESME User Manual §7.6 (same EB200 protocol). Parser is correctly coded big-endian. Will `TRAC:UDP:FLAG "SWAP"` ever be used? | 🟡 VERIFY | See E2. |
| G2 | **eSPAN maximum bandwidth — ICD says 30 MHz, compared to DDF-550's 80 MHz.** Is there a corresponding parser difference in the EB200 trace payload layout, or is it purely a device capability limit? | 🟡 VERIFY | If the payload layout is identical the parser is already correct. If bandwidth affects a field width or count, it needs a fix. |
| G3 | **DDF-1GTX adds `DFPAN_STEP_10HZ` enum value not present in DDF-550.** Confirmed as the only enum difference? | 🟢 NICE-TO-HAVE | Parser treats unknown enum values as passthrough. Low risk; confirmation just keeps the code clean. |

---

## 5. CA120 — `ca120_parser.dll`

ICD used: `R&S-CA120-ICD-V15`

| # | Question | Risk | Notes |
|---|---|---|---|
| C1 | **Dynamic AMMOS data ports (>9200) are negotiated via the XML DataStream command.** In EWTSS v2 deployment, which stream types will the bridge actually request? The ICD defines many (`tunerSpectrum`, `ifData`, `analogAudio`, `demresult`, etc.). | 🟡 VERIFY | The parser handles all known AMMOS frame types. This is a system-level question: which streams are the drs-server consumers expecting Kafka to carry? |
| C2 | **If multiple CA120 units are on the same LAN, they all broadcast on UDP 7999 for service discovery.** How does the bridge select the correct one — by MAC, by first-seen, or by a fixed IP in configuration? | 🟡 VERIFY | The parser does not handle discovery; this is bridge-layer logic. But the answer determines whether the YAML profile needs a fixed IP or a discovery filter. |
| C3 | **XML framing on port 9001 uses no binary wrapper** — framing is done by detecting the closing root tag (`</Reply>`, `</Request>`, `</Event>`). Is there any CA120 firmware version where a length prefix was added? | 🟢 NICE-TO-HAVE | Low risk; the ICD is explicit. Confirmation just rules out a version-specific quirk. |
| C4 | **AMMOS dynamic data ports are allocated in the range 9200–9400 via the `DataStream` XML command.** The parser extracts the IP/Port fields but does not validate whether the port is within this range. What is the actual port allocation scheme — fixed per stream type, or negotiated per session? | 🟡 VERIFY | If the DRS opens a listener on the wrong port (or the system sends data to a port the DRS is not listening on), no AMMOS frames are received. Silent data loss. |
| C5 | **AMMOS frame type 0x220 (HOP_DENSITY_WATERFALL_DATA) has no header byte-table in the ICD** — parser extracts the body fields only; the header layout is undocumented. Is there a supplementary spec or a firmware release note that defines the 0x220 header? | 🟢 NICE-TO-HAVE | Parser emits what it can. Missing header fields could cause mis-alignment if the header is longer than assumed. |
| C6 | **Endianness is never stated anywhere in the ICD (R&S-CA120-ICD-V15, all 143 pages checked).** The AMMOS binary frame structures (Frame_Header, IF_Data_Header, Spectrum_Data_Header, etc.) define every field as `uint32`/`uint64`/`float` with no byte-order annotation. Parser is coded **LITTLE-ENDIAN** on the basis that CA120 runs on x86 Windows CA120PU hardware and both client and server are x86 — but this has never been explicitly confirmed by the ICD or ECIL. | 🔴 BLOCKER | If the assumption is wrong, every multi-byte AMMOS field (MagicWord, FrameLength, timestamps, frequencies, spectrum bins) is silently decoded backwards. All frame synchronisation and all output JSON values would be garbage. ECIL must confirm byte order or supply a live packet capture against which `MagicWord = 0xFB746572` can be verified as `72 65 74 FB` on the wire (little-endian) not `FB 74 65 72` (big-endian). |

---

## 6. ESME (MONUV HF/VU) — `esme_parser_monvuhf.dll`

ICD used: `R&S User Manual 4113.0075.02-03`, Chapter 7

| # | Question | Risk | Notes |
|---|---|---|---|
| E1 | **Bridge must open the UDP listener socket before sending `TRAC:UDP:TAG`.** If the bridge crashes and restarts, does the ESME automatically stop pushing to the old UDP endpoint and reset to idle, or does the bridge need to send an explicit stop command first? | 🔴 BLOCKER | Failure to reset the ESME session on reconnect means the bridge's new UDP socket never receives data while the ESME keeps pushing to the stale endpoint. |
| E2 | **EB200 endianness is explicitly confirmed BIG-ENDIAN by default** (ESME User Manual 4113.0075.02-03, §7.6, p.578: *"Generally all data is transferred in network byte order i.e. in big endian order"*). Parser is correctly coded big-endian. However, `TRAC:UDP:FLAG "SWAP"` can switch to little-endian at runtime. Will `SWAP` ever be used in the RDFS deployment? | 🟡 VERIFY | Endianness itself is now confirmed — no change needed to the parser unless SWAP is enabled. The SCPI configuration must be locked to never use SWAP, or the parser needs a run-time flag. If SWAP is activated and the parser is not updated, every EB200 field will be silently wrong. |
| E3 | **DFPan (tag 1401) `OptionalHeaderLength` field** — the parser always uses this field to locate `PeriodicTraceData` rather than hardcoding an offset. Is this the only correct approach, or does the ESME firmware always emit the same optional header size in practice? | 🟢 NICE-TO-HAVE | The dynamic approach is already implemented correctly. Confirmation just validates the design decision. |
| E4 | **DFPan Azimuth/Bearing field (EB200 tag 0x1000, at byte offset 106) is only present in ESME firmware version ≥ 0x53.** The parser reads this field unconditionally. What firmware version is installed on the deployed ESME units? If < 0x53, every DFPan frame will be mis-parsed from offset 106 onward. | 🔴 BLOCKER | Bearing is the primary output of the ESME direction-finder. A wrong firmware version means every bearing reading the DRS returns to the system is garbage — completely silent mis-parse. |
| E5 | **SCPI command/response validation is absent** — the parser treats all SCPI text as opaque. If the ESME returns an error response (e.g., to a `TRAC:UDP:TAG` that fails), the parser does not detect it. Does the ESME use standard SCPI error codes (`*ERR?`, `SYST:ERR?`) or a proprietary error format? | 🟡 VERIFY | An undetected SCPI error means the DRS thinks the ESME accepted the command (e.g., UDP push activated) when it did not — the UDP data stream never starts but the bridge never retries. |

---

## 7. COMM DF — `comm_df_parser.dll`

ICD used: internal SDFC↔DRS protocol (reference variant)

| # | Question | Risk | Notes |
|---|---|---|---|
| CF1 | **CRC field in command frames** — the test suite uses hardcoded CRC bytes marked `TODO_CRC` (computed offline). The CRC polynomial and algorithm are not documented in the ICD. What polynomial/algorithm does the device use to validate the CRC? | 🔴 BLOCKER | If the bridge sends frames with a wrong CRC, the COMM DF device will reject every command. The parser also validates incoming CRCs — a wrong algorithm means all received frames appear corrupt. |
| CF2 | **Bridge is the TCP server (listens on 5490).** If the COMM DF device disconnects and reconnects (e.g., power cycle), does the bridge simply accept the new connection on the same listening socket, or does it need any explicit reset? | 🟡 VERIFY | The runtime listener implementation should handle this transparently via `accept()`, but the device may expect a specific handshake or state reset after reconnect. |
| CF3 | **SCD compact frames** (magic `0xAAAA` / `0xEEEE`) — the second-byte disambiguation rule (`0xAB` = SDFC cmd, `0xAA` = SCD) is documented. Are SCD frames ever sent in the initial demo configuration, or are they only used in specific scan modes? | 🟢 NICE-TO-HAVE | Parser handles SCD frames; just determines what to exercise in the demo. |

---

## 8. RSEC / Himshakti — `rsec.dll`

ICD used: `DLRL/HIMSHAKTI/RSEC/2025/IRS`, Version 1.0, dated 21-07-2025

| # | Question | Risk | Notes |
|---|---|---|---|
| R1 | **⚠ Endianness UNCONFIRMED — IRS v1.0 is completely silent on byte order for Variant A and B binary frames.** Parser is coded BIG-ENDIAN (DLRL network-byte-order convention). Must confirm with ESMP integration team or a live packet capture. | 🔴 BLOCKER | If the device is little-endian, every `CmdCode`, `BodyLen`, frequency, and payload field in Variant A/B frames is wrong. All `load_be16`/`load_be32` calls must be flipped to `load_le16`/`load_le32`. |
| R2 | **Port numbers are NOT defined in the IRS.** The IRS describes the protocol but says nothing about which TCP/UDP port each subsystem (ESMP, ECMP, BB Rx, RFPS, SCU, GNSS) listens on. | 🔴 BLOCKER | The bridge cannot connect to any RSEC subsystem without these port numbers. Must obtain from the RSEC integration team or deployment configuration document. |
| R3 | **Active Track (0x1502) and Operational Data (0x1504) are periodic 1 Hz messages marked "never ACK" in the IRS.** Is this confirmed? Sending an ACK to these would trigger an unnecessary response cycle. | 🟡 VERIFY | Parser emits these as `no_ack: true` in JSON to signal the bridge layer. Confirmation prevents the bridge layer from accidentally ACK-ing them. |
| R4 | **Variant A Active Track response (command 0x1502) body length — IRS table ambiguity.** The parser uses 62 bytes but a note in `rsec_parser.cpp` says it may be 61 bytes if the RESERVED byte is absent. | 🟡 VERIFY | If the device sends 61-byte bodies, the parser will emit `parse_warning: body_shorter_than_expected` on every Active Track frame. Needs one real capture to confirm. |
| R5 | **Track ID namespaces: 1–500 = ESM-correlated (semi-auto), 501–550 = manually-entered (manual EA mode).** Is this segregation enforced by the RSEC device (it rejects IDs outside range), or is it a convention in the integration spec only? | 🟢 NICE-TO-HAVE | Determines whether the bridge needs to validate track IDs before forwarding ECMP jam commands. |
| R6 | **`format_response()` for RSEC is a stub — no ACK/response frame generation is implemented.** When the real system sends a command to the DRS (mimicking RSEC), what is the exact binary frame layout the RSEC device returns as its ACK? This must be reverse-engineered from the IRS or a packet capture. | 🔴 BLOCKER | Without a correctly formatted response frame, the system's command/response handshake never completes. Every command the system sends will time out or fail. |

---

## 9. AUS Protocol Analyser — `aus_parser.dll`

ICD used: `API Server_PROTOCOL_ANALYSER.json` (AUS-C2 REST API spec v4.2)

| # | Question | Risk | Notes |
|---|---|---|---|
| A1 | **AUS-C2 IP (`192.168.23.60`) and bridge IP (`192.168.23.100`) are hardcoded in current notes.** Are these fixed for all deployments, or are they configurable in the AUS-C2 software? | 🟡 VERIFY | If they are configurable, they must be in the bridge YAML profile rather than hardcoded. |
| A2 | **API path is `POST /api/v4.2/system`.** Will the `v4.2` version string change with AUS-C2 software updates? If so, how is the bridge expected to stay in sync? | 🟡 VERIFY | A version mismatch means the bridge stops receiving events silently (HTTP 404 or empty body). |
| A3 | **`sensors_info` object in the JSON payload** — the parser currently reads `devices_info` and `remote_id_devices_info` but only iterates `sensors_info` for sensor-attached detections. What sensor types can appear in `sensors_info`? Are there additional sub-fields the parser should be extracting? | 🟢 NICE-TO-HAVE | Parser emits a `source: sensor` tag for these entries. Completeness depends on what the drs-server consumers need. |

---

## 10. RDFS — `rdfs/parser.dll`

ICD used: internal (RDFS protocol — the reference `rdfs.yaml` variant)

| # | Question | Risk | Notes |
|---|---|---|---|
| RD1 | **Parser DLL path in `rdfs.yaml` is `parsers/rdfs/parser.dll`** — there is no `drs-bridge/parsers/rdfs/` directory. Is the RDFS parser the same binary as `comm_df_parser.dll` (same SDFC family), or does it need its own dedicated parser DLL built? | 🔴 BLOCKER | The runtime will fail to load at startup if the path does not resolve to a real file. |
| RD2 | **Bridge is the TCP server for RDFS (listens on 5001, sends UDP on 5002).** The RDFS device connects in. Is there a connection handshake or identification frame the device sends immediately on connect, or does it start sending operational messages directly? | 🟡 VERIFY | Determines whether the bridge needs a session-init step before it can trust the first received frame. |

---

## 11. Cross-Cutting — Request/Response System

These gaps apply to multiple devices and affect the DRS's ability to correctly mimic hardware in the request/response path (real system sends command → DRS parses it → DRS sends back a correctly formed response).

| # | Question | Risk | Notes |
|---|---|---|---|
| X1 | **Transaction/sequence IDs are not threaded through the SDFC-family parsers** (DP-ECM HF, DP-ECM VU, COMM DF, RDFS). The parser distinguishes request from response by frame type alone. If the real system uses a sequence number to match a response to its originating command, the DRS response must echo the same sequence ID. Does each device use a request sequence number that must be mirrored in the response? | 🟡 VERIFY | A missing or zeroed sequence ID in a response frame would cause the real system to discard it as unmatched, making every command appear to time out even though the DRS replied. |
| X2 | **Session initialization handshake is undocumented for CA120, ESME, and RSEC.** The parsers assume operational frames arrive without any prior negotiation. Does each device expect an identification/capability exchange before it begins accepting commands? | 🟡 VERIFY | If the real system expects a session-init frame first and the DRS skips it, the system may ignore all subsequent DRS output or close the connection immediately. |
| X3 | **No per-device reconnect/reset protocol is specified.** If the STK-side system disconnects and reconnects (e.g., scenario restart), does each device expect the DRS to send a reset command, or does it simply accept the new connection and continue? | 🟡 VERIFY | Applies to all TCP-connected devices. A stale session state on reconnect could leave the DRS sending responses the system is not expecting, causing desync. |

---

## Summary — Blockers Only

| ID | Device | Blocker |
|---|---|---|
| H1 | JHF | Endianness unconfirmed |
| H2 | JHF | IQ streaming activation sequence unknown |
| V1 | JVU | Endianness unconfirmed |
| V5 | JVU | Group 200 jamming response body not decoded — DRS cannot echo a correct jam ACK |
| D1 | DDF-550 | XML magic words unknown — `format_response` will send malformed frames |
| D4 | DDF-550 | EB200 periodic data never decoded — DFPScan azimuth/bearing values never reach the system |
| D5 | DDF-550 | EB200 incomplete TCP read returns corrupt (-1) not wait (0) — large EB200 frames silently discarded |
| G1 | DDF-1GTX | XML magic words unknown — same issue as D1 |
| C6 | CA120 | Endianness never stated in ICD — LITTLE-ENDIAN assumed from x86 Windows host, not confirmed |
| CF1 | COMM DF | CRC algorithm unknown — all commands will be rejected |
| E1 | ESME | Session reset on reconnect unclear — UDP data may never arrive after restart |
| E4 | ESME | DFPan Azimuth field absent on firmware < v0x53 — every bearing reading silent garbage |
| R1 | RSEC | Endianness unconfirmed — all multi-byte fields could be wrong |
| R2 | RSEC | Port numbers not in IRS — cannot connect to any subsystem |
| R6 | RSEC | `format_response()` stub — DRS cannot send any response frame; all commands time out |
| RD1 | RDFS | Parser DLL file does not exist at configured path |
