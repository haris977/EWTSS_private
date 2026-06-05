// tcp_parser_demo — demonstrates binary and XML parsing over TCP.
//
// Usage:
//   tcp_parser_demo              — run demo with hardcoded test frames
//   tcp_parser_demo <port>       — start TCP server; auto-detects binary vs XML
//                                  from the first byte of each connection
//
// This program is a LEARNING TOOL, not a production server.
// The real TCP receive loop lives in Python (drs-bridge/src/drs_bridge/);
// this file shows the C++ parser layer in isolation so you can test it
// and understand the patterns before wiring it into the DLL ABI.

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "binary_parser.h"
#include "xml_parser.h"

// ── Test data ─────────────────────────────────────────────────────────────────

// SDFC Response frame: Group 100 (System Management), Unit 2 (System Version).
// Built by hand from the ICD §3.2 layout.
static const uint8_t kBinaryTestFrame[] = {
    // Header: SDFC Response magic (ICD §3.2)
    0xEE, 0xEF, 0xFE, 0xFF,
    // Status: 0 = OK
    0x00, 0x00,
    // Payload size: 26 bytes (0x1A) — System Version is always 26 bytes (ICD §4.4)
    0x1A, 0x00, 0x00, 0x00,
    // Group ID: 100 (System Management)
    0x64, 0x00,
    // Unit ID: 2 (System Version response)
    0x02, 0x00,
    // ── System Version payload (26 bytes) ──────────────────────────────────
    // fw_version     uint32 LE  → 1
    0x01, 0x00, 0x00, 0x00,
    // driver_version uint32 LE  → 2
    0x02, 0x00, 0x00, 0x00,
    // fpga_version   uint32 LE  → 3
    0x03, 0x00, 0x00, 0x00,
    // bsp_version    uint32 LE  → 4
    0x04, 0x00, 0x00, 0x00,
    // processor_id   uint16 LE  → 7
    0x07, 0x00,
    // rf_tuner_ids[0..2] uint16 LE each
    0x01, 0x00,  0x02, 0x00,  0x03, 0x00,
    // fpga_type_id   uint16 LE  → 4
    0x04, 0x00,
    // Footer: SDFC Response footer magic
    0xFF, 0xFE, 0xEF, 0xEE,
    // Total: 14 (header fields) + 26 (payload) + 4 (footer) = 44 bytes
};

// XML message for an FH Detection result (the format the XML-protocol hardware
// channels send instead of binary frames).
static const char kXmlTestMessage[] =
    "<?xml version=\"1.0\"?>\r\n"
    "<DrsMessage GroupId=\"101\" UnitId=\"40\" Status=\"0\" Timestamp=\"1748786400\">\r\n"
    "  <HopperCount>2</HopperCount>\r\n"
    "  <Hoppers>\r\n"
    "    <Hopper Channel=\"14\" FrequencyHz=\"48350000\""
                " PowerDbm=\"-61.2\" DwellUs=\"4000\"/>\r\n"
    "    <Hopper Channel=\"27\" FrequencyHz=\"56750000\""
                " PowerDbm=\"-58.8\" DwellUs=\"3900\"/>\r\n"
    "  </Hoppers>\r\n"
    "</DrsMessage>\r\n";

// ── Demo mode ─────────────────────────────────────────────────────────────────
static void run_demo() {
    char json[4096];

    printf("=== Demo 1: Binary Frame (SDFC Response) ===\n");
    printf("  %zu bytes received, first byte = 0x%02X\n",
           sizeof(kBinaryTestFrame), kBinaryTestFrame[0]);
    printf("  is_binary_frame() = %s\n\n",
           is_binary_frame(kBinaryTestFrame, sizeof(kBinaryTestFrame)) ? "true" : "false");

    BinaryFrame frame = {};
    if (parse_binary_frame(kBinaryTestFrame, sizeof(kBinaryTestFrame), frame)) {
        int n = binary_frame_to_json(frame, json, sizeof(json));
        printf("  JSON output (%d bytes):\n  %s\n\n", n, json);
    } else {
        printf("  ERROR: parse_binary_frame() returned false\n\n");
    }

    printf("=== Demo 2: XML Message (FH Detection) ===\n");
    printf("  %zu bytes received, first byte = '<'\n", strlen(kXmlTestMessage));
    printf("  is_binary_frame() = %s\n\n",
           is_binary_frame((const uint8_t*)kXmlTestMessage, strlen(kXmlTestMessage))
               ? "true" : "false");

    int n = parse_xml_to_json(kXmlTestMessage, json, sizeof(json));
    if (n > 0) {
        printf("  JSON output (%d bytes):\n  %s\n\n", n, json);
    } else {
        printf("  ERROR: parse_xml_to_json() returned %d\n\n", n);
    }

    printf("=== Protocol auto-detection summary ===\n");
    printf("  Binary frame magic bytes: 0xAA (SDFC cmd / SCD compact)"
           " or 0xEE (SDFC response)\n");
    printf("  XML messages always start with '<' (0x3C)\n");
    printf("  Detection rule: first byte == '<'  => XML path\n");
    printf("                  first byte != '<'  => binary path\n");
}

