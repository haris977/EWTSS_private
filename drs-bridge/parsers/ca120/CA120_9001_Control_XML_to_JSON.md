<a id="ca120--xml-control-path-tcp-9001---xml-to-json-reference"></a>
# CA120 XML to JSON Reference — Control Path (TCP 9001)

Auto-generated / hand-verified reference covering CA120's XML control channel
(Section 5, "Control Commands (XML)", of `R&S-CA120-ICD-V15`, pages 35–101).
For each of the ICD's 27 numbered command entries: the ICD's own literal
Request/Reply XML example, and the exact JSON `pugixml_generic_mirror.cpp`
(the shared XML->JSON mirror used by the ca120/ddf550/ddf1gtx parser DLLs)
produces for that XML today, verified by compiling and running the real
`ca120_parser.cpp` against every sample below (see "Verification method").

Working/review document -- not an architecture spec. Scope is the XML
control channel only; CA120's AMMOS binary data channel (Section 6 of the
ICD) is a separate, unrelated wire format and is **not** covered here.

Total control-path commands: 27

## Verification method

Every XML sample below was transcribed directly from `CA120_ICD_16-10-25.pdf`
pages 35–101 (the ICD's literal request/reply code blocks — these are
syntax-highlighted images in the PDF; the flat-text extraction of the same
PDF drops them, so they must be read from the rendered page, not
`CA120_ICD.txt`). Each sample was then run through a small driver program
that links the real, unmodified `ca120_parser.cpp` +
`pugixml_generic_mirror.cpp` sources and calls `extract_frame` +
`parse_message` exactly as `drs-bridge`'s Python host does, printing the
actual returned JSON. All 27×2 = 54 samples parsed successfully (0
`extract_frame`/`parse_message` failures). Nothing in the JSON columns below
is hand-written or guessed.

## Scope note: unlike DDF-550, this is not a flat command list

DDF-550's control ICD is a flat catalogue of ~100 named commands, each a
`<Command name="...">` leaf under `<Request>`/`<Reply>`. CA120's XML is
**capability-tree style**: each Request/Reply wraps exactly one (or, in two
of the 27 entries below, more than one) root node naming the subsystem being
controlled (`<ResourceManager>`, `<Control>`, `<Tuner>`, `<FFT>`,
`<DetectAndClassify>`, `<FrequencyHopping>`, `<Detector>`,
`<AnalogDemodulator>`, `<XmlCommunicationDevice>`, `<HopperAutoSeparation>`,
`<HopperFilterSeparation>`, `<Classifier>`, `<DigitalDemodulator>`,
`<BitstreamProcessing>`) — you `set`/`get` nodes/attributes inside a
subsystem tree rather than invoking numbered commands. There is no
`Event` example in the ICD's Section 5 (CA120 does support an `<Event>` root
at the wire level — see `test_xml_event` in `tests/test_frames_ca120.cpp` —
but the ICD's command catalogue only documents `Request`/`Reply` pairs).

Not every entry is a GET+SET pair the way DDF-550's usually are — some
commands are GET-only (e.g. `LicenseAllocation`, `Tuner Capabilities`), some
are SET-only (e.g. `Filter Suppress`, `DataStream start/stop`), and a couple
combine 2–3 subsystem roots in a single message (`StartApplication`'s
mcp/search/fh variants; `Enable All ST Processes`'s three Hopper modules).
Each entry below is labeled with whatever direction(s) the ICD actually
documents for it — no GET/SET pair is invented where the ICD doesn't have
one.

---

## 5.1 System / Resource Management

### 5.1.1 Job Creation – StartApplication (MCP/SEARCH/FH)  <a id="StartApplication"></a>

The ICD's own example folds three independent job-creation scenarios (MCP,
SEARCH, FH) into one illustrative code block. Structurally this is one
`<Request>` document containing three sibling `<ResourceManager>` children —
valid XML (same "multiple same-name children under one root" shape already
covered by `test_xml_multi_root_request`), so the mirror handles it exactly
like any other multi-child body: `resource_manager` becomes a JSON array.

**Request XML (from ICD):**
```xml
<Request type="set" id="16" time="6000000">
  <ResourceManager>
    <StartApplication type="mcp">
      <ResourceDemandClass>hw</ResourceDemandClass>
      <Tuner>1c7bf18d-c5f5-462b-8076-48661db329f1</Tuner>
      <FFP>
        <LocalStorage datastreams="if,symbol">activated</LocalStorage>
        <ResourceDemandClass>300k</ResourceDemandClass>
        <IgnoreError>0</IgnoreError>
        <Setup>
          <Tuner>
            <Parameters>
              <Coupling>1</Coupling>
            </Parameters>
          </Tuner>
        </Setup>
      </FFP>
    </StartApplication>
  </ResourceManager>
  <ResourceManager>
    <StartApplication type="search">
      <ResourceDemandClass>standard</ResourceDemandClass>
      <Tuner>1c7bf18d-c5f5-462b-8076-48661db329f1</Tuner>
      <DCP type="detector" mode="multiChannel">
        <ResourceDemandClass>hw</ResourceDemandClass>
        <FFP>
          <ResourceDemandClass>300k</ResourceDemandClass>
          <IgnoreError>0</IgnoreError>
          <Channels required="1">1</Channels>
        </FFP>
      </DCP>
      <Setup>
        <DetectAndClassify>
          <Parameters>
            <ChangeToProduction>1</ChangeToProduction>
          </Parameters>
        </DetectAndClassify>
      </Setup>
    </StartApplication>
  </ResourceManager>
  <ResourceManager>
    <StartApplication type="fh">
      <ResourceDemandClass>standard</ResourceDemandClass>
      <LocalStorage datastreams="hopDensity,histogram">activated</LocalStorage>
      <FHSetup>
        <Tuner>
          <Synthesizer>
            <Quantity>8</Quantity>
            <Bandwidth unit="Hz">300000</Bandwidth>
          </Synthesizer>
        </Tuner>
      </FHSetup>
      <FHA>
        <ResourceDemandClass>standard</ResourceDemandClass>
        <DCP>
          <ResourceDemandClass>hw</ResourceDemandClass>
          <Tuner>1c7bf18d-c5f5-462b-8076-48661db329f1</Tuner>
        </DCP>
      </FHA>
    </StartApplication>
  </ResourceManager>
</Request>
```

