// drs-bridge/parsers/dp_ecm/src/hf/format/mrx_g6_format.cpp
// MRx Group 6 — FH Monitoring / GO2Monitor: format-side dispatcher.
// All response units for this group are zero-payload ACKs.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
#include <cstring>
using namespace sdfc;

// =============================================================================
// MRx Group 6 format dispatcher
// =============================================================================

int mrx_g6_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    (void)json; (void)buf; (void)max_len;
    switch (unit_id) {
        case  8: case 10: case 12: case 14: case 16: is_ack = true; return 0;
        default: return 0;
    }
}
