using System;
using System.Text.Json.Serialization;

namespace Sg.App.Contracts;

public sealed record TimeSyncStatusDto(
    [property: JsonPropertyName("current_time")]   DateTime CurrentTime,
    [property: JsonPropertyName("ntp_offset_ms")]  double   NtpOffsetMs,
    [property: JsonPropertyName("ntp_jitter_ms")]  double   NtpJitterMs,
    [property: JsonPropertyName("ntp_peer")]       string?  NtpPeer,
    [property: JsonPropertyName("last_sync")]      DateTime LastSync,
    [property: JsonPropertyName("status")]         string   Status);