**Request JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ca120",
  "channel": "xml",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "16",
      "time": "6000000",
      "resource_manager": [
        {
          "start_application": {
            "type": "mcp",
            "resource_demand_class": "hw",
            "tuner": "1c7bf18d-c5f5-462b-8076-48661db329f1",
            "ffp": {
              "local_storage": { "datastreams": "if,symbol", "#text": "activated" },
              "resource_demand_class": "300k",
              "ignore_error": "0",
              "setup": { "tuner": { "parameters": { "coupling": "1" } } }
            }
          }
        },
        {
          "start_application": {
            "type": "search",
            "resource_demand_class": "standard",
            "tuner": "1c7bf18d-c5f5-462b-8076-48661db329f1",
            "dcp": {
              "type": "detector",
              "mode": "multiChannel",
              "resource_demand_class": "hw",
              "ffp": {
                "resource_demand_class": "300k",
                "ignore_error": "0",
                "channels": { "required": "1", "#text": "1" }
              }
            },
            "setup": {
              "detect_and_classify": { "parameters": { "change_to_production": "1" } }
            }
          }
        },
        {
          "start_application": {
            "type": "fh",
            "resource_demand_class": "standard",
            "local_storage": { "datastreams": "hopDensity,histogram", "#text": "activated" },
            "fh_setup": {
              "tuner": { "synthesizer": { "quantity": "8", "bandwidth": { "unit": "Hz", "#text": "300000" } } }
            },
            "fha": {
              "resource_demand_class": "standard",
              "dcp": { "resource_demand_class": "hw", "tuner": "1c7bf18d-c5f5-462b-8076-48661db329f1" }
            }
          }
        }
      ]
    }
  }
}
```

**Reply XML (from ICD).** Note the ICD's own example uses a single
`<ResourceManager>` wrapping three sibling `<StartApplication>` elements —
asymmetric with the request's three separate `<ResourceManager>` wrappers.
Also note the ICD literally writes `id="##"` as a placeholder (not a real
number) and `...` for every IP/Port/GUID value; both are transcribed
verbatim since that's what the ICD shows:
```xml
<Reply type="set" id="##">
  <ResourceManager>
    <StartApplication type="mcp">
      <IP>...</IP>
      <Port>...</Port>
      <GUID>...</GUID>
      <FFP>
        <IP>...</IP>
        <Port>...</Port>
        <GUID>...</GUID>
      </FFP>
    </StartApplication>
    <StartApplication type="search">
      <IP>...</IP>
      <Port>...</Port>
      <GUID>...</GUID>
      <Resources>
        <GUID>...</GUID>
        <GUID>...</GUID>
      </Resources>
    </StartApplication>
    <StartApplication type="fh">
      <IP>...</IP>
      <Port>...</Port>
      <GUID>...</GUID>
      <FHA>
        <IP>...</IP>
        <Port>...</Port>
        <GUID>...</GUID>
        <DCP>
          <IP>...</IP>
          <Port>...</Port>
          <GUID>...</GUID>
        </DCP>
      </FHA>
      <FFP>
        <GUID>...</GUID>
      </FFP>
    </StartApplication>
  </ResourceManager>
</Reply>
```

**Reply JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ca120",
  "channel": "xml",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "##",
      "resource_manager": {
        "start_application": [
          {
            "type": "mcp",
            "ip": "...", "port": "...", "guid": "...",
            "ffp": { "ip": "...", "port": "...", "guid": "..." }
          },
          {
            "type": "search",
            "ip": "...", "port": "...", "guid": "...",
            "resources": { "guid": ["...", "..."] }
          },
          {
            "type": "fh",
            "ip": "...", "port": "...", "guid": "...",
            "fha": {
              "ip": "...", "port": "...", "guid": "...",
              "dcp": { "ip": "...", "port": "...", "guid": "..." }
            },
            "ffp": { "guid": "..." }
          }
        ]
      }
    }
  }
}
```

### 5.1.2 LicenseAllocation (Get)  <a id="LicenseAllocation"></a>

**Request XML (from ICD):**
```xml
<Request type="get" id="67388" time="6000000">
  <ResourceManager>
    <LicenseAllocation/>
  </ResourceManager>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "get", "id": "67388", "time": "6000000",
      "resource_manager": { "license_allocation": {} }
    }
  }
}
```

**Reply XML (from ICD)** — the ICD's own template, with `...` placeholders
for every `<Name>`/`<TotalLicenses>`/`<AvailableLicenses>` value:
```xml
<Reply type="get" id="67388">
  <ResourceManager>
    <LicenseAllocation>
      <License>
        <Name>...</Name>
        <License>
          <Name>...</Name>
          <TotalLicenses>...</TotalLicenses>
          <AvailableLicenses>...</AvailableLicenses>
          <License>
            <Name>...</Name>
          </License>
        </License>
      </License>
    </LicenseAllocation>
  </ResourceManager>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get", "id": "67388",
      "resource_manager": {
        "license_allocation": {
          "license": {
            "name": "...",
            "license": {
              "name": "...",
              "total_licenses": "...",
              "available_licenses": "...",
              "license": { "name": "..." }
            }
          }
        }
      }
    }
  }
}
```

### 5.1.3 XmlCommunicationDevice – Filter Suppress (Set)  <a id="FilterSuppress"></a>

**Request XML (from ICD):**
```xml
<Request type="set" id="67406" time="6000000">
  <XmlCommunicationDevice>
    <Filter type="suppress">
      <Detector>
        <Input>
          <All/>
        </Input>
        <Parameters>
          <All/>
        </Parameters>
      </Detector>
      <AdvancedFilter>
        <Detector syntax="converter">
          <Input/>
        </Detector>
        <Detector syntax="converter">
          <Parameters/>
        </Detector>
        <Tuner syntax="converter">
          <ConnectionEstablishment/>
        </Tuner>
      </AdvancedFilter>
      <Tuner>
        <ConnectionEstablishment>
          <All/>
        </ConnectionEstablishment>
      </Tuner>
    </Filter>
  </XmlCommunicationDevice>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "set", "id": "67406", "time": "6000000",
      "xml_communication_device": {
        "filter": {
          "type": "suppress",
          "detector": { "input": { "all": {} }, "parameters": { "all": {} } },
          "advanced_filter": {
            "detector": [
              { "syntax": "converter", "input": {} },
              { "syntax": "converter", "parameters": {} }
            ],
            "tuner": { "syntax": "converter", "connection_establishment": {} }
          },
          "tuner": { "connection_establishment": { "all": {} } }
        }
      }
    }
  }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="set" id="67406" time="5999898">
  <XmlCommunicationDevice>
    <Filter type="suppress"/>
  </XmlCommunicationDevice>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set", "id": "67406", "time": "5999898",
      "xml_communication_device": { "filter": { "type": "suppress" } }
    }
  }
}
```