// ── TCP server mode ───────────────────────────────────────────────────────────
// Accepts one connection, reads data until the client disconnects, then exits.
// Demonstrates the receive-buffer pattern you need for TCP stream reassembly:
//
//   Binary channels: you cannot call parse_binary_frame() until the buffer
//   holds the complete frame (header + payload + footer).  Call
//   binary_frame_total_size() first — it reads the length field and tells
//   you how many bytes you need.
//
//   XML channels: buffer until you see the closing root tag.
//
// The 6 binary-protocol channels and the XML-protocol channels connect to
// different TCP ports (one drs-bridge instance per hardware variant).
// Each instance knows its protocol from the YAML profile — no runtime
// detection is needed in production; the detection below is just for this demo.

static void run_server(const char* port_str) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed\n"); return;
    }

    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) {
        printf("socket() failed: %d\n", WSAGetLastError());
        WSACleanup(); return;
    }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((u_short)atoi(port_str));

    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("bind() failed: %d\n", WSAGetLastError());
        closesocket(srv); WSACleanup(); return;
    }
    listen(srv, 1);
    printf("Listening on port %s. Send binary or XML data — auto-detected.\n", port_str);
    printf("(Connect with: echo -ne '\\xEE\\xEF...' | nc 127.0.0.1 %s)\n\n", port_str);

    SOCKET client = accept(srv, nullptr, nullptr);
    if (client == INVALID_SOCKET) {
        printf("accept() failed\n"); closesocket(srv); WSACleanup(); return;
    }
    printf("Client connected.\n");

    // Receive buffer — must be large enough for the biggest frame.
    // COMM DF FFT response is ~6 KB; we allocate 64 KB to be safe (ICD §9).
    static uint8_t rbuf[65536];
    size_t total          = 0;
    bool   protocol_known = false;
    bool   is_xml_channel = false;
    char   json[4096];

    while (true) {
        // Fill the buffer from the socket.
        // recv() returns the bytes available NOW — it may be less than a full frame.
        // That is normal for TCP; the loop handles partial receives.
        int got = recv(client,
                       (char*)(rbuf + total),
                       (int)(sizeof(rbuf) - total - 1), // -1 so we can null-terminate for XML
                       0);
        if (got <= 0) break; // peer disconnected or socket error
        total += (size_t)got;

        // Detect protocol from the very first byte we receive.
        if (!protocol_known && total > 0) {
            is_xml_channel = (rbuf[0] == '<');
            protocol_known = true;
            printf("Protocol detected: %s\n\n", is_xml_channel ? "XML" : "binary");
        }

        if (!is_xml_channel) {
            // ── Binary path ───────────────────────────────────────────────────
            // Keep trying to consume complete frames from the front of rbuf.
            // A single recv() might give us 2 frames — the inner loop handles that.
            while (total > 0) {
                size_t needed = binary_frame_total_size(rbuf, total);
                if (needed == 0) break;           // need more bytes — wait for next recv()
                if (needed == SIZE_MAX) {
                    printf("ERROR: unrecognised binary frame magic — discarding buffer\n");
                    total = 0; break;
                }
                if (total < needed) break;        // frame not yet complete — wait

                BinaryFrame frame = {};
                if (parse_binary_frame(rbuf, total, frame)) {
                    int n = binary_frame_to_json(frame, json, sizeof(json));
                    if (n > 0) printf("JSON: %s\n\n", json);
                }
                // Slide the window: remove the consumed frame from the front.
                total -= needed;
                if (total > 0)
                    memmove(rbuf, rbuf + needed, total);
            }
        } else {
            // ── XML path ──────────────────────────────────────────────────────
            // Buffer until we see the closing root tag.  The hardware always
            // sends one complete XML document per message.
            rbuf[total] = '\0'; // safe: we reserved one extra byte above
            const char* close_tag = "</DrsMessage>";
            if (strstr((char*)rbuf, close_tag)) {
                int n = parse_xml_to_json((char*)rbuf, json, sizeof(json));
                if (n > 0) printf("JSON: %s\n\n", json);
                else       printf("ERROR: XML parse failed\n");
                total = 0; // message consumed — ready for the next one
            }
            // else: closing tag not yet received, loop and wait for more bytes
        }
    }

    printf("Client disconnected.\n");
    closesocket(client);
    closesocket(srv);
    WSACleanup();
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc == 2) {
        run_server(argv[1]);
    } else {
        run_demo();
    }
    return 0;
}
