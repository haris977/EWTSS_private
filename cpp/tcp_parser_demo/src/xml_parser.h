#pragma once
#include <cstddef>

// Parse an EW hardware XML message to the same JSON schema the binary parser
// produces (group_id, unit_id, status, and type-specific payload fields).
//
// Expected XML structure (hardware sends this for the XML-protocol channels):
//
//   <DrsMessage GroupId="101" UnitId="40" Status="0" Timestamp="1748786400">
//     <HopperCount>2</HopperCount>
//     <Hoppers>
//       <Hopper Channel="14" FrequencyHz="48350000" PowerDbm="-61.2" DwellUs="4000"/>
//       <Hopper Channel="27" FrequencyHz="56750000" PowerDbm="-58.8" DwellUs="3900"/>
//     </Hoppers>
//   </DrsMessage>
//
// xml:      null-terminated XML string (whole message, not a stream fragment)
// out_json: caller-provided output buffer
// cap:      size of out_json
//
// Returns bytes written (excluding null), or -1 on parse error / buffer overflow.
int parse_xml_to_json(const char* xml, char* out_json, size_t cap);