### 5.1.4 Control – RunningMode (Get)  <a id="RunningModeGet"></a>

**Request XML (from ICD):**
```xml
<Request type="get" id="67426" time="6000000">
  <Control>
    <RunningMode/>
  </Control>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": { "type": "get", "id": "67426", "time": "6000000", "control": { "running_mode": {} } }
  }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="get" id="67426">
  <Control>
    <RunningMode>1</RunningMode>
  </Control>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": { "type": "get", "id": "67426", "control": { "running_mode": "1" } }
  }
}
```

---

## 5.2 Signal Processing & Analysis

### 5.2.1 FFT – Parameters (Module Level Configuration Query)  <a id="FFTModuleLevel"></a>

**Request XML (from ICD):**
```xml
<Request type="get" id="67443" time="6000000">
  <FFT>
    <Parameters/>
  </FFT>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": { "request": { "type": "get", "id": "67443", "time": "6000000", "fft": { "parameters": {} } } }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="get" id="67443">
  <FFT>
    <Parameters>
      <FFT_Length>1024</FFT_Length>
      <WindowType>HANN</WindowType>
      <Speed>20</Speed>
      <DisplayMode>average</DisplayMode>
      <AverageTime unit="us">0</AverageTime>
      <MeasurementTime unit="us">0</MeasurementTime>
      <CalculationMode>baseband</CalculationMode>
      <TimeSignalType>none</TimeSignalType>
      <Bandwidth unit="Hz">0</Bandwidth>
      <FrequencyOffset unit="Hz">0</FrequencyOffset>
    </Parameters>
  </FFT>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get", "id": "67443",
      "fft": {
        "parameters": {
          "fft_length": "1024",
          "window_type": "HANN",
          "speed": "20",
          "display_mode": "average",
          "average_time": { "unit": "us", "#text": "0" },
          "measurement_time": { "unit": "us", "#text": "0" },
          "calculation_mode": "baseband",
          "time_signal_type": "none",
          "bandwidth": { "unit": "Hz", "#text": "0" },
          "frequency_offset": { "unit": "Hz", "#text": "0" }
        }
      }
    }
  }
}
```

### 5.2.2 FFT Parameters (System Level Query)  <a id="FFTSystemLevel"></a>

Same request/reply shape as 5.2.1 — the ICD documents this as a distinct
catalogue entry (different `id`) even though the XML surface is identical.

**Request XML (from ICD):**
```xml
<Request type="get" id="70147" time="6000000">
  <FFT>
    <Parameters/>
  </FFT>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": { "request": { "type": "get", "id": "70147", "time": "6000000", "fft": { "parameters": {} } } }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="get" id="70147">
  <FFT>
    <Parameters>
      <FFT_Length>1024</FFT_Length>
      <WindowType>HANN</WindowType>
      <Speed>20</Speed>
      <DisplayMode>average</DisplayMode>
      <AverageTime unit="us">0</AverageTime>
      <MeasurementTime unit="us">0</MeasurementTime>
      <CalculationMode>baseband</CalculationMode>
      <TimeSignalType>none</TimeSignalType>
      <Bandwidth unit="Hz">0</Bandwidth>
      <FrequencyOffset unit="Hz">0</FrequencyOffset>
    </Parameters>
  </FFT>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get", "id": "70147",
      "fft": {
        "parameters": {
          "fft_length": "1024", "window_type": "HANN", "speed": "20", "display_mode": "average",
          "average_time": { "unit": "us", "#text": "0" },
          "measurement_time": { "unit": "us", "#text": "0" },
          "calculation_mode": "baseband", "time_signal_type": "none",
          "bandwidth": { "unit": "Hz", "#text": "0" },
          "frequency_offset": { "unit": "Hz", "#text": "0" }
        }
      }
    }
  }
}
```

### 5.2.3 Analog Demodulator Parameters (Get)  <a id="AnalogDemodulatorGet"></a>

**Request XML (from ICD)** — note the ICD reuses `id="70147"` here too:
```xml
<Request type="get" id="70147" time="6000000">
  <AnalogDemodulator>
    <Parameters/>
  </AnalogDemodulator>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": { "request": { "type": "get", "id": "70147", "time": "6000000", "analog_demodulator": { "parameters": {} } } }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="get" id="70147">
  <AnalogDemodulator>
    <Parameters>
      <FrequencyOffset unit="Hz">0</FrequencyOffset>
      <ModulationType>AM</ModulationType>
      <Bandwidth unit="Hz">12000</Bandwidth>
      <AudioSampleRate>medium</AudioSampleRate>
      <BFO_Frequency unit="Hz">1000</BFO_Frequency>
      <SquelchLevel unit="dBuV">0</SquelchLevel>
      <SquelchStatus>0</SquelchStatus>
      <GainControlMode>AGCDefault</GainControlMode>
      <GainControlValue unit="dB">0</GainControlValue>
    </Parameters>
  </AnalogDemodulator>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get", "id": "70147",
      "analog_demodulator": {
        "parameters": {
          "frequency_offset": { "unit": "Hz", "#text": "0" },
          "modulation_type": "AM",
          "bandwidth": { "unit": "Hz", "#text": "12000" },
          "audio_sample_rate": "medium",
          "bfo_frequency": { "unit": "Hz", "#text": "1000" },
          "squelch_level": { "unit": "dBuV", "#text": "0" },
          "squelch_status": "0",
          "gain_control_mode": "AGCDefault",
          "gain_control_value": { "unit": "dB", "#text": "0" }
        }
      }
    }
  }
}
```

