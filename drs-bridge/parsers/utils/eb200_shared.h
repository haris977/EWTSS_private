#pragma once

// Shared R&S EB200 mass-data protocol decoding, used by DDF-550 and
// DDF-1GTX (the two EB200-protocol siblings within the RDFS family; CA120
// and JHF do not use this protocol at all). Constants, enum-to-string
// tables, and the packet decoder are byte-for-byte identical between the
// two ICDs for everything covered here -- only channel/msg_kind/root-tag
// classification stays per-device (see each parser's own
// classify_xml_root / impl_parse_xml_*).

#include <cstdint>
#include <string>

#include "json_writer.h"

// ---------------------------------------------------------------------------
// EB200 and framing constants  (§5.1.7, §3.3.1 in both ICDs)
// ---------------------------------------------------------------------------

constexpr uint32_t EB200_MAGIC     = 0x000EB200u;  // BE: 00 0E B2 00
constexpr int      EB200_HDR_BYTES = 16;

// Conventional GenericAttribute header (Table 3): tag(2) + len(2)
constexpr int GA_CONV_HDR = 4;
// Advanced GenericAttribute header (Table 4): tag(2) + res(2) + len(4) + res(16)
constexpr int GA_ADV_HDR  = 24;

// Conventional TraceAttribute header (Table 6): n(2) + res(1) + opt_len(1) + flags(4)
constexpr int TA_CONV_HDR = 8;
// Advanced TraceAttribute header (Table 7): n(4)+res(4)+opt_len(4)+flags_lo(4)+flags_hi(4)+res(16)
constexpr int TA_ADV_HDR  = 36;

// Audio optional header (Table 12)
constexpr int AUDIO_OPT_HDR_BYTES = 42;

// Selector flag bits (Table 8)
constexpr uint32_t SEL_OPTIONAL_HEADER = 0x80000000u;

// Trace tag decimal values (Table 5 / §4.6 eTRACETAG)
constexpr uint16_t TAG_AUDIO      = 401u;
constexpr uint16_t TAG_IFPAN      = 501u;
constexpr uint16_t TAG_CW         = 801u;
constexpr uint16_t TAG_IF_IQ      = 901u;
constexpr uint16_t TAG_VIDEO      = 1001u;
constexpr uint16_t TAG_VIDEOPAN   = 1101u;
constexpr uint16_t TAG_PSCAN      = 1201u;
constexpr uint16_t TAG_SELCALL    = 1301u;
constexpr uint16_t TAG_GPSCOMPASS = 1801u;
constexpr uint16_t TAG_ANTLEVEL   = 1901u;
constexpr uint16_t TAG_DFPSCAN    = 5301u;  // advanced
constexpr uint16_t TAG_SIGP       = 5501u;  // advanced
constexpr uint16_t TAG_HRPAN      = 5601u;  // advanced

// Tags >= 5000 use the advanced GenericAttribute/TraceAttribute layout
constexpr uint16_t TAG_ADVANCED_THRESHOLD = 5000u;

// ---------------------------------------------------------------------------
// Enum-to-string helpers
// ---------------------------------------------------------------------------

const char* trace_tag_name(uint16_t tag);
const char* demod_str(uint16_t v);

// ---------------------------------------------------------------------------
// Audio optional header decoder  (Table 12, §5.1.7.7)
// ---------------------------------------------------------------------------

void decode_audio_opt_header(const uint8_t* oh, int oh_bytes, sdfc::JsonWriter& j);

// ---------------------------------------------------------------------------
// parse_eb200 — decodes one full EB200 packet into JSON.
// `hw` is the only per-device value ("ddf550" / "ddf1gtx") -- everything
// else about the packet layout is identical between the two ICDs.
// ---------------------------------------------------------------------------

std::string parse_eb200(const uint8_t* pkt, int pkt_len, const char* hw);
