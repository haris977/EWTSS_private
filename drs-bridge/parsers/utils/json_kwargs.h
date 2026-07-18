#pragma once

// Shared helpers for hand-scanning the small, flat kwargs JSON that
// format_response() receives (msg_type/id/time/xml_body/scpi_cmd/... --
// never the full generic-mirror tree, just a few caller-supplied fields).
// Used by every SDFC-family parser (CA120, DDF-550, DDF-1GTX, ESME) so the
// escape handling and field-scanning rules are byte-for-byte identical
// everywhere instead of hand-copied per parser.

#include <cstdint>
#include <string>

// Find the byte offset PAST the first occurrence of "</tag>" in xml[0..len).
// Returns -1 if not found.
int xml_closing_end(const uint8_t* xml, int len, const char* tag);

// Extract a JSON string field value from a flat JSON object string.
// Stops at the first unescaped closing quote -- suitable for field values
// that do not contain escaped Unicode sequences beyond the basics.
std::string json_str_field(const char* json, const char* key);

// Extract a JSON integer field value from a flat JSON object string.
// Returns -1 if the key is absent or its value is not a plain integer.
long long json_int_field(const char* json, const char* key);