### 5.2.4 Analog Demodulator Parameters (Set)  <a id="AnalogDemodulatorSet"></a>

**Request XML (from ICD):**
```xml
<Request type="set" id="78249" time="6000000">
  <AnalogDemodulator>
    <Parameters>
      <FrequencyOffset unit="Hz">-276</FrequencyOffset>
      <ModulationType>FM</ModulationType>
      <Bandwidth unit="Hz">176478</Bandwidth>
      <AudioSampleRate>high</AudioSampleRate>
      <BFO_Frequency unit="Hz">0</BFO_Frequency>
      <SquelchLevel unit="dBuV">0</SquelchLevel>
      <SquelchStatus>0</SquelchStatus>
      <GainControlMode>AGCDefault</GainControlMode>
      <GainControlValue unit="dB">0</GainControlValue>
    </Parameters>
  </AnalogDemodulator>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "set", "id": "78249", "time": "6000000",
      "analog_demodulator": {
        "parameters": {
          "frequency_offset": { "unit": "Hz", "#text": "-276" },
          "modulation_type": "FM",
          "bandwidth": { "unit": "Hz", "#text": "176478" },
          "audio_sample_rate": "high",
          "bfo_frequency": { "unit": "Hz", "#text": "0" },
          "squelch_level": { "unit": "dBuV", "#text": "0" },
          "squelch_status": "0",
          "gain_control_mode": "AGCDefault",
          "gain_control_value": { "unit": "dB", "#text": "0" }
        }
      }
    }
  }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="set" id="78249">
  <AnalogDemodulator>
    <Parameters/>
  </AnalogDemodulator>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": { "reply": { "type": "set", "id": "78249", "analog_demodulator": { "parameters": {} } } }
}
```

### 5.2.5 Control – Running Mode (Set)  <a id="RunningModeSet"></a>

**Request XML (from ICD):**
```xml
<Request type="set" id="71680" time="6000000">
  <Control>
    <RunningMode>1</RunningMode>
  </Control>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": { "request": { "type": "set", "id": "71680", "time": "6000000", "control": { "running_mode": "1" } } }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="set" id="71680">
  <Control>
    <RunningMode/>
  </Control>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": { "reply": { "type": "set", "id": "71680", "control": { "running_mode": {} } } }
}
```

### 5.2.6 Detector – Parameters (Get)  <a id="DetectorGet"></a>

**Request XML (from ICD):**
```xml
<Request type="get" id="67510" time="6000000">
  <Detector>
    <Parameters/>
  </Detector>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": { "request": { "type": "get", "id": "67510", "time": "6000000", "detector": { "parameters": {} } } }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="get" id="67510">
  <Detector>
    <Parameters>
      <SearchBandwidth>auto</SearchBandwidth>
      <SNR unit="dB">5</SNR>
      <Threshold>robust</Threshold>
      <ShortTimeSearchBandwidth>auto</ShortTimeSearchBandwidth>
      <ShortTimeThreshold>robust</ShortTimeThreshold>
      <ShortTimeSNR unit="dB">5</ShortTimeSNR>
      <BurstMinDuration unit="us">1200</BurstMinDuration>
      <BurstMaxDuration unit="us">120000</BurstMaxDuration>
    </Parameters>
  </Detector>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get", "id": "67510",
      "detector": {
        "parameters": {
          "search_bandwidth": "auto",
          "snr": { "unit": "dB", "#text": "5" },
          "threshold": "robust",
          "short_time_search_bandwidth": "auto",
          "short_time_threshold": "robust",
          "short_time_snr": { "unit": "dB", "#text": "5" },
          "burst_min_duration": { "unit": "us", "#text": "1200" },
          "burst_max_duration": { "unit": "us", "#text": "120000" }
        }
      }
    }
  }
}
```

### 5.2.7 Detector – Parameters (Set)  <a id="DetectorSet"></a>

**Request XML (from ICD).** Note the request omits `unit="..."` attributes
that the Get reply's equivalent fields carry (`ShortTimeSNR`,
`BurstMinDuration`, `BurstMaxDuration` are bare integers here) — transcribed
exactly as the ICD shows it:
```xml
<Request type="set" id="73597" time="6000000">
  <Detector>
    <Parameters>
      <ShortTimeSearchBandwidth>wideband_vuhf</ShortTimeSearchBandwidth>
      <ShortTimeThreshold>robust</ShortTimeThreshold>
      <ShortTimeSNR>5</ShortTimeSNR>
      <BurstMinDuration>1200</BurstMinDuration>
      <BurstMaxDuration>120000</BurstMaxDuration>
    </Parameters>
  </Detector>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "set", "id": "73597", "time": "6000000",
      "detector": {
        "parameters": {
          "short_time_search_bandwidth": "wideband_vuhf",
          "short_time_threshold": "robust",
          "short_time_snr": "5",
          "burst_min_duration": "1200",
          "burst_max_duration": "120000"
        }
      }
    }
  }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="set" id="73597">
  <Detector>
    <Parameters/>
  </Detector>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": { "reply": { "type": "set", "id": "73597", "detector": { "parameters": {} } } }
}
```

### 5.2.8 DetectAndClassify – Parameters (Get)  <a id="DetectAndClassifyGet"></a>

**Request XML (from ICD)** — note the ICD reuses `id="67510"` here too
(same id as 5.2.6):
```xml
<Request type="get" id="67510" time="6000000">
  <DetectAndClassify>
    <Parameters/>
  </DetectAndClassify>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": { "request": { "type": "get", "id": "67510", "time": "6000000", "detect_and_classify": { "parameters": {} } } }
}
```

