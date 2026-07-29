<a id="rsec--himshakti-binary-icd-to-json-reference"></a>
# RSEC (HIMSHAKTI) Binary ICD to JSON Reference

Auto-generated / hand-verified reference covering every message
`drs-bridge/parsers/rsec/src/rsec_parser.cpp`'s `parse_message()` recognizes.
Source ICD: `DLRL/HIMSHAKTI/RSEC/2025/IRS`, Version 1.0, 21-07-2025 (BEL/DLRL).

Every hex frame and JSON block below was produced by actually running the
compiled parser (`extract_frame()` + `parse_message()`) against the example
byte values shown — not hand-typed — so what you see here is exactly what
the DLL emits for that input today. Field values in the examples are
arbitrary but internally consistent (e.g. angles, frequencies); they are not
taken from a real RSEC capture.

Working/review document — not an architecture spec. For the narrative
version of this contract (envelope fields, direction-of-flow, known gaps)
see [`docs/ewtss/specs/rsec-parser-json-contract.md`](../../../docs/ewtss/specs/rsec-parser-json-contract.md).

Total messages: 41 (26 on the main protocol, 6 on BB Rx/RFPS, 7 on the SCU
servo link, 3 GNSS NMEA sentence types recognized by `parse_message`; GPGGA
is decoded even though it wasn't previously listed as its own test case).

---

## Contents

- [1. RSEC → ESMP (commands)](#1-rsec--esmp-commands)
- [2. ESMP → RSEC (telemetry / responses)](#2-esmp--rsec-telemetry--responses)
- [3. RSEC → ECMP (semi-auto EA)](#3-rsec--ecmp-semi-auto-ea)
- [4. ECMP → RSEC](#4-ecmp--rsec)
- [5. RSEC → ECMP (manual mode)](#5-rsec--ecmp-manual-mode)
- [6. RSEC → Broadband Rx LRU / RFPS](#6-rsec--broadband-rx-lru--rfps)
- [7. RFPS → RSEC](#7-rfps--rsec)
- [8. RSEC → SCU (servo, JHB(E) only)](#8-rsec--scu-servo-jhbe-only)
- [9. SCU → RSEC](#9-scu--rsec)
- [10. GNSS → RSEC](#10-gnss--rsec)

Wire framing per section (all multi-byte fields big-endian):
- **§1-5** (variant 1, "main protocol"): `[SOM 0xAAAA][CmdCode 2B][SeqNo 2B][BodyLen 4B][Body][EOM 0xEEEE]`, 12 bytes overhead.
- **§6-7** (variant 2, BB Rx/RFPS): `[SOM 0xAAABBABB][BodyLen 4B][CmdGroup 2B, always 0x0064][CmdUnitID 2B][Body][EOM 0xCCCDDCDD]`, 16 bytes overhead.
- **§8-9** (variant 3, SCU): `[SOF 0x24][DataLen 1B][CmdCode 1B][Data (DataLen-1) B][XOR checksum 1B][EOF 0x0D]`.
- **§10** (variant 4, GNSS): plain NMEA-0183 ASCII sentences terminated `\r\n`.

---

## 1. RSEC → ESMP (commands)

### 1.1 restart_emitter_processing <a id="restart_emitter_processing"></a>

Command Code `0x100F` · IRS §5.2.1 · No body — resets ES Processor to its
last-loaded configuration; does not clear the Warner library.

| Name | Data type | Size | Description |
|---|---|---|---|
| *(none)* | | 0 | This message has no body per the IRS. |

**HEX (frame):**
```
AA AA 10 0F 00 01 00 00 00 00 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4111,
  "cmd_code_hex": "0x100F",
  "seq_no": 1,
  "body_len": 0,
  "msg_type": "restart_emitter_processing"
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 1.2 time_data <a id="time_data"></a>

Command Code `0x1002` · IRS §5.2.11 · Sent once, on RSEC initialization.

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| System Time in Seconds | unsigned int | 4B | s | 0 - 0xFFFFFFFF | Seconds elapsed from epoch 1970-01-01T00:00:00.000 |

**HEX (frame):**
```
AA AA 10 02 00 02 00 00 00 04 67 45 80 00 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4098,
  "cmd_code_hex": "0x1002",
  "seq_no": 2,
  "body_len": 4,
  "msg_type": "time_data",
  "system_time_epoch_sec": 1732608000
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 1.3 load_warner_library <a id="load_warner_library"></a>

Command Code `0x1003` · IRS §5.2.2 · ESMP stops periodic messages
(`active_track_data`/`operational_data`) while this is being processed.

| Name | Data type | Size | Description |
|---|---|---|---|
| New Set/Modify Flag | unsigned char | 1B | 1 = new set (ESMP replaces the whole library), 0 = modify/append |
| Number of Library Records | unsigned short | 2B | 0-500 |
| *repeated per record (226B each):* | | | |
| Record Number | unsigned short | 2B | |
| Central Radar Database Number | unsigned int | 4B | |
| Frequency | unsigned int | 4B (KHz) | |
| Frequency Attributes | unsigned int | 4B | bit-encoded |
| Frequency Tolerance | unsigned short | 2B (KHz) | |
| PW | unsigned int | 4B (ns) | |
| PW Tolerance | unsigned short | 2B (ns) | |
| PRF | unsigned int | 4B (Hz) | |
| PRI Attributes | unsigned int | 4B | bit-encoded |
| PRF Tolerance | unsigned short | 2B (Hz) | |
| Threat Threshold Amplitude | unsigned char | 1B (dBm) | |
| Scan Type | unsigned char | 1B | bit-encoded |
| Antenna Scan Period | unsigned short | 2B (ms) | |
| ASP Tolerance | unsigned short | 2B (ms) | |
| JPRO Number | 24 bytes | | see [§1.13 jpro object](#jpro-object) |
| Threat Level | unsigned char | 1B | |
| NTDS Code | unsigned char | 1B | |
| Platform | unsigned char | 1B | |
| Operational Role | unsigned char | 1B | |
| Confidence Level | unsigned char | 1B | |
| Radar Name | 8 bytes ASCII | | |
| Radar Mode | unsigned char | 1B | |
| Next Mode Record Number | unsigned short | 2B | 0 = end of chain |
| Frequency Minimum/Maximum | unsigned int × 2 | 4B each (KHz) | |
| PW Minimum/Maximum | unsigned int × 2 | 4B each (ns) | |
| PGRI | unsigned int | 4B (ns) | |
| Spot PRFs | unsigned int[16] | 4B each (Hz) | fixed 16 entries |
| Spot Frequencies | unsigned int[16] | 4B each (KHz) | fixed 16 entries |

**HEX (frame, 1 record):**
```
AA AA 10 03 00 03 00 00 00 E5 01 00 01 00 0C 00 00 00 63 00 2D C6 C0 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 05 01 02 01 07 52 44 52 2D 41 42 43 31 01 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4099,
  "cmd_code_hex": "0x1003",
  "seq_no": 3,
  "body_len": 229,
  "msg_type": "load_warner_library",
  "new_set": true,
  "num_library_records": 1,
  "records": [
    {
      "record_no": 12,
      "central_radar_db_no": 99,
      "freq_khz": 3000000,
      "freq_attributes": 0,
      "freq_tolerance_khz": 0,
      "pw_ns": 0,
      "pw_tolerance_ns": 0,
      "prf_hz": 0,
      "pri_attributes": 0,
      "prf_tolerance_hz": 0,
      "threat_threshold_dbm": 0,
      "scan_type": 0,
      "asp_ms": 0,
      "asp_tolerance_ms": 0,
      "jpro": {
        "drfm_mode": 0,
        "num_responses": 0,
        "repeat": false,
        "techniques": []
      },
      "threat_level": 5,
      "ntds_code": 1,
      "platform": 2,
      "operational_role": 1,
      "confidence_level": 7,
      "radar_name": "RDR-ABC1",
      "radar_mode": 1,
      "next_mode_record_no": 0,
      "freq_min_khz": 0,
      "freq_max_khz": 0,
      "pw_min_ns": 0,
      "pw_max_ns": 0,
      "pgri_ns": 0,
      "spot_prfs_hz": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
      "spot_frequencies_khz": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
    }
  ]
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 1.4 delete_warner <a id="delete_warner"></a>

Command Code `0x1004` · IRS §5.2.3

| Name | Data type | Size | Description |
|---|---|---|---|
| Number of Library Records | unsigned short | 2B | 0-500; 0 = empty library |
| Record Number *(repeated)* | unsigned short | 2B each | one per declared record |

**HEX (frame — declares 2 records but only 1 supplied, to show the truncation warning):**
```
AA AA 10 04 00 04 00 00 00 04 00 02 00 0C EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4100,
  "cmd_code_hex": "0x1004",
  "seq_no": 4,
  "body_len": 4,
  "msg_type": "delete_warner",
  "num_library_records": 2,
  "record_numbers": [12],
  "parse_warning": "body_truncated_fewer_records_than_declared"
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 1.5 lockout_freq_bands <a id="lockout_freq_bands"></a>

Command Code `0x1005` · IRS §5.2.4 · A value of 0 in `num_bands` means "remove all existing lockout bands."

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| Number of Lockout Frequency Bands | unsigned char | 1B | | 0-16 | |
| Start Frequency *(repeated)* | unsigned int | 4B | KHz | 1GHz-40GHz | |
| Stop Frequency *(repeated)* | unsigned int | 4B | KHz | 1GHz-40GHz | |

**HEX (frame):**
```
AA AA 10 05 00 05 00 00 00 09 01 00 1E 84 80 00 26 25 A0 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4101,
  "cmd_code_hex": "0x1005",
  "seq_no": 5,
  "body_len": 9,
  "msg_type": "lockout_freq_bands",
  "num_bands": 1,
  "bands": [
    {"freq_low_khz": 2000000, "freq_high_khz": 2500000}
  ]
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 1.6 lockout_sectors <a id="lockout_sectors"></a>

Command Code `0x1006` · IRS §5.2.5 · 0 sectors means "remove all existing lockout sectors."

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| Number of Lockout Sectors | unsigned char | 1B | | 0-10 | |
| Start Angle *(repeated)* | unsigned short | 2B | deg ×10 | 0-359.9 | |
| End Angle *(repeated)* | unsigned short | 2B | deg ×10 | 0-359.9 | |

**HEX (frame):**
```
AA AA 10 06 00 06 00 00 00 05 01 03 84 07 08 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4102,
  "cmd_code_hex": "0x1006",
  "seq_no": 6,
  "body_len": 5,
  "msg_type": "lockout_sectors",
  "num_sectors": 1,
  "sectors": [
    {"start_deg": 90.0, "end_deg": 180.0}
  ]
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 1.7 auto_purge <a id="auto_purge"></a>

Command Code `0x1008` · IRS §5.2.6 · Single field: `0` = Auto Purge Off; `>0` = passive-track age threshold in seconds before purge.

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| Track Age | unsigned short | 2B | s | 0-600 | |

**HEX (frame):**
```
AA AA 10 08 00 07 00 00 00 02 00 78 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4104,
  "cmd_code_hex": "0x1008",
  "seq_no": 7,
  "body_len": 2,
  "msg_type": "auto_purge",
  "track_age_sec": 120
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 1.8 purge_passive_track <a id="purge_passive_track"></a>

Command Code `0x1010` · IRS §5.2.7 · `num_tracks = 0` purges every passive track.

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| Number of Tracks | unsigned short | 2B | | 0-500 | 0 = purge all |
| Track Number *(repeated)* | unsigned short | 2B each | | 1-500 | |

**HEX (frame):**
```
AA AA 10 10 00 08 00 00 00 06 00 02 00 0A 00 0B EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4112,
  "cmd_code_hex": "0x1010",
  "seq_no": 8,
  "body_len": 6,
  "msg_type": "purge_passive_track",
  "num_tracks": 2,
  "track_numbers": [10, 11]
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 1.9 purge_all <a id="purge_all"></a>

Command Code `0x1011` · IRS §5.2.12 · No body — removes all passive tracks and their pulse data.

**HEX (frame):**
```
AA AA 10 11 00 09 00 00 00 00 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4113,
  "cmd_code_hex": "0x1011",
  "seq_no": 9,
  "body_len": 0,
  "msg_type": "purge_all"
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 1.10 platform_heading (RSEC→ESMP) <a id="platform_heading"></a>

Command Code `0x1014` · IRS §5.3.5 · Sent on every update of heading from the Digital Compass.

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| Platform Heading | unsigned short | 2B | deg ×10 | 0-360.0 | |

**HEX (frame):**
```
AA AA 10 14 00 0A 00 00 00 02 04 B0 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4116,
  "cmd_code_hex": "0x1014",
  "seq_no": 10,
  "body_len": 2,
  "msg_type": "platform_heading",
  "heading_deg": 120
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 1.11 set_scan_bands <a id="set_scan_bands"></a>

Command Code `0x1118` · IRS §5.2.8

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| Number of Frequency Bands | unsigned char | 1B | | 1-16 | |
| DF_ComputationMode | unsigned char | 1B | | 1-3 | 1=ADF, 2=BLI, 3=ADF+BLI |
| Sector_Selection_BLI | unsigned char | 1B | | 1-4 | valid only if DF_ComputationMode is 2 or 3 |
| *repeated per band (14B each):* | | | | | |
| Scan Band Index | unsigned char | 1B | | 1-16 | |
| Start Frequency | unsigned short | 2B | MHz | 1000-18000 | |
| Stop Frequency | unsigned short | 2B | MHz | 1000-18000 | |
| RFCU Attenuation | unsigned char | 1B | dB | 0-30 | |
| SSU Attenuation | unsigned char | 1B | dB | 0-30 | |
| DigRx_Threshold | unsigned char | 1B | dB | -96 to -0 | |
| IF BW Selection | unsigned char | 1B | | 1-2 | 1=±250MHz, 2=±20MHz |
| DetectionTime | unsigned short | 2B | ms | 20-20000 | |
| CollectionTime | unsigned short | 2B | ms | 20-20000 | |
| Number of Revisits | unsigned char | 1B | | 1-4 | |

**HEX (frame, 1 band):**
```
AA AA 11 18 00 0B 00 00 00 11 01 03 01 01 07 D0 17 70 0A 08 28 01 00 64 00 C8 02 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4376,
  "cmd_code_hex": "0x1118",
  "seq_no": 11,
  "body_len": 17,
  "msg_type": "set_scan_bands",
  "num_bands": 1,
  "df_computation_mode": 3,
  "sector_selection_bli": 1,
  "bands": [
    {
      "scan_band_index": 1,
      "start_freq_mhz": 2000,
      "stop_freq_mhz": 6000,
      "rfcu_attenuation": 10,
      "ssu_attenuation": 8,
      "digrx_threshold": 40,
      "if_bw_selection": 1,
      "detection_time_ms": 100,
      "collection_time_ms": 200,
      "num_revisits": 2
    }
  ]
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 1.12 directed_search <a id="directed_search"></a>

Command Code `0x1114` · IRS §5.2.10 · Sets NB Rx scan configuration to "Directed Search" mode.

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| Frequency | unsigned short | 2B | MHz | 1000-18000 | directed RF frequency |
| RF Attenuation | unsigned char | 1B | dB | 0-30 | |
| IF Attenuation | unsigned char | 1B | dB | 0-30 | |
| Threshold | unsigned char | 1B | dB | 0-63 → -63 to -0 | |
| IF BW Selection | unsigned char | 1B | | 1-2 | 1=±250MHz, 2=±20MHz |
| Scan Quadrant | unsigned char | 1B | | 1-4 | 1=(0-90°) 2=(90-180°) 3=(180-270°) 4=(270-0°) |

**HEX (frame):**
```
AA AA 11 14 00 0C 00 00 00 07 1F 40 0A 05 3C 01 02 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4372,
  "cmd_code_hex": "0x1114",
  "seq_no": 12,
  "body_len": 7,
  "msg_type": "directed_search",
  "frequency_mhz": 8000,
  "rf_attenuation_db": 10,
  "if_attenuation_db": 5,
  "threshold_db": 60,
  "if_bw_selection": 1,
  "scan_quadrant": 2
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 1.13 ack_nack_rsec_to_esmp <a id="ack_nack_rsec_to_esmp"></a><a id="jpro-object"></a>

Command Code `0x150A` · IRS §5.2.9 · Same 5-byte layout as the ESMP→RSEC
direction (§2.4) — one decoder handles both.

| Name | Data type | Size | Description |
|---|---|---|---|
| Message Code | unsigned short | 2B | code of the message being acknowledged |
| Sequence Number | unsigned short | 2B | sequence number of that message instance |
| Acknowledgement Status | unsigned char | 1B | 0=NACK invalid, 1=ACK received, 2=ACK received & executed, 3=received but failed to execute |

**HEX (frame):**
```
AA AA 15 0A 00 0D 00 00 00 05 10 03 00 03 02 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 5386,
  "cmd_code_hex": "0x150A",
  "seq_no": 13,
  "body_len": 5,
  "msg_type": "ack_nack_rsec_to_esmp",
  "acked_cmd_code_hex": "0x1003",
  "acked_seq_no": 3,
  "ack_status": 2
}
```

#### The `jpro` object

Every message below that carries a JPRO Number (24 raw bytes, IRS §5.5.1.1)
decodes to this shape. `techniques[]` has 0-3 entries (`num_responses`,
masked to the low 5 bits of the response-count byte; bit 5 is the separate
`repeat` flag). Worked example with all 3 technique slots populated (this is
also the case that used to silently truncate before the buffer-size fix —
see the parser's inline comments on `decode_jpro()`):

```json
{
  "drfm_mode": 2053,
  "num_responses": 3,
  "repeat": false,
  "techniques": [
    {"technique":3,  "technique_value":20,"num_cycles":5, "velocity_profile":0, "end_range_or_doppler":0,  "subtype_code":0,"non_linear_profile":0},
    {"technique":8,  "technique_value":4, "num_cycles":10,"velocity_profile":50,"end_range_or_doppler":100,"subtype_code":0,"non_linear_profile":2},
    {"technique":132,"technique_value":6, "num_cycles":1, "velocity_profile":0, "end_range_or_doppler":0,  "subtype_code":3,"non_linear_profile":0}
  ]
}
```
`technique` 1-16 = DRFM/DTO jamming techniques (spot noise, barrage, VGPO/
VGPI, RGPO/RGPI, false targets, etc.); 32/64/128 = angle-deception
techniques (Blink/SRM/SSRM), OR-able onto a jamming technique (e.g. `132` =
`4 + 128`). `technique_value`'s meaning depends on `technique` — consult IRS
p.45-46 before treating it as a plain number.

[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

## 2. ESMP → RSEC (telemetry / responses)

### 2.1 active_track_data <a id="active_track_data"></a>

Command Code `0x1502` · periodic, 1Hz · **never ACK'd** even though the envelope looks like a command (IRS §5.1 rule).

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| System Mode | unsigned char | 1B | | | current ESMP operating mode |
| Number of Tracks | unsigned char | 1B | | | |
| *repeated per track (24B each):* | | | | | |
| Track ID | unsigned short | 2B | | 1-500 ESM-correlated, 501-550 manual | |
| DOA | signed short | 2B | deg ×10 | 0-359.9 | |
| Frequency | unsigned int | 4B | KHz | | |
| PRI | unsigned int | 4B | µs | | |
| PW | unsigned int | 4B | ns | | |
| Scan Period | unsigned int | 4B | ms | | |
| Amplitude | signed short | 2B | dBm | | |
| Track Status | unsigned char | 1B | | bit flags | b0=active, b1=manual, b2=jamming |
| Emitter Category | unsigned char | 1B | | | |

**HEX (frame, 1 track):**
```
AA AA 15 02 00 14 00 00 00 1A 01 01 00 2A 03 84 00 00 27 10 00 00 03 E8 00 00 13 88 00 00 01 F4
FF D8 01 03 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 5378,
  "cmd_code_hex": "0x1502",
  "seq_no": 20,
  "body_len": 26,
  "msg_type": "active_track_data",
  "system_mode": 1,
  "num_tracks": 1,
  "tracks": [
    {
      "track_id": 42,
      "doa_deg": 90.0,
      "freq_mhz": 10.000,
      "pri_us": 1000,
      "pw_ns": 5000,
      "scan_ms": 500,
      "amplitude_dbm": -40,
      "status": "0x01",
      "emitter_cat": 3
    }
  ]
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 2.2 operational_data <a id="operational_data"></a>

Command Code `0x1504` · periodic, 1Hz · **never ACK'd**.

| Name | Data type | Size | Description |
|---|---|---|---|
| ESMP Status | unsigned char | 1B | 0=Init, 1=Operational, 2=Fault, 3=Standby |
| Number of Active Tracks | unsigned char | 1B | |
| Number of Manual Tracks | unsigned char | 1B | |
| Scan Status | unsigned char | 1B | 0=Idle, 1=Scanning, 2=Directed |
| Scan Band Index | unsigned short | 2B | currently scanning band |
| HW Status | unsigned char | 1B | hardware status flags |
| Reserved | unsigned char | 1B | |

**HEX (frame):**
```
AA AA 15 04 00 15 00 00 00 08 01 03 00 01 00 02 00 00 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 5380,
  "cmd_code_hex": "0x1504",
  "seq_no": 21,
  "body_len": 8,
  "msg_type": "operational_data",
  "esmp_status": 1,
  "num_active_tracks": 3,
  "num_manual_tracks": 0,
  "scan_status": 1,
  "scan_band_index": 2,
  "hw_status_hex": "00"
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 2.3 purge_response <a id="purge_response"></a>

Command Code `0x1505`

| Name | Data type | Size | Description |
|---|---|---|---|
| Result | unsigned short | 2B | 0=success, 1=fail, 2=track_not_found |

**HEX (frame):**
```
AA AA 15 05 00 16 00 00 00 02 00 05 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 5381,
  "cmd_code_hex": "0x1505",
  "seq_no": 22,
  "body_len": 2,
  "msg_type": "purge_response",
  "result": 0
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 2.4 ack_nack_esmp_to_rsec <a id="ack_nack_esmp_to_rsec"></a>

Command Code `0x1509` · IRS §5.3.4 · Same 5-byte layout as [§1.13](#jpro-object).

**HEX (frame):**
```
AA AA 15 09 00 17 00 00 00 05 10 10 00 08 01 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 5385,
  "cmd_code_hex": "0x1509",
  "seq_no": 23,
  "body_len": 5,
  "msg_type": "ack_nack_esmp_to_rsec",
  "acked_cmd_code_hex": "0x1010",
  "acked_seq_no": 8,
  "ack_status": 1
}
```

**⚠ IRS self-contradiction inherited as-is:** IRS §5.9.1 ("IQ Data Logging
Response," a *different* message on the RFPS/BB Rx wire family, §7.1) is
titled "Data Flow from RFPS to RSEC" with IRS ID `RFPS_TO_RSEC_01`, but its
own "Source of Command" field says **ESMP**. That message isn't this one —
flagging it here because it's the same kind of ESMP/RFPS boundary question
this section lives on. Confirm the real sender before wiring up your reply
logic.

[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

## 3. RSEC → ECMP (semi-auto EA)

### 3.1 semi_auto_track <a id="semi_auto_track"></a>

Command Code `0x1101` · IRS §5.5.1

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| Track No | unsigned short | 2B | | 1-500 | |
| DOA | unsigned short | 2B | deg ×10 | 0-359.9 | |
| Frequency | unsigned int | 4B | KHz | 1000MHz-18GHz | |
| PW | unsigned int | 4B | ns | | |
| PRF | unsigned int | 4B | Hz | | |
| Threat Level | unsigned char | 1B | | 0-9 | |
| JPRO Number | 24 bytes | | | | see [§1.13](#jpro-object) |

**HEX (frame):**
```
AA AA 11 01 00 1E 00 00 00 29 00 05 03 84 00 12 4F 80 00 00 03 E8 00 00 07 D0 05 00 02 01 03 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4353,
  "cmd_code_hex": "0x1101",
  "seq_no": 30,
  "body_len": 41,
  "msg_type": "semi_auto_track",
  "track_no": 5,
  "doa_deg": 90,
  "freq_khz": 1200000,
  "pw_ns": 1000,
  "prf_hz": 2000,
  "threat_level": 5,
  "jpro": {
    "drfm_mode": 2,
    "num_responses": 1,
    "repeat": false,
    "techniques": [
      {"technique":3,"technique_value":0,"num_cycles":0,"velocity_profile":0,"end_range_or_doppler":0,"subtype_code":0,"non_linear_profile":0}
    ]
  }
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 3.2 semi_auto_jam <a id="semi_auto_jam"></a>

Command Code `0x1102` · IRS §5.5.2 · **No JPRO in this message** — jams emitters already being tracked, using whatever technique they were tracked/jammed with previously.

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| Number of Emitters | unsigned char | 1B | | 1-50 | |
| Emitter Number *(repeated)* | unsigned short | 2B | | 1-500 | |
| Jam Duration *(repeated)* | unsigned short | 2B | s | 0-100 | 0 = jam indefinitely |

**HEX (frame):**
```
AA AA 11 02 00 1F 00 00 00 05 01 00 0F 00 14 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4354,
  "cmd_code_hex": "0x1102",
  "seq_no": 31,
  "body_len": 5,
  "msg_type": "semi_auto_jam",
  "num_emitters": 1,
  "emitters": [
    {"emitter_number": 15, "jam_duration_sec": 20}
  ]
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 3.3 semi_auto_track_jam <a id="semi_auto_track_jam"></a>

Command Code `0x1103` · IRS §5.5.3 · Track + Jam combined, 69-byte body.

| Name | Data type | Size | UoM | Description |
|---|---|---|---|---|
| Track No | unsigned short | 2B | | 1-500 |
| DOA | unsigned short | 2B | deg×10 | |
| Frequency | unsigned int | 4B | KHz | |
| Frequency Attributes | unsigned int | 4B | | bit-encoded |
| PW | unsigned int | 4B | ns | |
| PRF | unsigned int | 4B | Hz | |
| PRI Attributes | unsigned int | 4B | | bit-encoded |
| Amplitude | signed char | 1B | dBm | |
| Scan Type | unsigned char | 1B | | bit-encoded |
| ASP | unsigned short | 2B | ms | |
| PW Minimum | unsigned int | 4B | ns | |
| PW Maximum | unsigned int | 4B | ns | |
| Spot Frequencies | unsigned int | 4B | KHz | |
| Threat Level | unsigned char | 1B | | 0-9 |
| Elevation | unsigned short | 2B | deg×10 | |
| JPRO Number | 24 bytes | | | see [§1.13](#jpro-object) |
| Jam Duration | unsigned short | 2B | s | 0 = indefinite |

**HEX (frame):**
```
AA AA 11 03 00 20 00 00 00 45 00 4D 03 84 00 12 4F 80 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 E2 01 00 64 00 00 01 F4 00 00 05 DC 00 12 4F 80 05 00 96 00 02 01 03 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 1E EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4355,
  "cmd_code_hex": "0x1103",
  "seq_no": 32,
  "body_len": 69,
  "msg_type": "semi_auto_track_jam",
  "track_no": 77,
  "doa_deg": 90,
  "freq_khz": 1200000,
  "freq_attributes": 0,
  "pw_ns": 0,
  "prf_hz": 0,
  "pri_attributes": 0,
  "amplitude_dbm": -30,
  "scan_type": 1,
  "asp_ms": 100,
  "pw_min_ns": 500,
  "pw_max_ns": 1500,
  "spot_freq_khz": 1200000,
  "threat_level": 5,
  "elevation_deg": 15,
  "jpro": {
    "drfm_mode": 2,
    "num_responses": 1,
    "repeat": false,
    "techniques": [
      {"technique":3,"technique_value":0,"num_cycles":0,"velocity_profile":0,"end_range_or_doppler":0,"subtype_code":0,"non_linear_profile":0}
    ]
  },
  "jam_duration_sec": 30
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 3.4 break_track <a id="break_track"></a>

Command Code `0x1104` · IRS §5.5.5

| Name | Data type | Size | UoM | Range |
|---|---|---|---|---|
| Number of Emitters | unsigned char | 1B | | 1-50 |
| Emitter Number *(repeated)* | unsigned short | 2B | | 1-500 |

**HEX (frame):**
```
AA AA 11 04 00 21 00 00 00 03 01 00 0F EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4356,
  "cmd_code_hex": "0x1104",
  "seq_no": 33,
  "body_len": 3,
  "msg_type": "break_track",
  "num_emitters": 1,
  "emitter_numbers": [15]
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 3.5 stop_jam (semi-auto) <a id="stop_jam"></a>

Command Code `0x1105` · IRS §5.5.4 · Same shape as [break_track](#break_track).

**HEX (frame):**
```
AA AA 11 05 00 22 00 00 00 03 01 00 0F EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4357,
  "cmd_code_hex": "0x1105",
  "seq_no": 34,
  "body_len": 3,
  "msg_type": "stop_jam",
  "num_emitters": 1,
  "emitter_numbers": [15]
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 3.6 change_jpro <a id="change_jpro"></a>

Command Code `0x1106` · IRS §5.5.6 · Updates the jamming technique of a track already under tracking or jamming.

| Name | Data type | Size | Range |
|---|---|---|---|
| Track No | unsigned short | 2B | 1-500 |
| JPRO Number | 24 bytes | | see [§1.13](#jpro-object) |

**HEX (frame — 3 technique slots populated, demonstrating the fixed no-truncation JPRO path):**
```
AA AA 11 06 00 23 00 00 00 1A 00 05 08 05 03 03 14 05 00 00 00 00 08 04 0A 32 64 00 02 84 06 01
00 00 03 00 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4358,
  "cmd_code_hex": "0x1106",
  "seq_no": 35,
  "body_len": 26,
  "msg_type": "change_jpro",
  "track_no": 5,
  "jpro": {
    "drfm_mode": 2053,
    "num_responses": 3,
    "repeat": false,
    "techniques": [
      {"technique":3,  "technique_value":20,"num_cycles":5, "velocity_profile":0, "end_range_or_doppler":0,  "subtype_code":0,"non_linear_profile":0},
      {"technique":8,  "technique_value":4, "num_cycles":10,"velocity_profile":50,"end_range_or_doppler":100,"subtype_code":0,"non_linear_profile":2},
      {"technique":132,"technique_value":6, "num_cycles":1, "velocity_profile":0, "end_range_or_doppler":0,  "subtype_code":3,"non_linear_profile":0}
    ]
  }
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

## 4. ECMP → RSEC

### 4.1 ea_operational_data <a id="ea_operational_data"></a>

Command Code `0x1153` · IRS §5.6.1 · Sent after EC Processor executes EA tasks.

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| No of Tracks | unsigned char | 1B | | 1-50 | |
| *repeated per track (36B each):* | | | | | |
| Emitter Number | unsigned short | 2B | | 501-550 | |
| Frequency | unsigned int | 4B | MHz | 1000-18000 | |
| Azimuth | unsigned short | 2B | deg×10 | 0-359.9 | DOA of emitter |
| Elevation | unsigned short | 2B | deg×10 | 0-30 | |
| Threat Status | unsigned char | 1B | | bit-encoded | see IRS §5.6.1.1 |
| JPRO Number | 24 bytes | | | | see [§1.13](#jpro-object) |
| RTG Status | unsigned char | 1B | | 0-1 | 1=RTG ON, 0=RTG OFF |

**HEX (frame):**
```
AA AA 11 53 00 28 00 00 00 25 01 01 F5 00 00 1F 40 03 84 00 96 07 00 02 01 03 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 4435,
  "cmd_code_hex": "0x1153",
  "seq_no": 40,
  "body_len": 37,
  "msg_type": "ea_operational_data",
  "num_tracks": 1,
  "tracks": [
    {
      "emitter_number": 501,
      "freq_mhz": 8000,
      "azimuth_deg": 90.0,
      "elevation_deg": 15.0,
      "threat_status": "0x07",
      "jpro": {
        "drfm_mode": 2,
        "num_responses": 1,
        "repeat": false,
        "techniques": [
          {"technique":3,"technique_value":0,"num_cycles":0,"velocity_profile":0,"end_range_or_doppler":0,"subtype_code":0,"non_linear_profile":0}
        ]
      },
      "rtg_status": 1
    }
  ]
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

## 5. RSEC → ECMP (manual mode)

### 5.1 ecmp_manual_start <a id="ecmp_manual_start"></a>

Command Code `0x6001` · IRS §5.7.1 ("Track Command (Manual)")

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| Track No | unsigned short | 2B | | 501-550 | |
| DOA | unsigned short | 2B | deg×10 | 0-359.9 | |
| Frequency | unsigned int | 4B | KHz | | |
| Frequency Attributes | unsigned int | 4B | | bit-encoded | |
| PW | unsigned int | 4B | ns | | |
| PRF | unsigned int | 4B | Hz | | |
| Threat Level | unsigned char | 1B | | 0-9 | |
| JPRO Number | 24 bytes | | | | see [§1.13](#jpro-object) |

**HEX (frame):**
```
AA AA 60 01 00 32 00 00 00 2D 01 F5 03 84 00 12 4F 80 00 00 00 00 00 00 00 00 00 00 00 00 05 00
02 01 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 EE EE
```
**JSON:**
```json
{
  "frame_variant": 1,
  "cmd_code": 24577,
  "cmd_code_hex": "0x6001",
  "seq_no": 50,
  "body_len": 45,
  "msg_type": "ecmp_manual_start",
  "track_no": 501,
  "doa_deg": 90,
  "freq_khz": 1200000,
  "freq_attributes": 0,
  "pw_ns": 0,
  "prf_hz": 0,
  "threat_level": 5,
  "jpro": {
    "drfm_mode": 2,
    "num_responses": 1,
    "repeat": false,
    "techniques": [
      {"technique":3,"technique_value":0,"num_cycles":0,"velocity_profile":0,"end_range_or_doppler":0,"subtype_code":0,"non_linear_profile":0}
    ]
  }
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 5.2 Manual-mode messages — all 11 now field-level decoded (2026-07-28)

All manual-mode command codes are now dispatched with full field-level
decoding, built directly against `HIMSHAKTI_RSEC_IRS_02062025_BEL_To_Constelli.pdf`
§5.7.1-5.7.12. Two of these (`0x0FA8`, `0x0FAE`) had no dispatch case at all
before this pass — not even the raw-hex fallback — and were only discovered
by reading the IRS's table of contents against the parser's actual switch
statement.

| Command Code | IRS message | IRS § | `msg_type` | Notes |
|---|---|---|---|---|
| `0x0FA4` | Break Track (Manual) | 5.7.5 | `break_track_manual` | Same wire shape as semi-auto Break Track (0x1104); reuses `parse_ecmp_break_track` |
| `0x0FA5` | Jam Command (Manual) | 5.7.2 | `jam_command_manual` | Same wire shape as semi-auto Jam Command (0x1102); reuses `parse_ecmp_jam` |
| `0x0FA6` | Stop Jam (Manual) | 5.7.4 | `stop_jam_manual` | Same wire shape as semi-auto Stop Jam (0x1105); reuses `parse_ecmp_stop_jam` |
| `0x0FA7` | Track and Jam Command (Manual) | 5.7.3 | `track_and_jam_manual` | Same wire shape as semi-auto (0x1103); reuses `parse_ecmp_track_and_jam` |
| `0x0FA8` | Update Track (Manual) | 5.7.6 | `update_track_manual` | **Was not wired in at all.** Variable-length parameter list (code 1-12, size per code) — new `parse_update_track_manual` |
| `0x0FAE` | Change Mode (Manual) | 5.7.7 | `change_mode_manual` | **Was not wired in at all.** 1-byte mode value (1=Semi Auto, 2=Manual) — new `parse_change_mode_manual` |
| `0x0FB0` | Reset EA Subsystem | 5.7.9 | `reset_ea_subsystem` | No data element table — trigger-only, same as Purge All (0x1011) |
| `0x1108` | Platform Heading Data to EA Processor | 5.7.10 | `platform_heading_to_ea` | **Was not wired in at all** (previously fell through to `"msg_type":"unknown"`). Same 2-byte ÷10-deg shape as ESMP-side Platform Heading (0x1014); reuses `parse_platform_heading` |
| `0x1120` | Set Forbidden Frequency Bands | 5.7.8 | `set_forbidden_bands` | New decoder — distinct from ESMP-side Lockout Bands (0x1005): UINT16 MHz fields, not UINT32 KHz |
| `0x1121` | Set Prohibited Sectors | 5.7.11 | `set_prohibited_sectors` | New decoder — distinct from ESMP-side Lockout Sectors (0x1006): adds an `entry_id` field |
| `0x111D` | ECM operational status | 5.7.12 | `ecm_operational_status` | Same wire shape as EA Operational Data Semi (0x1153); reuses `parse_ea_operational_data` |

All 11 covered by new tests in `tests/test_frames_rsec.cpp` (31/31 passing).
`format_response()` (encode side) for these remains explicitly out of scope —
see the parser-level note on that decision.

[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

## 6. RSEC → Broadband Rx LRU / RFPS

Variant 2 framing: `[0xAAABBABB][BodyLen 4B][CmdGroup=0x0064][CmdUnitID][Body][0xCCCDDCDD]`.

### 6.1 bb_sfb_selection <a id="bb_sfb_selection"></a>

CmdUnitID `0x1126` · IRS §5.4.1 · Selects Switch Filter Band (operational mode) of BB Rx (2.2-18GHz).

| Name | Data type | Size | Description |
|---|---|---|---|
| Number of Bands | unsigned char | 1B | |
| Freq Low *(repeated)* | unsigned int | 4B (MHz) | |
| Freq High *(repeated)* | unsigned int | 4B (MHz) | |

**HEX (frame, 2 bands):**
```
AA AB BA BB 00 00 00 11 00 64 11 26 02 00 00 07 D0 00 00 17 70 00 00 17 70 00 00 46 50 CC CD DC DD
```
**JSON:**
```json
{
  "frame_variant": 2,
  "cmd_group": 100,
  "cmd_uid": 4390,
  "cmd_uid_hex": "0x1126",
  "body_len": 17,
  "msg_type": "bb_sfb_selection",
  "num_bands": 2,
  "bands": [
    {"freq_low_mhz": 2000, "freq_high_mhz": 6000},
    {"freq_low_mhz": 6000, "freq_high_mhz": 18000}
  ]
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 6.2 bb_rf_sector_blank <a id="bb_rf_sector_blank"></a>

CmdUnitID `0x1127` · IRS §5.4.2

| Name | Data type | Size | UoM |
|---|---|---|---|
| Number of Sectors | unsigned char | 1B | |
| Start/End DOA *(repeated)* | unsigned short × 2 | 2B each | deg×10 |

**HEX (frame):**
```
AA AB BA BB 00 00 00 05 00 64 11 27 01 03 84 07 08 CC CD DC DD
```
**JSON:**
```json
{
  "frame_variant": 2,
  "cmd_group": 100,
  "cmd_uid": 4391,
  "cmd_uid_hex": "0x1127",
  "body_len": 5,
  "msg_type": "bb_rf_sector_blank",
  "num_sectors": 1,
  "sectors": [
    {"start_deg": 90.0, "end_deg": 180.0}
  ]
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 6.3 bb_cal_onoff <a id="bb_cal_onoff"></a>

CmdUnitID `0x1128` · IRS §5.4.3

| Name | Data type | Size | Range |
|---|---|---|---|
| Calibration ON/OFF | unsigned char | 1B | 0=off, 1=on (load calibrated values) |

**HEX (frame):**
```
AA AB BA BB 00 00 00 01 00 64 11 28 01 CC CD DC DD
```
**JSON:**
```json
{
  "frame_variant": 2,
  "cmd_group": 100,
  "cmd_uid": 4392,
  "cmd_uid_hex": "0x1128",
  "body_len": 1,
  "msg_type": "bb_cal_onoff",
  "calibration_enabled": true
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 6.4 rfps_iq_data_logging <a id="rfps_iq_data_logging"></a>

CmdUnitID `0x1009` · IRS §5.8.1 ("Data Flow from RSEC to ESMPRFPS interface" — the
IRS names the interface ambiguously; see the caveat under [§2.4](#ack_nack_esmp_to_rsec)).

| Name | Data type | Size | UoM | Range | Description |
|---|---|---|---|---|---|
| Track Frequency | unsigned short | 2B | MHz | 1000-40000 | |
| RF Attenuation | unsigned char | 1B | dB | 0-30 | |
| IF Attenuation | unsigned char | 1B | dB | 0-30 | |
| Threshold | unsigned char | 1B | dB | 30-95 → -95 to -30 | |
| IF BW Selection | unsigned char | 1B | | 1-2 | 1=±500MHz, 2=±20MHz |
| Sector | unsigned char | 1B | | 1-4 | |
| No of Pulses | unsigned short | 2B | | 0-10000 | pulses to record for IQ logging |

**HEX (frame):**
```
AA AB BA BB 00 00 00 09 00 64 10 09 1F 40 0A 05 3C 01 02 01 F4 CC CD DC DD
```
**JSON:**
```json
{
  "frame_variant": 2,
  "cmd_group": 100,
  "cmd_uid": 4105,
  "cmd_uid_hex": "0x1009",
  "body_len": 9,
  "msg_type": "rfps_iq_data_logging",
  "track_frequency_mhz": 8000,
  "rf_attenuation_db": 10,
  "if_attenuation_db": 5,
  "threshold_db": 60,
  "if_bw_selection": 1,
  "sector": 2,
  "num_of_pulses": 500
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

## 7. RFPS → RSEC

### 7.1 rfps_iq_logging_response <a id="rfps_iq_logging_response"></a>

CmdUnitID `0x1076` · IRS §5.9.1

| Name | Data type | Size | Description |
|---|---|---|---|
| IQ File Logging Completion Status | unsigned char | 1B | 0=not completed, 1=completed |
| Pulse Count | unsigned int | 4B | total pulses IQ-logged |

**HEX (frame):**
```
AA AB BA BB 00 00 00 05 00 64 10 76 00 00 00 07 D0 CC CD DC DD
```
**JSON:**
```json
{
  "frame_variant": 2,
  "cmd_group": 100,
  "cmd_uid": 4214,
  "cmd_uid_hex": "0x1076",
  "body_len": 5,
  "msg_type": "rfps_iq_logging_response",
  "result": 0,
  "num_samples": 2000
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 7.2 rfps_fingerprint_response <a id="rfps_fingerprint_response"></a>

CmdUnitID `0x3507` · IRS §5.9.2 · The largest message in this ICD — 165 bytes, 35 fields.

| Name | Data type | Size | UoM | Description |
|---|---|---|---|---|
| ESM Track Number | unsigned short | 2B | | 1-500 |
| RFPS Track Number | unsigned short | 2B | | 1-500 |
| Library Match Flag | unsigned char | 1B | | 0-1 |
| Database Server Status | unsigned short | 2B | | connection status |
| Carrier Name | 20 bytes ASCII | | | |
| Carrier Class | 20 bytes ASCII | | | |
| Radar Name | 20 bytes ASCII | | | |
| Radar Mode | 10 bytes ASCII | | | |
| Confidence Level | unsigned char | 1B | | 0-100 |
| Type of Emitter Signal | unsigned char | 1B | | Pulsed=0,CW=1,Pulsed-Freq.Agility=2,FM_CW=3,Pulsed-Freq.Diversity=4,QuasiCW=5,IFF-Integration=8,Noise Radar=9,IFF-Transponder=16 |
| Type of Frequency Agility | unsigned char | 1B | | PulseToPulse=1,ScanToScan=2,Random=4,Ramp=8,Sinusoidal=16 |
| Type of PW Agility | unsigned char | 1B | | Regular=1,Agility=2,Doublet=4,Jitter=8 |
| Type of Modulation | unsigned char | 1B | | NoMod=0,Chirp=1,PhaseMod=2,SteppedFM=4,Ph+SteppedFM=6,MulPhaseMod=18,fmcw=3 |
| Type of PRI | unsigned char | 1B | | Dwell&Switch=1,Stagger=2,Jitter=4,Fixed=8,Sliding=16,Wobulating=32,PGRI=64,PseudoRandom=128,Unknown=255 |
| Type of Scan | unsigned char | 1B | | Circular=1,Sector=2,LockOn=3,Conical=4,TWS=5,Complex=6,Raster=7,Helical=8,Palmer=9 |
| Freq Min/Max/Avg | unsigned int × 3 | 4B each (MHz) | |
| PRI Min/Max/Avg | unsigned int × 3 | 4B each (µs) | |
| PRF Avg | unsigned int | 4B (Hz) | |
| PW Min/Max/Avg | unsigned int × 3 | 4B each (ns) | |
| Rise Time / Fall Time | unsigned int × 2 | 4B each (ns) | |
| No Agile Freq | unsigned char | 1B | | |
| No PRI Levels | unsigned char | 1B | | |
| Jitter Percentage | unsigned int | 4B | | 1-33% |
| ChirpRate_FMModFreq | unsigned int | 4B (MHz/µs ×0.0001) | | |
| PhaseCode Length | unsigned short | 2B | | see caveat below |
| PhaseCode_ChipTime | unsigned int | 4B (ns) | | |
| No Freq Steps | unsigned char | 1B | | |
| SteppedFM Min/Max Freq | unsigned int × 2 | 4B each (MHz) | |
| SteppedFM BitTime | unsigned int | 4B (ns) | |
| FM Deviation | unsigned int | 4B (MHz) | |

**⚠ IRS ambiguity, not resolved by this parser:** the IRS's descriptive
Name/Identifier list also mentions a `PhaseCode` *string* field between
`PhaseCode Length` and `PhaseCode_ChipTime`, but the IRS's own typed Data
Element Table never assigns it a byte size — so it is **not** on the wire in
this decoder. If a real RFPS capture does include it, every field after
`phase_code_length` below shifts and this decoder needs updating. Don't
trust `phase_code_chip_time_ns` onward until confirmed against a real
capture.

**HEX (frame):**
```
AA AB BA BB 00 00 00 A5 00 64 35 07 00 01 00 01 01 00 01 41 4E 2F 41 4C 51 2D 58 58 20 52 41 44
41 52 20 20 20 20 00 54 52 41 43 4B 49 4E 47 20 20 20 20 20 20 20 20 20 20 20 20 53 45 41 2D 45
41 47 4C 45 20 20 20 20 20 20 20 20 20 20 20 54 57 53 20 20 20 20 20 20 20 55 02 04 01 00 02 01
00 00 1F 40 00 00 21 34 00 00 20 3A 00 00 00 64 00 00 00 C8 00 00 00 96 00 00 1A 0A 00 00 01 F4
00 00 03 E8 00 00 02 EE 00 00 00 32 00 00 00 32 04 02 00 00 00 05 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 CC CD DC DD
```
**JSON:**
```json
{
  "frame_variant": 2,
  "cmd_group": 100,
  "cmd_uid": 13575,
  "cmd_uid_hex": "0x3507",
  "body_len": 165,
  "msg_type": "rfps_fingerprint_response",
  "esm_track_number": 1,
  "rfps_track_number": 1,
  "library_match_flag": 1,
  "database_server_status": 1,
  "carrier_name": "AN/ALQ-XX RADAR    ",
  "carrier_class": "TRACKING            ",
  "radar_name": "SEA-EAGLE           ",
  "radar_mode": "TWS       ",
  "confidence_level": 85,
  "type_of_emitter_signal": 2,
  "type_of_frequency_agility": 4,
  "type_of_pw_agility": 1,
  "type_of_modulation": 0,
  "type_of_pri": 2,
  "type_of_scan": 1,
  "freq_min_mhz": 8000,
  "freq_max_mhz": 8500,
  "freq_avg_mhz": 8250,
  "pri_min_us": 100,
  "pri_max_us": 200,
  "pri_avg_us": 150,
  "prf_avg_hz": 6666,
  "pw_min_ns": 500,
  "pw_max_ns": 1000,
  "pw_avg_ns": 750,
  "rise_time_ns": 50,
  "fall_time_ns": 50,
  "num_agile_freq": 4,
  "num_pri_levels": 2,
  "jitter_percentage": 5,
  "chirp_rate_fm_mod_freq": 0,
  "phase_code_length": 0,
  "phase_code_chip_time_ns": 0,
  "num_freq_steps": 0,
  "stepped_fm_min_freq_mhz": 0,
  "stepped_fm_max_freq_mhz": 0,
  "stepped_fm_bit_time_ns": 0,
  "fm_deviation_mhz": 0
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

## 8. RSEC → SCU (servo, JHB(E) only)

Variant 3 framing: `[0x24][DataLen][CmdCode][Data][XOR checksum][0x0D]`. All
SCU angle fields use **BAM encoding** (`raw × 360.0/65536.0`), not the ÷10
tenths-of-a-degree scaling every other angle field in this ICD uses.

### 8.1 scu_position_slew <a id="scu_position_slew"></a>

CmdCode `0x01` · IRS §5.11.1 · Single rotation axis — **no elevation field exists**.

| Name | Data type | Size | UoM | Range |
|---|---|---|---|---|
| Position Angle | unsigned short | 2B | deg (BAM, res. 0.0054) | 0-359.99 |

**HEX (frame; raw `16384` = 90.0°):**
```
24 03 01 40 00 42 0D
```
**JSON:**
```json
{
  "frame_variant": 3,
  "cmd_code": 1,
  "cmd_code_hex": "0x01",
  "data_len": 3,
  "msg_type": "scu_position_slew",
  "position_angle_deg": 90
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 8.2 scu_spin <a id="scu_spin"></a>

CmdCode `0x02` · IRS §5.11.2

| Name | Data type | Size | UoM | Range |
|---|---|---|---|---|
| Speed | unsigned char | 1B | rpm | 1-200 |
| Direction | unsigned char | 1B | | `0x00`=CW, `0xFF`=CCW |

**HEX (frame):**
```
24 03 02 2D 00 2C 0D
```
**JSON:**
```json
{
  "frame_variant": 3,
  "cmd_code": 2,
  "cmd_code_hex": "0x02",
  "data_len": 3,
  "msg_type": "scu_spin",
  "speed_rpm": 45,
  "direction": 0
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 8.3 scu_sector_scan <a id="scu_sector_scan"></a>

CmdCode `0x03` · IRS §5.11.3

| Name | Data type | Size | UoM | Range |
|---|---|---|---|---|
| Start Angle | unsigned short | 2B | deg (BAM) | 0-359.99 |
| Stop Angle | unsigned short | 2B | deg (BAM) | 0-359.99 |
| Speed Reserved1 / Direction Reserved2 | 1B + 1B | | | see caveat below |

**⚠** the IRS's own field table labels these last two bytes "Speed
Reserved1"/"Direction Reserved2" — reading like a copy-paste leftover from
the Spin message rather than genuine sector-scan fields. Surfaced here as
raw hex, not invented as `scan_speed_rpm`/`direction`, pending confirmation.

**HEX (frame):**
```
24 07 03 00 00 80 00 00 00 84 0D
```
**JSON:**
```json
{
  "frame_variant": 3,
  "cmd_code": 3,
  "cmd_code_hex": "0x03",
  "data_len": 7,
  "msg_type": "scu_sector_scan",
  "start_angle_deg": 0,
  "stop_angle_deg": 180,
  "reserved_bytes_hex": "00 00"
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 8.4 scu_standby / scu_bit / scu_stop <a id="scu_standby"></a><a id="scu_bit"></a><a id="scu_stop"></a>

CmdCodes `0x04`/`0x05`/`0x06` · IRS §5.11.4 · No body for any of the three — distinguished purely by CmdCode.

**HEX + JSON (STANDBY, `0x04`):**
```
24 01 04 05 0D
```
```json
{"frame_variant":3,"cmd_code":4,"cmd_code_hex":"0x04","data_len":1,"msg_type":"scu_standby"}
```

**HEX + JSON (BIT, `0x05`):**
```
24 01 05 04 0D
```
```json
{"frame_variant":3,"cmd_code":5,"cmd_code_hex":"0x05","data_len":1,"msg_type":"scu_bit"}
```

**HEX + JSON (STOP, `0x06`):**
```
24 01 06 07 0D
```
```json
{"frame_variant":3,"cmd_code":6,"cmd_code_hex":"0x06","data_len":1,"msg_type":"scu_stop"}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

## 9. SCU → RSEC

### 9.1 scu_continuous_feedback <a id="scu_continuous_feedback"></a>

CmdCode `0xA0`* · IRS §5.12.1

| Name | Data type | Size | Description |
|---|---|---|---|
| Feedback Code | unsigned char | 1B | bit0=BIT cmd checksum ok, bit1=BIT cmd received, bit2=BIT ready for operation, bits3-5=reserved, bits6-7=Current SCU Status (00=emergency stop,10=manual,11=auto) |
| Servo Status | unsigned char | 1B | bit0=encoder ok, bit1=amplifier ok, bits2-7=reserved |
| Azimuth | unsigned short | 2B | deg (BAM, res. 0.0054), 0-360 |

**⚠ `0xA0` is not an IRS-assigned wire byte.** The IRS only documents
RSEC→SCU command codes (`0x5001`-`0x5006`); it never states the actual wire
`CmdCode` byte the SCU uses when *sending* Continuous Feedback back to RSEC.
`0xA0` is a placeholder so the dispatcher has a code path — **confirm the
real byte with BEL before relying on this**, or this message will currently
fall through to `scu_unknown` against a real SCU.

**HEX (frame):**
```
24 05 A0 C7 03 40 00 21 0D
```
**JSON:**
```json
{
  "frame_variant": 3,
  "cmd_code": 160,
  "cmd_code_hex": "0xA0",
  "data_len": 5,
  "msg_type": "scu_continuous_feedback",
  "bit_cmd_checksum_ok": true,
  "bit_cmd_received": true,
  "bit_ready_for_operation": true,
  "current_scu_status": 3,
  "servo_encoder_ok": true,
  "servo_amplifier_ok": true,
  "azimuth_deg": 90
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

## 10. GNSS → RSEC

Variant 4 framing: plain NMEA-0183 ASCII, `$TTMMM,f1,...,fN*CS\r\n`, no binary header/footer.

### 10.1 gnss_rmc <a id="gnss_rmc"></a>

IRS §5.10.1.1 ("Receiving of GPS Positional information in $GPRMC sentence")

**RAW:**
```
$GPRMC,083559.00,A,2000.00,N,07830.00,E,0.5,089.9,010124,,,A*61
```
**JSON:**
```json
{
  "frame_variant": 4,
  "sentence_type": "GPRMC",
  "msg_type": "gnss_rmc",
  "utc_time": "083559.00",
  "status": "A",
  "date": "010124",
  "latitude_deg": 20,
  "longitude_deg": 78.5,
  "speed_knots": 0.5,
  "true_course_deg": 89.9
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 10.2 gnss_gga <a id="gnss_gga"></a>

IRS §5.10.1.1 ("Receiving of GPS Positional information in $GPGGA sentence" —
note the IRS reuses section number 5.10.1.1 for both $GPRMC and $GPGGA; the
second one should be 5.10.1.2. Documented here as-is.)

**RAW:**
```
$GPGGA,083559.00,2000.00,N,07830.00,E,1,08,0.9,15.0,M,,,,*47
```
**JSON:**
```json
{
  "frame_variant": 4,
  "sentence_type": "GPGGA",
  "msg_type": "gnss_gga",
  "utc_time": "083559.00",
  "latitude_deg": 20,
  "longitude_deg": 78.5,
  "fix_quality": 1,
  "num_satellites": 8,
  "hdop": 0.9,
  "altitude_m": 15
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)

---

### 10.3 gnss_hdt <a id="gnss_hdt"></a>

Mentioned in IRS p.78 (`$GPHDT, Heading in Degrees`) but never given its own
Data Element Table in the IRS body — decoded here on the strength of the
standard NMEA-0183 `$--HDT` sentence definition, not an IRS table.

**RAW:**
```
$GPHDT,274.5,T*CB
```
**JSON:**
```json
{
  "frame_variant": 4,
  "sentence_type": "GPHDT",
  "msg_type": "gnss_hdt",
  "heading_deg_true": 274.5
}
```
[back to top](#rsec--himshakti-binary-icd-to-json-reference)