**Reply XML (from ICD).** Note `<ReservedFFPs>` — the shared
`to_snake_case()` splits consecutive-capital runs one letter at a time, so
`FFPs` (three caps + lowercase `s`) becomes `ff_ps`, not `ffps`:
```xml
<Reply type="get" id="67510">
  <DetectAndClassify>
    <Parameters>
      <BitstreamClassification>0</BitstreamClassification>
      <ChangeToProduction>0</ChangeToProduction>
      <ClassProgressRule>fast</ClassProgressRule>
      <ReservedFFPs>0</ReservedFFPs>
      <ScanEnabled>0</ScanEnabled>
      <ScanStartFrequency unit="Hz">70000000</ScanStartFrequency>
      <ScanStopFrequency unit="Hz">150000000</ScanStopFrequency>
      <Ask2ByMorseReplacement>1</Ask2ByMorseReplacement>
      <DecayTime unit="s">40</DecayTime>
    </Parameters>
  </DetectAndClassify>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get", "id": "67510",
      "detect_and_classify": {
        "parameters": {
          "bitstream_classification": "0",
          "change_to_production": "0",
          "class_progress_rule": "fast",
          "reserved_ff_ps": "0",
          "scan_enabled": "0",
          "scan_start_frequency": { "unit": "Hz", "#text": "70000000" },
          "scan_stop_frequency": { "unit": "Hz", "#text": "150000000" },
          "ask2_by_morse_replacement": "1",
          "decay_time": { "unit": "s", "#text": "40" }
        }
      }
    }
  }
}
```

---

## 5.3 Tuner Control

### 5.3.1 Tuner – Parameters (Set Frequency)  <a id="TunerSetFrequency"></a>

**Request XML (from ICD):**
```xml
<Request type="set" id="72293" time="6000000">
  <Tuner>
    <Parameters>
      <Frequency unit="Hz">100000000</Frequency>
    </Parameters>
  </Tuner>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "set", "id": "72293", "time": "6000000",
      "tuner": { "parameters": { "frequency": { "unit": "Hz", "#text": "100000000" } } }
    }
  }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="set" id="72293">
  <Tuner>
    <Parameters/>
  </Tuner>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": { "reply": { "type": "set", "id": "72293", "tuner": { "parameters": {} } } }
}
```

### 5.3.2 Tuner – Parameters (Get Configuration)  <a id="TunerGetConfiguration"></a>

**Request XML (from ICD):**
```xml
<Request type="get" id="67456" time="6000000">
  <Tuner>
    <Parameters/>
  </Tuner>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": { "request": { "type": "get", "id": "67456", "time": "6000000", "tuner": { "parameters": {} } } }
}
```

**Reply XML (from ICD)** — the ICD's largest single reply body (26 fields):
```xml
<Reply type="get" id="67456">
  <Tuner>
    <Parameters>
      <Frequency unit="Hz">110000000</Frequency>
      <Bandwidth unit="Hz">80000000</Bandwidth>
      <AntennaConnectorVUHF_Tuner>X44</AntennaConnectorVUHF_Tuner>
      <AntennaConnectorHF_Tuner>X44</AntennaConnectorHF_Tuner>
      <TunerCommutationFrequency unit="Hz">20000000</TunerCommutationFrequency>
      <AntennaControlAutomatic>0</AntennaControlAutomatic>
      <Preselection>lowDistortion</Preselection>
      <Attenuation unit="dB">4</Attenuation>
      <AttenuationHoldTime unit="us">0</AttenuationHoldTime>
      <AutomaticGainControl>1</AutomaticGainControl>
      <GainControl unit="dBuV">50</GainControl>
      <GainControlTime>default</GainControlTime>
      <IFPanoramaAveragingMode>off</IFPanoramaAveragingMode>
      <IFPanoramaSpan unit="Hz">80000000</IFPanoramaSpan>
      <IFPanoramaStep unit="Hz">25000</IFPanoramaStep>
      <IFPanoramaSelectivity>auto</IFPanoramaSelectivity>
      <PIFPanoramaMode>off</PIFPanoramaMode>
      <PIFPanoramaActivityTime unit="us">15000</PIFPanoramaActivityTime>
      <PIFPanoramaObservationTime unit="us">500000</PIFPanoramaObservationTime>
      <Squelch unit="dBuV">-1</Squelch>
      <MeasurementTime>auto</MeasurementTime>
      <ScanStartFrequency unit="Hz">70000000</ScanStartFrequency>
      <ScanStopFrequency unit="Hz">150000000</ScanStopFrequency>
      <ScanStepFrequency unit="Hz">25000</ScanStepFrequency>
      <ScanEnabled>0</ScanEnabled>
      <Mode>ffm</Mode>
      <AutomaticAttenuation>0</AutomaticAttenuation>
    </Parameters>
  </Tuner>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get", "id": "67456",
      "tuner": {
        "parameters": {
          "frequency": { "unit": "Hz", "#text": "110000000" },
          "bandwidth": { "unit": "Hz", "#text": "80000000" },
          "antenna_connector_vuhf_tuner": "X44",
          "antenna_connector_hf_tuner": "X44",
          "tuner_commutation_frequency": { "unit": "Hz", "#text": "20000000" },
          "antenna_control_automatic": "0",
          "preselection": "lowDistortion",
          "attenuation": { "unit": "dB", "#text": "4" },
          "attenuation_hold_time": { "unit": "us", "#text": "0" },
          "automatic_gain_control": "1",
          "gain_control": { "unit": "dBuV", "#text": "50" },
          "gain_control_time": "default",
          "if_panorama_averaging_mode": "off",
          "if_panorama_span": { "unit": "Hz", "#text": "80000000" },
          "if_panorama_step": { "unit": "Hz", "#text": "25000" },
          "if_panorama_selectivity": "auto",
          "pif_panorama_mode": "off",
          "pif_panorama_activity_time": { "unit": "us", "#text": "15000" },
          "pif_panorama_observation_time": { "unit": "us", "#text": "500000" },
          "squelch": { "unit": "dBuV", "#text": "-1" },
          "measurement_time": "auto",
          "scan_start_frequency": { "unit": "Hz", "#text": "70000000" },
          "scan_stop_frequency": { "unit": "Hz", "#text": "150000000" },
          "scan_step_frequency": { "unit": "Hz", "#text": "25000" },
          "scan_enabled": "0",
          "mode": "ffm",
          "automatic_attenuation": "0"
        }
      }
    }
  }
}
```

### 5.3.3 Tuner – Capabilities (Get)  <a id="TunerCapabilities"></a>

**ICD data-quality note:** the ICD's own literal reply example is a
comment-only placeholder (`<!-- Various capability flags, limits, and
supported parameter lists -->`, no actual child elements), even though the
same section's Parameter Summary table documents ~30 real capability fields
(`SupportsAttMode`, `MinFrequency`, `MaxFrequency`, `BandwidthList`, etc. —
see pages 68–74 of the ICD). This is transcribed and run exactly as the ICD
shows it; the mirror's actual behavior for a populated `<Capabilities>` body
is already proven by every other Get reply in this document (flat
`<Tag>value</Tag>` children each become a `"tag": "value"` JSON field, same
as `Detector`/`AnalogDemodulator`/`Tuner Parameters` above), so no separate
verification is needed once real device output is available.

**Request XML (from ICD):**
```xml
<Request type="get" id="67462" time="6000000">
  <Tuner>
    <Capabilities/>
  </Tuner>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": { "request": { "type": "get", "id": "67462", "time": "6000000", "tuner": { "capabilities": {} } } }
}
```

**Reply XML (from ICD, comment-only placeholder):**
```xml
<Reply type="get" id="67462">
  <Tuner>
    <Capabilities>
      <!-- Various capability flags, limits, and supported parameter lists -->
    </Capabilities>
  </Tuner>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": { "reply": { "type": "get", "id": "67462", "tuner": { "capabilities": {} } } }
}
```

---

## 5.4 Frequency Hopping (FH)

### 5.4.1 HopperAutoSeparation – Parameters (Get)  <a id="HopperAutoSeparationGet"></a>

**Request XML (from ICD):**
```xml
<Request type="get" id="67539" time="6000000">
  <HopperAutoSeparation>
    <Parameters/>
  </HopperAutoSeparation>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": { "request": { "type": "get", "id": "67539", "time": "6000000", "hopper_auto_separation": { "parameters": {} } } }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="get" id="67539">
  <HopperAutoSeparation>
    <Parameters>
      <Duration>1</Duration>
      <Level>0</Level>
      <SymbolRate>0</SymbolRate>
      <TimingSeparation>0</TimingSeparation>
      <RecordUnnamed>0</RecordUnnamed>
      <SeparationDurationMinimum unit="us">100000</SeparationDurationMinimum>
      <SeparationBurstsMinimum>0</SeparationBurstsMinimum>
      <ProfileList listType="replaceCompletely"/>
    </Parameters>
  </HopperAutoSeparation>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get", "id": "67539",
      "hopper_auto_separation": {
        "parameters": {
          "duration": "1", "level": "0", "symbol_rate": "0", "timing_separation": "0",
          "record_unnamed": "0",
          "separation_duration_minimum": { "unit": "us", "#text": "100000" },
          "separation_bursts_minimum": "0",
          "profile_list": { "list_type": "replaceCompletely" }
        }
      }
    }
  }
}
```

### 5.4.2 HopperFilterSeparation – Parameters (Get)  <a id="HopperFilterSeparationGet"></a>

**Request XML (from ICD)** — same `id="67539"` reused a third time:
```xml
<Request type="get" id="67539" time="6000000">
  <HopperFilterSeparation>
    <Parameters/>
  </HopperFilterSeparation>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": { "request": { "type": "get", "id": "67539", "time": "6000000", "hopper_filter_separation": { "parameters": {} } } }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="get" id="67539">
  <HopperFilterSeparation>
    <Parameters>
      <SmartProcessing>0</SmartProcessing>
      <TimingSeparation>0</TimingSeparation>
      <ProfileList listType="replaceCompletely"/>
    </Parameters>
  </HopperFilterSeparation>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get", "id": "67539",
      "hopper_filter_separation": {
        "parameters": {
          "smart_processing": "0", "timing_separation": "0",
          "profile_list": { "list_type": "replaceCompletely" }
        }
      }
    }
  }
}
```

### 5.4.3 FrequencyHopping – HistogramParameters (Set)  <a id="HistogramParameters"></a>

**Request XML (from ICD):**
```xml
<Request type="set" id="75689" time="6000000">
  <FrequencyHopping>
    <HistogramParameters>
      <SignalDurationRange unit="us" info="min,max">647,107742</SignalDurationRange>
      <FrequencyRange unit="Hz" info="min,max">120250784,120251560</FrequencyRange>
      <BandwidthRange unit="Hz" info="min,max">7421,21875</BandwidthRange>
      <SymbolrateRange unit="Bd" info="min,max">0,0</SymbolrateRange>
      <ShiftRange unit="Hz" info="min,max">0,0</ShiftRange>
      <ModulationType>UNKNOWN</ModulationType>
      <ModulationTypeStatus>0</ModulationTypeStatus>
      <AzimuthRange unit="deg" info="min,max">0,0</AzimuthRange>
    </HistogramParameters>
  </FrequencyHopping>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "set", "id": "75689", "time": "6000000",
      "frequency_hopping": {
        "histogram_parameters": {
          "signal_duration_range": { "unit": "us", "info": "min,max", "#text": "647,107742" },
          "frequency_range": { "unit": "Hz", "info": "min,max", "#text": "120250784,120251560" },
          "bandwidth_range": { "unit": "Hz", "info": "min,max", "#text": "7421,21875" },
          "symbolrate_range": { "unit": "Bd", "info": "min,max", "#text": "0,0" },
          "shift_range": { "unit": "Hz", "info": "min,max", "#text": "0,0" },
          "modulation_type": "UNKNOWN",
          "modulation_type_status": "0",
          "azimuth_range": { "unit": "deg", "info": "min,max", "#text": "0,0" }
        }
      }
    }
  }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="set" id="75689">
  <FrequencyHopping>
    <HistogramParameters/>
  </FrequencyHopping>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": { "reply": { "type": "set", "id": "75689", "frequency_hopping": { "histogram_parameters": {} } } }
}
```

### 5.4.4 HopperFilterSeparation – ProcessingStatus (Set)  <a id="HopperFilterSeparationProcessingStatus"></a>

**ICD data-quality note:** the ICD's own literal Reply example for this
section is `<Reply type="set" id="75689"><FrequencyHopping><HistogramParameters/></FrequencyHopping></Reply>`
— byte-for-byte the *previous* section's (5.4.3) reply, with a mismatched
`id` (75689, not this request's 69580) and the wrong root element
(`FrequencyHopping`/`HistogramParameters` instead of
`HopperFilterSeparation`/`ProcessingStatus`). This looks like a copy-paste
error in the ICD authoring, not a real protocol quirk. Transcribed and run
exactly as printed so the JSON below matches what a client would actually
get if it trusted the ICD's example literally; do not use this as the basis
for a `format_response` reply-shape assumption without checking a real
device capture first.

**Request XML (from ICD):**
```xml
<Request type="set" id="69580" time="6000000">
  <HopperFilterSeparation>
    <ProcessingStatus>
      <Status>1</Status>
    </ProcessingStatus>
  </HopperFilterSeparation>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "set", "id": "69580", "time": "6000000",
      "hopper_filter_separation": { "processing_status": { "status": "1" } }
    }
  }
}
```

**Reply XML (from ICD — see data-quality note above):**
```xml
<Reply type="set" id="75689">
  <FrequencyHopping>
    <HistogramParameters/>
  </FrequencyHopping>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": { "reply": { "type": "set", "id": "75689", "frequency_hopping": { "histogram_parameters": {} } } }
}
```

### 5.4.5 System – Enable All ST Processes (Set)  <a id="EnableAllSTProcesses"></a>

**Request XML (from ICD)** — one `<Request>` combining three Hopper module
roots (`HopperAutoSeparation`, `HopperFilterSeparation`,
`HopperOnlineRecombination`) as siblings:
```xml
<Request type="set" id="80001" time="6000000">
  <HopperAutoSeparation>
    <ProcessingStatus>
      <Status>1</Status>
    </ProcessingStatus>
  </HopperAutoSeparation>
  <HopperFilterSeparation>
    <ProcessingStatus>
      <Status>1</Status>
    </ProcessingStatus>
  </HopperFilterSeparation>
  <HopperOnlineRecombination>
    <ProcessingStatus>
      <Status>1</Status>
    </ProcessingStatus>
  </HopperOnlineRecombination>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "set", "id": "80001", "time": "6000000",
      "hopper_auto_separation": { "processing_status": { "status": "1" } },
      "hopper_filter_separation": { "processing_status": { "status": "1" } },
      "hopper_online_recombination": { "processing_status": { "status": "1" } }
    }
  }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="set" id="80001">
  <HopperAutoSeparation>
    <ProcessingStatus/>
  </HopperAutoSeparation>
  <HopperFilterSeparation>
    <ProcessingStatus/>
  </HopperFilterSeparation>
  <HopperOnlineRecombination>
    <ProcessingStatus/>
  </HopperOnlineRecombination>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set", "id": "80001",
      "hopper_auto_separation": { "processing_status": {} },
      "hopper_filter_separation": { "processing_status": {} },
      "hopper_online_recombination": { "processing_status": {} }
    }
  }
}
```

### 5.4.6 FrequencyHopping – QueryHopperProduction (Get)  <a id="QueryHopperProduction"></a>

**Request XML (from ICD)** — same `id="67539"` reused a fourth time:
```xml
<Request type="get" id="67539" time="6000000">
  <FrequencyHopping>
    <QueryHopperProduction/>
  </FrequencyHopping>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": { "request": { "type": "get", "id": "67539", "time": "6000000", "frequency_hopping": { "query_hopper_production": {} } } }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="get" id="67539">
  <FrequencyHopping>
    <QueryHopperProduction>
      <ProductionSetup>
        <GUID>{90674751-b27c-46da-b8a6-b168217de3ce}</GUID>
        <AutoRecording>0</AutoRecording>
        <ProfileNames/>
        <MinCoverage unit="%">50</MinCoverage>
        <RadioID>0</RadioID>
        <Enabled>0</Enabled>
        <CurrentlyActive>0</CurrentlyActive>
      </ProductionSetup>
    </QueryHopperProduction>
  </FrequencyHopping>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get", "id": "67539",
      "frequency_hopping": {
        "query_hopper_production": {
          "production_setup": {
            "guid": "{90674751-b27c-46da-b8a6-b168217de3ce}",
            "auto_recording": "0",
            "profile_names": {},
            "min_coverage": { "unit": "%", "#text": "50" },
            "radio_id": "0",
            "enabled": "0",
            "currently_active": "0"
          }
        }
      }
    }
  }
}
```

---

## 5.5 DataStream

### 5.5.1 Control – DataStream (Set / start)  <a id="DataStreamStart"></a>

**Request XML (from ICD):**
```xml
<Request type="set" id="68917" time="6000000">
  <Control>
    <DataStream action="start" type="tunerSpectrum">
      <!-- Other possible types: detectorSpectrum, detectorThreshold, hopDensity, histogram, demresult, processorSpectrum, decoded, image, analogAudio, digitalAudio -->
      <Protocol>tcp</Protocol>
      <IP>192.168.1.226</IP>
      <Port>14776</Port>
    </DataStream>
  </Control>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "set", "id": "68917", "time": "6000000",
      "control": {
        "data_stream": {
          "action": "start", "type": "tunerSpectrum",
          "protocol": "tcp", "ip": "192.168.1.226", "port": "14776"
        }
      }
    }
  }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="set" id="68917">
  <Control>
    <DataStream action="start" type="tunerSpectrum"/>
  </Control>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set", "id": "68917",
      "control": { "data_stream": { "action": "start", "type": "tunerSpectrum" } }
    }
  }
}
```

### 5.5.2 Control – DataStream (Stop)  <a id="DataStreamStop"></a>

**Request XML (from ICD):**
```xml
<Request type="set" id="69035" time="6000000">
  <Control>
    <DataStream action="stop" type="tunerSpectrum">
      <IP>192.168.1.226</IP>
      <Port>14776</Port>
    </DataStream>
  </Control>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "set", "id": "69035", "time": "6000000",
      "control": { "data_stream": { "action": "stop", "type": "tunerSpectrum", "ip": "192.168.1.226", "port": "14776" } }
    }
  }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="set" id="69035">
  <Control>
    <DataStream action="stop" type="tunerSpectrum"/>
  </Control>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set", "id": "69035",
      "control": { "data_stream": { "action": "stop", "type": "tunerSpectrum" } }
    }
  }
}
```

---

## 5.6 Classifier

### 5.6.1 DetectAndClassify – Enable Emission Results (Set)  <a id="EnableEmissionResults"></a>

**Request XML (from ICD):**
```xml
<Request type="set" id="69089" time="6000000">
  <DetectAndClassify>
    <DetectorUpdates>
      <Pause>0</Pause>
    </DetectorUpdates>
  </DetectAndClassify>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "set", "id": "69089", "time": "6000000",
      "detect_and_classify": { "detector_updates": { "pause": "0" } }
    }
  }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="set" id="69089">
  <DetectAndClassify>
    <DetectorUpdates/>
  </DetectAndClassify>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": { "reply": { "type": "set", "id": "69089", "detect_and_classify": { "detector_updates": {} } } }
}
```

### 5.6.2 DetectAndClassify – Enable Classification Results (Set)  <a id="EnableClassificationResults"></a>

**Request XML (from ICD):**
```xml
<Request type="set" id="69147" time="6000000">
  <DetectAndClassify>
    <ClassificationMode>1</ClassificationMode>
  </DetectAndClassify>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "set", "id": "69147", "time": "6000000",
      "detect_and_classify": { "classification_mode": "1" }
    }
  }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="set" id="69147">
  <DetectAndClassify>
    <ClassificationMode/>
  </DetectAndClassify>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": { "reply": { "type": "set", "id": "69147", "detect_and_classify": { "classification_mode": {} } } }
}
```

### 5.6.3 Classifier – ProcessingStatus (Set)  <a id="ClassifierProcessingStatus"></a>

**Request XML (from ICD):**
```xml
<Request type="set" id="78916" time="6000000">
  <Classifier>
    <ProcessingStatus>
      <Status>1</Status>
    </ProcessingStatus>
  </Classifier>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "set", "id": "78916", "time": "6000000",
      "classifier": { "processing_status": { "status": "1" } }
    }
  }
}
```

**Reply XML (from ICD):**
```xml
<Reply type="set" id="78916">
  <Classifier>
    <ProcessingStatus/>
  </Classifier>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": { "reply": { "type": "set", "id": "78916", "classifier": { "processing_status": {} } } }
}
```

### 5.6.4 Digital Demodulator & Bitstream Processing – Available Demodulators / Decoders (Get)  <a id="AvailableDemodulatorsDecoders"></a>

**Request XML (from ICD)** — two subsystem roots (`DigitalDemodulator`,
`BitstreamProcessing`) as top-level siblings:
```xml
<Request type="get" id="70054" time="6000000">
  <DigitalDemodulator>
    <AvailableDemodulators/>
  </DigitalDemodulator>
  <BitstreamProcessing>
    <AvailableDecoders/>
  </BitstreamProcessing>
</Request>
```
**Request JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "request",
  "body": {
    "request": {
      "type": "get", "id": "70054", "time": "6000000",
      "digital_demodulator": { "available_demodulators": {} },
      "bitstream_processing": { "available_decoders": {} }
    }
  }
}
```

**Reply XML (from ICD)** — the ICD shows one `<DemodulatorInfo>` and one
`<Decoder>` entry, with a comment noting more entries repeat the same shape
for every other available demodulator/decoder:
```xml
<Reply type="get" id="70054">
  <DigitalDemodulator>
    <AvailableDemodulators>
      <DemodulatorInfo>
        <DemodulatorName>ASK2</DemodulatorName>
        <DemodulatorVersion>1</DemodulatorVersion>
        <ModuleID>1048576</ModuleID>
        <ParameterSize>9</ParameterSize>
        <SupportsSymbolData>1</SupportsSymbolData>
        <SupportsIQ_ConstellationData>0</SupportsIQ_ConstellationData>
        <SupportsInstantData>1</SupportsInstantData>
        <SupportsImageData>0</SupportsImageData>
        <SupportsTransmissionData>0</SupportsTransmissionData>
        <SupportsAudioData>0</SupportsAudioData>
        <IsUniversal>1</IsUniversal>
        <SupportsSpecialData>0</SupportsSpecialData>
      </DemodulatorInfo>
      <!-- More DemodulatorInfo nodes follow for each available demodulator -->
    </AvailableDemodulators>
  </DigitalDemodulator>
  <BitstreamProcessing>
    <AvailableDecoders>
      <Decoder id="100000" classificationOnly="1">
        <DecoderName>ASCII</DecoderName>
      </Decoder>
      <!-- More Decoder nodes follow for each available decoder -->
    </AvailableDecoders>
  </BitstreamProcessing>
</Reply>
```
**Reply JSON:**
```json
{
  "hw": "ca120", "channel": "xml", "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get", "id": "70054",
      "digital_demodulator": {
        "available_demodulators": {
          "demodulator_info": {
            "demodulator_name": "ASK2",
            "demodulator_version": "1",
            "module_id": "1048576",
            "parameter_size": "9",
            "supports_symbol_data": "1",
            "supports_iq_constellation_data": "0",
            "supports_instant_data": "1",
            "supports_image_data": "0",
            "supports_transmission_data": "0",
            "supports_audio_data": "0",
            "is_universal": "1",
            "supports_special_data": "0"
          }
        }
      },
      "bitstream_processing": {
        "available_decoders": {
          "decoder": { "id": "100000", "classification_only": "1", "decoder_name": "ASCII" }
        }
      }
    }
  }
}
```

---

## Notes for downstream consumers

- Every reply above that ends in an empty `{}` (e.g. `"parameters": {}`,
  `"running_mode": {}`) is not a parser omission — it's what the ICD's own
  literal `<Tag/>`-self-closing or comment-only example produces once
  mirrored. A real device's Set-ack reply is documented by the ICD as
  exactly this shape (empty node = "accepted, no further data").
- `resource_manager`, `start_application`, and `detector` (in `AdvancedFilter`)
  become JSON **arrays** only when the same tag name repeats as a sibling
  under the same parent — this is the shared mirror's general rule (see
  `pugixml_generic_mirror.h`), not anything CA120-specific.
- Two ICD data-quality issues are called out inline above (§5.3.3's
  comment-only Capabilities reply; §5.4.4's copy-pasted-from-§5.4.3 reply
  example) — flag these if this doc is ever used to write conformance tests
  against real hardware.
- This document covers `parse_message` (decode) only. `format_response`
  (JSON → XML encode) is a separate, not-yet-ported piece of work for CA120
  — see the parser's own `format_response` doc comment in `ca120_parser.cpp`.
