<a id="ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference"></a>
# DDF-550 XML to JSON Reference — Control Path (TCP 9150) + Preclassifier (TCP 9153/9154)

Auto-generated / hand-verified reference covering all XML channels DDF-550
exposes. Two source ICDs, two parts to this document:

- **Part 1 — Control path (TCP 9150).** Source: `Remote_commands.html`. For
  each of 103 commands: the ICD's own literal XML Request/Reply example,
  and the exact JSON `pugixml_generic_mirror.cpp` (the shared XML->JSON
  mirror used by the ddf550/ca120/ddf1gtx parser DLLs) produces for that
  XML today.
- **Part 2 — Preclassifier (TCP 9153 control / TCP 9154 output).** Source:
  `DDFSystemControlInterfacePreClassifier.pdf` (R&S DDF-SCIF User Manual,
  3025.2887.02 v10) — a completely different ICD from Part 1's, describing
  a different sub-protocol entirely (see that section's intro for details).

Working/review document -- not an architecture spec.

Total control-path commands: 103

---

## 1.1 Calibration-Commands

### 1.1.1 CalibrationSettings  <a id="CalibrationSettings"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | int32 | iCalibrationTimeInterval | Time interval [min] (range 15 to 60: 15 min to 1 h) for periodically calibrating the
                        R&S DDFx                  or validity period of calibration. |
| i/o | boolean | bPeriodicCalibration | Calibration mode (true: periodic, false: triggered). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="CalibrationSettings">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CalibrationSettings"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="CalibrationSettings">
    <Param name="iCalibrationTimeInterval">30</Param>
    <Param name="bPeriodicCalibration">true</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CalibrationSettings",
        "param": [
          {
            "name": "iCalibrationTimeInterval",
            "#text": "30"
          },
          {
            "name": "bPeriodicCalibration",
            "#text": "true"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="CalibrationSettings">
    <Param name="iCalibrationTimeInterval">30</Param>
    <Param name="bPeriodicCalibration">true</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CalibrationSettings",
        "param": [
          {
            "name": "iCalibrationTimeInterval",
            "#text": "30"
          },
          {
            "name": "bPeriodicCalibration",
            "#text": "true"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="CalibrationSettings">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CalibrationSettings"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.1.2 CalibrationTrigger  <a id="CalibrationTrigger"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| in | int32 | iRangeId | ID of scan range or search range to calibrate: 0 to 999. |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="CalibrationTrigger">
    <Param name="iRangeId">0</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CalibrationTrigger",
        "param": {
          "name": "iRangeId",
          "#text": "0"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="CalibrationTrigger">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CalibrationTrigger"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

## 1.2 Common-Commands

### 1.2.1 DfMode  <a id="DfMode"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eDFMODE | eOperationMode | DF mode: Rx, Rx Panorama Scan, FFM (Fixed Frequency Mode), Scan, Search. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="DfMode">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "DfMode"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="DfMode">
    <Param name="eOperationMode">DFMODE_FFM</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "DfMode",
        "param": {
          "name": "eOperationMode",
          "#text": "DFMODE_FFM"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="DfMode">
    <Param name="eOperationMode">DFMODE_FFM</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "DfMode",
        "param": {
          "name": "eOperationMode",
          "#text": "DFMODE_FFM"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="DfMode">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "DfMode"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.2.2 MeasureSettingsFFM  <a id="MeasureSettingsFFM"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | int64 | iFrequency | Main tuner center frequency [Hz] (range depends on installed antennas and options). |
| i/o | eAVERAGE_MODE | eAvgMode | Averaging mode. |
| i/o | eDFPAN_STEP | eDfPanStep | DF channel spacing. |
| i/o | eBLOCK_AVERAGING_SELECT | eBlockAveragingSelect | Select number of averaging cycles or time duration for block averaging. |
| i/o | int32 | iBlockAveragingCycles | Number of averaging cycles (if selected). |
| i/o | int32 | iBlockAveragingTime | Block averaging time [ms] (if selected). |
| i/o | int32 | iThreshold | Level threshold [dBµV] (range -30 to +130). |
| i/o | eANT_POL | eAntPol | Antenna polarization. |
| i/o | eSTATE | eAntPreAmp | Antenna preamplifier on or off. |
| i/o | eDF_ALT | eDfAlt | DF evaluation principle (DF alternative). |
| i/o | eSPAN | eSpan | Realtime bandwidth (frequency span). |
| i/o | eWINDOW_TYPE | eWindowType | FFT window type (function). |
| i/o | eDFPAN_SELECTIVITY | eDfPanSelectivity | Df panorama selectivity. |
| i/o | eATT_SELECT | eAttSelect | Attenuation setting manual or automatic. |
| i/o | int32 | iAttValue | Attenuation [dB] (range 0 to 40) (if manual). |
| i/o | int32 | iAttHoldTime | Time duration [1/10 s] (range 0 to 100: 0 s to 10 s) attenuation value stays unchanged
                        if input level drops. |
| i/o | eIFPAN_STEP | eIFPanStep | IF panorama channel spacing. |
| i/o | eIFPAN_SELECTIVITY | eIFPanSelectivity | IF panorama selectivity. |
| i/o | eIFPAN_MODE | eIFPanMode | IF panorama mode. |
| i/o | int32 | iSrEmitterEstimation | Super-resolution (SR) emitter estimation or predetermined emitter count for all channels
                        (0: work in auto mode; 1 to 9: force specific count)                  (option R&S
                        DDFx-SR, Super-Resolution, only). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="MeasureSettingsFFM">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "MeasureSettingsFFM"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="MeasureSettingsFFM">
    <Param name="iFrequency">1000000000</Param>
    <Param name="eAvgMode">DFSQU_GATE</Param>
    <Param name="eDfPanStep">DFPAN_STEP_20KHZ</Param>
    <Param name="eBlockAveragingSelect">BLOCK_AVERAGING_SELECT_TIME</Param>
    <Param name="iBlockAveragingCycles">200</Param>
    <Param name="iBlockAveragingTime">200</Param>
    <Param name="iThreshold">11</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="eAntPreAmp">STATE_OFF</Param>
    <Param name="eDfAlt">DFALT_AUTO</Param>
    <Param name="eSpan">IFPAN_FREQ_RANGE_500</Param>
    <Param name="eWindowType">DF_WINDOW_TYPE_BLACKMAN_HARRIS</Param>
    <Param name="eDfPanSelectivity">DFPAN_SELECTIVITY_NORMAL</Param>
    <Param name="eAttSelect">ATT_MANUAL</Param>
    <Param name="iAttValue">0</Param>
    <Param name="iAttHoldTime">10</Param>
    <Param name="eIFPanStep">IFPAN_STEP_1KHZ</Param>
    <Param name="eIFPanSelectivity">IFPAN_SELECTIVITY_NORMAL</Param>
    <Param name="eIFPanMode">IFPAN_MODE_AVERAGE</Param>
    <Param name="iSrEmitterEstimation">0</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "MeasureSettingsFFM",
        "param": [
          {
            "name": "iFrequency",
            "#text": "1000000000"
          },
          {
            "name": "eAvgMode",
            "#text": "DFSQU_GATE"
          },
          {
            "name": "eDfPanStep",
            "#text": "DFPAN_STEP_20KHZ"
          },
          {
            "name": "eBlockAveragingSelect",
            "#text": "BLOCK_AVERAGING_SELECT_TIME"
          },
          {
            "name": "iBlockAveragingCycles",
            "#text": "200"
          },
          {
            "name": "iBlockAveragingTime",
            "#text": "200"
          },
          {
            "name": "iThreshold",
            "#text": "11"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "eAntPreAmp",
            "#text": "STATE_OFF"
          },
          {
            "name": "eDfAlt",
            "#text": "DFALT_AUTO"
          },
          {
            "name": "eSpan",
            "#text": "IFPAN_FREQ_RANGE_500"
          },
          {
            "name": "eWindowType",
            "#text": "DF_WINDOW_TYPE_BLACKMAN_HARRIS"
          },
          {
            "name": "eDfPanSelectivity",
            "#text": "DFPAN_SELECTIVITY_NORMAL"
          },
          {
            "name": "eAttSelect",
            "#text": "ATT_MANUAL"
          },
          {
            "name": "iAttValue",
            "#text": "0"
          },
          {
            "name": "iAttHoldTime",
            "#text": "10"
          },
          {
            "name": "eIFPanStep",
            "#text": "IFPAN_STEP_1KHZ"
          },
          {
            "name": "eIFPanSelectivity",
            "#text": "IFPAN_SELECTIVITY_NORMAL"
          },
          {
            "name": "eIFPanMode",
            "#text": "IFPAN_MODE_AVERAGE"
          },
          {
            "name": "iSrEmitterEstimation",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="MeasureSettingsFFM">
    <Param name="iFrequency">1000000000</Param>
    <Param name="eAvgMode">DFSQU_GATE</Param>
    <Param name="eDfPanStep">DFPAN_STEP_20KHZ</Param>
    <Param name="eBlockAveragingSelect">BLOCK_AVERAGING_SELECT_TIME</Param>
    <Param name="iBlockAveragingCycles">200</Param>
    <Param name="iBlockAveragingTime">200</Param>
    <Param name="iThreshold">11</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="eAntPreAmp">STATE_OFF</Param>
    <Param name="eDfAlt">DFALT_AUTO</Param>
    <Param name="eSpan">IFPAN_FREQ_RANGE_500</Param>
    <Param name="eWindowType">DF_WINDOW_TYPE_BLACKMAN_HARRIS</Param>
    <Param name="eDfPanSelectivity">DFPAN_SELECTIVITY_NORMAL</Param>
    <Param name="eAttSelect">ATT_MANUAL</Param>
    <Param name="iAttValue">0</Param>
    <Param name="iAttHoldTime">10</Param>
    <Param name="eIFPanStep">IFPAN_STEP_1KHZ</Param>
    <Param name="eIFPanSelectivity">IFPAN_SELECTIVITY_NORMAL</Param>
    <Param name="eIFPanMode">IFPAN_MODE_AVERAGE</Param>
    <Param name="iSrEmitterEstimation">0</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "MeasureSettingsFFM",
        "param": [
          {
            "name": "iFrequency",
            "#text": "1000000000"
          },
          {
            "name": "eAvgMode",
            "#text": "DFSQU_GATE"
          },
          {
            "name": "eDfPanStep",
            "#text": "DFPAN_STEP_20KHZ"
          },
          {
            "name": "eBlockAveragingSelect",
            "#text": "BLOCK_AVERAGING_SELECT_TIME"
          },
          {
            "name": "iBlockAveragingCycles",
            "#text": "200"
          },
          {
            "name": "iBlockAveragingTime",
            "#text": "200"
          },
          {
            "name": "iThreshold",
            "#text": "11"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "eAntPreAmp",
            "#text": "STATE_OFF"
          },
          {
            "name": "eDfAlt",
            "#text": "DFALT_AUTO"
          },
          {
            "name": "eSpan",
            "#text": "IFPAN_FREQ_RANGE_500"
          },
          {
            "name": "eWindowType",
            "#text": "DF_WINDOW_TYPE_BLACKMAN_HARRIS"
          },
          {
            "name": "eDfPanSelectivity",
            "#text": "DFPAN_SELECTIVITY_NORMAL"
          },
          {
            "name": "eAttSelect",
            "#text": "ATT_MANUAL"
          },
          {
            "name": "iAttValue",
            "#text": "0"
          },
          {
            "name": "iAttHoldTime",
            "#text": "10"
          },
          {
            "name": "eIFPanStep",
            "#text": "IFPAN_STEP_1KHZ"
          },
          {
            "name": "eIFPanSelectivity",
            "#text": "IFPAN_SELECTIVITY_NORMAL"
          },
          {
            "name": "eIFPanMode",
            "#text": "IFPAN_MODE_AVERAGE"
          },
          {
            "name": "iSrEmitterEstimation",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="MeasureSettingsFFM">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "MeasureSettingsFFM"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

## 1.3 Demodulation-Commands

### 1.3.1 AudioFilterMode  <a id="AudioFilterMode"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eAF_FILTER_MODE | eAudioFilterMode | Audio filter mode. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="AudioFilterMode">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AudioFilterMode"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="AudioFilterMode">
    <Param name="eAudioFilterMode">AF_FILTER_AF_FILTER_OFF</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AudioFilterMode",
        "param": {
          "name": "eAudioFilterMode",
          "#text": "AF_FILTER_AF_FILTER_OFF"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="AudioFilterMode">
    <Param name="eAudioFilterMode">AF_FILTER_AF_FILTER_OFF</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AudioFilterMode",
        "param": {
          "name": "eAudioFilterMode",
          "#text": "AF_FILTER_AF_FILTER_OFF"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="AudioFilterMode">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AudioFilterMode"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.3.2 DemodulationSettings  <a id="DemodulationSettings"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eDEMODULATION | eDemodulation | Demodulation mode. |
| i/o | int64 | iBfoFrequency | BFO frequency [Hz]. |
| i/o | int64 | iAfFrequency | Demodulation frequency [Hz]. |
| i/o | eAF_BANDWIDTH | eAfBandwidth | Demodulation bandwidth. |
| i/o | int32 | iAfThreshold | Demodulation threshold [dBµV]. |
| i/o | boolean | bUseAfThreshold | Level threshold for demodulation active (true: active, false: not active, i.e. all
                        channels regardless of their level will be demodulated. |
| i/o | int64 | iPassbandFrequency | SSB passband frequency [Hz]. |
| i/o | eLEVEL_INDICATOR | eLevelIndicator | Rx measurements level detector characteristics. |
| i/o | boolean | bAfc | AFC (automatic frequency control) or MFC (manual f. c.) in use (true: AFC, false:
                        MFC). |
| i/o | eGAIN_CONTROL | eGainSelect | AGC (automatic gain control) or MGC (manual g. c.) in use. |
| i/o | int32 | iGainValue | MGC value [dBµV] (range -30 to 130). |
| i/o | eGAIN_TIMING | eGainTiming | AGC gain timing characteristics. |
| i/o | boolean | bStereoDecoder | Stereo decoder (true: enabled, demodulation bandwidth must be 120 kHz min., false:
                        disabled). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="DemodulationSettings">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "DemodulationSettings"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="DemodulationSettings">
    <Param name="eDemodulation">MOD_FM</Param>
    <Param name="iBfoFrequency">1</Param>
    <Param name="iAfFrequency">1</Param>
    <Param name="eAfBandwidth">BW_25</Param>
    <Param name="iAfThreshold">1</Param>
    <Param name="bUseAfThreshold">true</Param>
    <Param name="iPassbandFrequency">1</Param>
    <Param name="eLevelIndicator">LEVEL_INDICATOR_RMS</Param>
    <Param name="bAfc">true</Param>
    <Param name="eGainSelect">GAIN_AUTO</Param>
    <Param name="iGainValue">5</Param>
    <Param name="eGainTiming">GC_FAST</Param>
    <Param name="bStereoDecoder">false</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "DemodulationSettings",
        "param": [
          {
            "name": "eDemodulation",
            "#text": "MOD_FM"
          },
          {
            "name": "iBfoFrequency",
            "#text": "1"
          },
          {
            "name": "iAfFrequency",
            "#text": "1"
          },
          {
            "name": "eAfBandwidth",
            "#text": "BW_25"
          },
          {
            "name": "iAfThreshold",
            "#text": "1"
          },
          {
            "name": "bUseAfThreshold",
            "#text": "true"
          },
          {
            "name": "iPassbandFrequency",
            "#text": "1"
          },
          {
            "name": "eLevelIndicator",
            "#text": "LEVEL_INDICATOR_RMS"
          },
          {
            "name": "bAfc",
            "#text": "true"
          },
          {
            "name": "eGainSelect",
            "#text": "GAIN_AUTO"
          },
          {
            "name": "iGainValue",
            "#text": "5"
          },
          {
            "name": "eGainTiming",
            "#text": "GC_FAST"
          },
          {
            "name": "bStereoDecoder",
            "#text": "false"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="DemodulationSettings">
    <Param name="eDemodulation">MOD_FM</Param>
    <Param name="iBfoFrequency">1</Param>
    <Param name="iAfFrequency">1</Param>
    <Param name="eAfBandwidth">BW_25</Param>
    <Param name="iAfThreshold">1</Param>
    <Param name="bUseAfThreshold">true</Param>
    <Param name="iPassbandFrequency">1</Param>
    <Param name="eLevelIndicator">LEVEL_INDICATOR_RMS</Param>
    <Param name="bAfc">true</Param>
    <Param name="eGainSelect">GAIN_AUTO</Param>
    <Param name="iGainValue">5</Param>
    <Param name="eGainTiming">GC_FAST</Param>
    <Param name="bStereoDecoder">false</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "DemodulationSettings",
        "param": [
          {
            "name": "eDemodulation",
            "#text": "MOD_FM"
          },
          {
            "name": "iBfoFrequency",
            "#text": "1"
          },
          {
            "name": "iAfFrequency",
            "#text": "1"
          },
          {
            "name": "eAfBandwidth",
            "#text": "BW_25"
          },
          {
            "name": "iAfThreshold",
            "#text": "1"
          },
          {
            "name": "bUseAfThreshold",
            "#text": "true"
          },
          {
            "name": "iPassbandFrequency",
            "#text": "1"
          },
          {
            "name": "eLevelIndicator",
            "#text": "LEVEL_INDICATOR_RMS"
          },
          {
            "name": "bAfc",
            "#text": "true"
          },
          {
            "name": "eGainSelect",
            "#text": "GAIN_AUTO"
          },
          {
            "name": "iGainValue",
            "#text": "5"
          },
          {
            "name": "eGainTiming",
            "#text": "GC_FAST"
          },
          {
            "name": "bStereoDecoder",
            "#text": "false"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="DemodulationSettings">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "DemodulationSettings"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.3.3 ITU  <a id="ITU"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | boolean | bEnableMeasurement | ITU measurement active (true: enabled, false: disabled). |
| i/o | eMEASUREMODE | eBwMeasurementMode | Type of bandwidth ("x dB" or "beta %") for bandwidth measurement. |
| i/o | int32 | iConfigBwXdB | Parameter x [1/10 dB] (range 0 to 1000: 0 dB to 100 dB) for "x dB" measuring. |
| i/o | int32 | iConfigBwBeta | Parameter beta [1/10 %] (range 1 to 999: 0.1 % to 99.9 %) for "beta %" measuring. |
| i/o | boolean | bUseAutoBandwidthLimits | Auto bandwidth limits in use (true: auto limits, false: custom limits). |
| i/o | int32 | iLowerBandwidthLimit | Left (lower) custom bandwidth limit [Hz] (center frequency to lower limit, if custom
                        limits in use). |
| i/o | int32 | iUpperBandwidthLimit | Right (higher) custom bandwidth limit [Hz] (center frequency to upper limit, ditto). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="ITU">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ITU"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="ITU">
    <Param name="bEnableMeasurement">true</Param>
    <Param name="eBwMeasurementMode">MEASUREMODE_BETA</Param>
    <Param name="iConfigBwXdB">260</Param>
    <Param name="iConfigBwBeta">10</Param>
    <Param name="bUseAutoBandwidthLimits">true</Param>
    <Param name="iLowerBandwidthLimit">-40000000</Param>
    <Param name="iUpperBandwidthLimit">40000000</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ITU",
        "param": [
          {
            "name": "bEnableMeasurement",
            "#text": "true"
          },
          {
            "name": "eBwMeasurementMode",
            "#text": "MEASUREMODE_BETA"
          },
          {
            "name": "iConfigBwXdB",
            "#text": "260"
          },
          {
            "name": "iConfigBwBeta",
            "#text": "10"
          },
          {
            "name": "bUseAutoBandwidthLimits",
            "#text": "true"
          },
          {
            "name": "iLowerBandwidthLimit",
            "#text": "-40000000"
          },
          {
            "name": "iUpperBandwidthLimit",
            "#text": "40000000"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="ITU">
    <Param name="bEnableMeasurement">true</Param>
    <Param name="eBwMeasurementMode">MEASUREMODE_BETA</Param>
    <Param name="iConfigBwXdB">260</Param>
    <Param name="iConfigBwBeta">10</Param>
    <Param name="bUseAutoBandwidthLimits">true</Param>
    <Param name="iLowerBandwidthLimit">-40000000</Param>
    <Param name="iUpperBandwidthLimit">40000000</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ITU",
        "param": [
          {
            "name": "bEnableMeasurement",
            "#text": "true"
          },
          {
            "name": "eBwMeasurementMode",
            "#text": "MEASUREMODE_BETA"
          },
          {
            "name": "iConfigBwXdB",
            "#text": "260"
          },
          {
            "name": "iConfigBwBeta",
            "#text": "10"
          },
          {
            "name": "bUseAutoBandwidthLimits",
            "#text": "true"
          },
          {
            "name": "iLowerBandwidthLimit",
            "#text": "-40000000"
          },
          {
            "name": "iUpperBandwidthLimit",
            "#text": "40000000"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="ITU">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ITU"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.3.4 RDS  <a id="RDS"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | boolean | bDecoderEnabled | Enable RDS decoder (true: enabled, false: disabled). |
| out | string | zData | Flags, RDS codes PI, TP, TA, MS, DI (see table above for details). |
| out | string | zProgramString | RDS code PS: program string (name of program, 8 chars max.). |
| out | string | zRadioText | RDS code RT: radio text (64 chars max.). |
| out | string | zGroupCodeStats | RDS group code statistics (see table above for details). |
| in | boolean | bClearGroupCodeStats | Reset RDS group code statistics. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="RDS">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "RDS"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="RDS">
    <Param name="bDecoderEnabled">true</Param>
    <Param name="zData">0, 54035, 1, 0, 1, 1</Param>
    <Param name="zProgramString">BAYERN 3</Param>
    <Param name="zRadioText">BAYERN 3 am Mittwoch</Param>
    <Param name="zGroupCodeStats">188, 0, 93, 28, 1, 0, 28, 0, 86, 0, 0, 0, 29, 0, 96, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                     0, 0, 0, 0, 0, 0, 0</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "RDS",
        "param": [
          {
            "name": "bDecoderEnabled",
            "#text": "true"
          },
          {
            "name": "zData",
            "#text": "0, 54035, 1, 0, 1, 1"
          },
          {
            "name": "zProgramString",
            "#text": "BAYERN 3"
          },
          {
            "name": "zRadioText",
            "#text": "BAYERN 3 am Mittwoch"
          },
          {
            "name": "zGroupCodeStats",
            "#text": "188, 0, 93, 28, 1, 0, 28, 0, 86, 0, 0, 0, 29, 0, 96, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,\n                     0, 0, 0, 0, 0, 0, 0"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="RDS">
    <Param name="bDecoderEnabled">true</Param>
    <Param name="bClearGroupCodeStats">false</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "RDS",
        "param": [
          {
            "name": "bDecoderEnabled",
            "#text": "true"
          },
          {
            "name": "bClearGroupCodeStats",
            "#text": "false"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="RDS">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "RDS"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.3.5 SelCall  <a id="SelCall"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | boolean | bDecoderEnabled | State of SelCal decoder (true: enabled, false: disabled). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="SelCall">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SelCall"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="SelCall">
    <Param name="bDecoderEnabled">false</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SelCall",
        "param": {
          "name": "bDecoderEnabled",
          "#text": "false"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="SelCall">
    <Param name="bDecoderEnabled">false</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SelCall",
        "param": {
          "name": "bDecoderEnabled",
          "#text": "false"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="SelCall">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SelCall"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.3.6 Volume  <a id="Volume"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | int32 | iMainVolume | Main volume (range 0 [= mute] to 100 [= max. volume]). |
| i/o | int32 | iMainBalance | Main balance (range -50 [= left] to +50 [= right]). |
| i/o | int32 | iDemodVolume | Demodulation volume (range 0 [= mute] to 100 [= max. volume]). |
| i/o | int32 | iDemodBalance | Demodulation balance (range -50 [= left] to +50 [= right]). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="Volume">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "Volume"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="Volume">
    <Param name="iMainVolume">50</Param>
    <Param name="iMainBalance">0</Param>
    <Param name="iDemodVolume">100</Param>
    <Param name="iDemodBalance">0</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "Volume",
        "param": [
          {
            "name": "iMainVolume",
            "#text": "50"
          },
          {
            "name": "iMainBalance",
            "#text": "0"
          },
          {
            "name": "iDemodVolume",
            "#text": "100"
          },
          {
            "name": "iDemodBalance",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="Volume">
    <Param name="iMainVolume">50</Param>
    <Param name="iMainBalance">0</Param>
    <Param name="iDemodVolume">100</Param>
    <Param name="iDemodBalance">0</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Volume",
        "param": [
          {
            "name": "iMainVolume",
            "#text": "50"
          },
          {
            "name": "iMainBalance",
            "#text": "0"
          },
          {
            "name": "iDemodVolume",
            "#text": "100"
          },
          {
            "name": "iDemodBalance",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="Volume">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Volume"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

## 1.4 Diagnosis-Commands

### 1.4.1 AntennaBitsControl  <a id="AntennaBitsControl"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | int64 | iAntennaBits | Value to assign to the antenna bits manually (bits opened by the antenna bits mask
                        only, all others remain unchanged). |
| i/o | int64 | iAntennaBitsMask | Antenna bits mask to open antenna bits to be set or reset manually: set (1) bits to
                        open, reset (0) to prevent from modifying. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="AntennaBitsControl">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaBitsControl"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="AntennaBitsControl">
    <Param name="iAntennaBits">1</Param>
    <Param name="iAntennaBitsMask">1</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaBitsControl",
        "param": [
          {
            "name": "iAntennaBits",
            "#text": "1"
          },
          {
            "name": "iAntennaBitsMask",
            "#text": "1"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="AntennaBitsControl">
    <Param name="iAntennaBits">1</Param>
    <Param name="iAntennaBitsMask">1</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AntennaBitsControl",
        "param": [
          {
            "name": "iAntennaBits",
            "#text": "1"
          },
          {
            "name": "iAntennaBitsMask",
            "#text": "1"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="AntennaBitsControl">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AntennaBitsControl"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.4.2 AntennaLevelSwitch  <a id="AntennaLevelSwitch"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eANTLEVELSWITCH | eAntLevelSwitch | Normal DF/Rx operation or antenna radiator test (with output of antenna levels and
                        with or without antenna test radiator). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="AntennaLevelSwitch">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaLevelSwitch"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="AntennaLevelSwitch">
    <Param name="eAntLevelSwitch">ANTLEVEL_OFF</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaLevelSwitch",
        "param": {
          "name": "eAntLevelSwitch",
          "#text": "ANTLEVEL_OFF"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="AntennaLevelSwitch">
    <Param name="eAntLevelSwitch">ANTLEVEL_OFF</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AntennaLevelSwitch",
        "param": {
          "name": "eAntLevelSwitch",
          "#text": "ANTLEVEL_OFF"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="AntennaLevelSwitch">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AntennaLevelSwitch"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.4.3 CalibrationAntennaSwitch  <a id="CalibrationAntennaSwitch"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eCAL_SWITCH | eCalSwitchPos | Position of calibration switch: get input signal from antenna or from calibration
                        generator. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="CalibrationAntennaSwitch">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CalibrationAntennaSwitch"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="CalibrationAntennaSwitch">
    <Param name="eCalSwitchPos">ANTCAL_RECEIVE</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CalibrationAntennaSwitch",
        "param": {
          "name": "eCalSwitchPos",
          "#text": "ANTCAL_RECEIVE"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="CalibrationAntennaSwitch">
    <Param name="eCalSwitchPos">ANTCAL_RECEIVE</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CalibrationAntennaSwitch",
        "param": {
          "name": "eCalSwitchPos",
          "#text": "ANTCAL_RECEIVE"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="CalibrationAntennaSwitch">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CalibrationAntennaSwitch"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.4.4 CalibrationGenerator  <a id="CalibrationGenerator"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | int64 | iFrequency | Frequency [Hz] of calibration generator. |
| i/o | eCALMOD | eModulation | Modulation mode. |
| i/o | eCALOUT | eOutput | Output connector. |
| i/o | int32 | iAttenuation | Attenuation [dB]. |
| out | int64 | iLowerLimit | Lower frequency limit [Hz]. NOTE: If eOutput is set to NONE, full frequency range
                        is allowed. Frequency will be auto-adjusted when selecting an active output. |
| out | int64 | iUpperLimit | Upper frequency limit [Hz]. NOTE: (see iLowerLimit). |
| out | int32 | iOutputLevel | Output level [1/10 dBm]. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="CalibrationGenerator">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CalibrationGenerator"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="CalibrationGenerator">
    <Param name="iFrequency">4500000000</Param>
    <Param name="eModulation">CALMOD_F10K10L</Param>
    <Param name="eOutput">CALOUT_NONE</Param>
    <Param name="iAttenuation">0</Param>
    <Param name="iLowerLimit">9000</Param>
    <Param name="iUpperLimit">6100000000</Param>
    <Param name="iOutputLevel">0</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CalibrationGenerator",
        "param": [
          {
            "name": "iFrequency",
            "#text": "4500000000"
          },
          {
            "name": "eModulation",
            "#text": "CALMOD_F10K10L"
          },
          {
            "name": "eOutput",
            "#text": "CALOUT_NONE"
          },
          {
            "name": "iAttenuation",
            "#text": "0"
          },
          {
            "name": "iLowerLimit",
            "#text": "9000"
          },
          {
            "name": "iUpperLimit",
            "#text": "6100000000"
          },
          {
            "name": "iOutputLevel",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="CalibrationGenerator">
    <Param name="iFrequency">4500000000</Param>
    <Param name="eModulation">CALMOD_F10K10L</Param>
    <Param name="eOutput">CALOUT_NONE</Param>
    <Param name="iAttenuation">0</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CalibrationGenerator",
        "param": [
          {
            "name": "iFrequency",
            "#text": "4500000000"
          },
          {
            "name": "eModulation",
            "#text": "CALMOD_F10K10L"
          },
          {
            "name": "eOutput",
            "#text": "CALOUT_NONE"
          },
          {
            "name": "iAttenuation",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="CalibrationGenerator">
    <Param name="iLowerLimit">9000</Param>
    <Param name="iUpperLimit">6100000000</Param>
    <Param name="iOutputLevel">0</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CalibrationGenerator",
        "param": [
          {
            "name": "iLowerLimit",
            "#text": "9000"
          },
          {
            "name": "iUpperLimit",
            "#text": "6100000000"
          },
          {
            "name": "iOutputLevel",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.4.5 Memtest  <a id="Memtest"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | eMEMTEST_TYPE | eType | Specify type of memory test; SET: trigger type of memory test specified;         
                        GET: query results of type of memory test specified. |
| out | eMEMTEST_RESULT | eResult | Memory test result: successful, unsuccessful or aborted. |
| out | string | zDate | Date (12 chars max.) of memory test. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="Memtest">
    <Param name="eType">MEMTEST_OFF</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "Memtest",
        "param": {
          "name": "eType",
          "#text": "MEMTEST_OFF"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="Memtest">
    <Param name="eType">MEMTEST_OFF</Param>
    <Param name="eResult">MEMTEST_RESULT_PASS</Param>
    <Param name="zDate">2017-12-01</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "Memtest",
        "param": [
          {
            "name": "eType",
            "#text": "MEMTEST_OFF"
          },
          {
            "name": "eResult",
            "#text": "MEMTEST_RESULT_PASS"
          },
          {
            "name": "zDate",
            "#text": "2017-12-01"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="Memtest">
    <Param name="eType">MEMTEST_OFF</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Memtest",
        "param": {
          "name": "eType",
          "#text": "MEMTEST_OFF"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="Memtest">
    <Param name="eType">MEMTEST_OFF</Param>
    <Param name="eResult">MEMTEST_RESULT_PASS</Param>
    <Param name="zDate">2017-12-01</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Memtest",
        "param": [
          {
            "name": "eType",
            "#text": "MEMTEST_OFF"
          },
          {
            "name": "eResult",
            "#text": "MEMTEST_RESULT_PASS"
          },
          {
            "name": "zDate",
            "#text": "2017-12-01"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.4.6 NmeaShow  <a id="NmeaShow"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | boolean | bShow | Enable debug output of received NMEA sentences. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="NmeaShow">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "NmeaShow"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="NmeaShow">
    <Param name="bShow">false</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "NmeaShow",
        "param": {
          "name": "bShow",
          "#text": "false"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="NmeaShow">
    <Param name="bShow">false</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "NmeaShow",
        "param": {
          "name": "bShow",
          "#text": "false"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="NmeaShow">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "NmeaShow"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.4.7 Selftest  <a id="Selftest"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | eSELFTEST | eSelfTestType | Type of self test. |
| out | eRESULT | eResult | Self test result: successful: all tests passed, unsuccessful: at least one test point
                        failed. |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="Selftest">
    <Param name="eSelfTestType">SELFTEST_SHORT</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Selftest",
        "param": {
          "name": "eSelfTestType",
          "#text": "SELFTEST_SHORT"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="Selftest">
    <Param name="eSelfTestType">SELFTEST_SHORT</Param>
    <Param name="eResult">RESULT_GO</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Selftest",
        "param": [
          {
            "name": "eSelfTestType",
            "#text": "SELFTEST_SHORT"
          },
          {
            "name": "eResult",
            "#text": "RESULT_GO"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.4.8 SigPData  <a id="SigPData"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | eSIGP_DATATYPE | eDataType | Type of signal processing data to output. |
| i/o | eSTATE | eOutput | Data output on or off. |
| i/o | int64 | iFrequency | Frequency of SigPData (0: Auto, i.e. Rx frequency/center frequency of current measurement). |
| i/o | int32 | iRangeId | ID of scan range currently used. |
| out | int32 | iLogChannel | Number of logical channel currently used. |
| out | int32 | iPhysChannel | Number of physical channel currently used. |
| out | int32 | iHop | Number of hop currently used. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="SigPData">
    <Param name="eDataType">SIGP_DATA_AVG</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SigPData",
        "param": {
          "name": "eDataType",
          "#text": "SIGP_DATA_AVG"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="SigPData">
    <Param name="eDataType">SIGP_DATA_AVG</Param>
    <Param name="eOutput">STATE_OFF</Param>
    <Param name="iFrequency">0</Param>
    <Param name="iRangeId">1</Param>
    <Param name="iLogChannel">2</Param>
    <Param name="iPhysChannel">3</Param>
    <Param name="iHop">4</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SigPData",
        "param": [
          {
            "name": "eDataType",
            "#text": "SIGP_DATA_AVG"
          },
          {
            "name": "eOutput",
            "#text": "STATE_OFF"
          },
          {
            "name": "iFrequency",
            "#text": "0"
          },
          {
            "name": "iRangeId",
            "#text": "1"
          },
          {
            "name": "iLogChannel",
            "#text": "2"
          },
          {
            "name": "iPhysChannel",
            "#text": "3"
          },
          {
            "name": "iHop",
            "#text": "4"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="SigPData">
    <Param name="eDataType">SIGP_DATA_AVG</Param>
    <Param name="eOutput">STATE_OFF</Param>
    <Param name="iFrequency">0</Param>
    <Param name="iRangeId">1</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SigPData",
        "param": [
          {
            "name": "eDataType",
            "#text": "SIGP_DATA_AVG"
          },
          {
            "name": "eOutput",
            "#text": "STATE_OFF"
          },
          {
            "name": "iFrequency",
            "#text": "0"
          },
          {
            "name": "iRangeId",
            "#text": "1"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="SigPData">
    <Param name="eDataType">SIGP_DATA_AVG</Param>
    <Param name="iLogChannel">2</Param>
    <Param name="iPhysChannel">3</Param>
    <Param name="iHop">4</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SigPData",
        "param": [
          {
            "name": "eDataType",
            "#text": "SIGP_DATA_AVG"
          },
          {
            "name": "iLogChannel",
            "#text": "2"
          },
          {
            "name": "iPhysChannel",
            "#text": "3"
          },
          {
            "name": "iHop",
            "#text": "4"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.4.9 StatusReportingRegister  <a id="StatusReportingRegister"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | boolean | bSettling | Status Reporting Register bit set or reset: Operation Status Register bit 1: SETTLING. |
| out | boolean | bBlanking | ditto, bit 11: RX_Blanking. |
| out | boolean | bMute | ditto, bit 12: RX_Mute. |
| out | boolean | bRefSettling | Operation Fanout Status Register bit 0: REFSettling. |
| out | boolean | bWarmingUp | ditto, bit 1: WARMingup. |
| out | boolean | bStateVoltage | Questionable Status Register bit 0: VOLTage. |
| out | boolean | bStateTemp | ditto, bit 4: TEMPerature. |
| out | boolean | bStateFreq | ditto, bit 5: FREQency. |
| out | boolean | bDccOvermodulation | ditto, bit 9: level (OVERload). |
| out | boolean | bStateICom | ditto, bit 10: INTComm. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="StatusReportingRegister">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "StatusReportingRegister"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="StatusReportingRegister">
    <Param name="bSettling">false</Param>
    <Param name="bBlanking">false</Param>
    <Param name="bMute">false</Param>
    <Param name="bRefSettling">false</Param>
    <Param name="bWarmingUp">false</Param>
    <Param name="bStateVoltage">false</Param>
    <Param name="bStateTemp">false</Param>
    <Param name="bStateFreq">false</Param>
    <Param name="bDccOvermodulation">false</Param>
    <Param name="bStateICom">false</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "StatusReportingRegister",
        "param": [
          {
            "name": "bSettling",
            "#text": "false"
          },
          {
            "name": "bBlanking",
            "#text": "false"
          },
          {
            "name": "bMute",
            "#text": "false"
          },
          {
            "name": "bRefSettling",
            "#text": "false"
          },
          {
            "name": "bWarmingUp",
            "#text": "false"
          },
          {
            "name": "bStateVoltage",
            "#text": "false"
          },
          {
            "name": "bStateTemp",
            "#text": "false"
          },
          {
            "name": "bStateFreq",
            "#text": "false"
          },
          {
            "name": "bDccOvermodulation",
            "#text": "false"
          },
          {
            "name": "bStateICom",
            "#text": "false"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.4.10 Temperature  <a id="Temperature"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| auto | string | zModuleName | Name of hardware module to address (24 chars max.) or "ALL" (also default value).
                        (default value: 
                        ALL) |
| out | sTEMP_INFO | asTempTestPoint[256] | Array with all temperature test points (64 max.) on hardware module(s). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="Temperature">
    <Param name="zModuleName">ALL</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "Temperature",
        "param": {
          "name": "zModuleName",
          "#text": "ALL"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="Temperature">
    <Param name="zModuleName">ALL</Param>
    <Array name="asTempTestPoint">
      <Struct name="sTempTestPoint">
        <Param name="zModule">P1</Param>
        <Param name="zSensor">TEMP_FPGA</Param>
        <Param name="iValue">57</Param>
      </Struct>
      <!-- ... -->
    </Array>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "Temperature",
        "param": {
          "name": "zModuleName",
          "#text": "ALL"
        },
        "array": {
          "name": "asTempTestPoint",
          "struct": {
            "name": "sTempTestPoint",
            "param": [
              {
                "name": "zModule",
                "#text": "P1"
              },
              {
                "name": "zSensor",
                "#text": "TEMP_FPGA"
              },
              {
                "name": "iValue",
                "#text": "57"
              }
            ]
          }
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.4.11 TestPoints  <a id="TestPoints"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| auto | string | zModuleName | Name of hardware module to address (24 chars max.) or "ALL".
                        (default value: 
                        ALL) |
| out | sTEST_POINT | asTestPoint[768] | Array with test points (512 max.) on hardware module(s). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="TestPoints">
    <Param name="zModuleName">ALL</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "TestPoints",
        "param": {
          "name": "zModuleName",
          "#text": "ALL"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="TestPoints">
    <Param name="zModuleName">ALL</Param>
    <Array name="asTestPoint">
      <Struct name="sTestPoint">
        <Param name="zName">TTEMP</Param>
        <Param name="zModule">PRESEL VU</Param>
        <Param name="iValue">1503</Param>
        <Param name="iUpperLimit">800</Param>
        <Param name="iLowerLimit">2200</Param>
        <Param name="eOutOfRange">LIMIT_IN</Param>
        <Param name="bValid">true</Param>
      </Struct>
      <!-- ... -->
    </Array>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "TestPoints",
        "param": {
          "name": "zModuleName",
          "#text": "ALL"
        },
        "array": {
          "name": "asTestPoint",
          "struct": {
            "name": "sTestPoint",
            "param": [
              {
                "name": "zName",
                "#text": "TTEMP"
              },
              {
                "name": "zModule",
                "#text": "PRESEL VU"
              },
              {
                "name": "iValue",
                "#text": "1503"
              },
              {
                "name": "iUpperLimit",
                "#text": "800"
              },
              {
                "name": "iLowerLimit",
                "#text": "2200"
              },
              {
                "name": "eOutOfRange",
                "#text": "LIMIT_IN"
              },
              {
                "name": "bValid",
                "#text": "true"
              }
            ]
          }
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.4.12 XGLoopback  <a id="XGLoopback"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eXGLOOPBACKTEST | eXGLoopbackTestEnable | Start one of the loopback tests on XG (10 Gbit/s) loopback interface. |
| i/o | boolean | bXGLoopbackTrigError | Trigger error on lane 1 (true: yes, false: no). |
| out | int64 | iLoopbackRxCount | Number of frames received by XG loopback interface. |
| out | int64 | iLoopbackErrorCount | Number of errors occurred during XG loopback test. |
| out | int32 | iLoopbackStatus | State of XG loopback interface. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="XGLoopback">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "XGLoopback"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="XGLoopback">
    <Param name="eXGLoopbackTestEnable">XGLOOPBACK_OFF</Param>
    <Param name="bXGLoopbackTrigError">false</Param>
    <Param name="iLoopbackRxCount">0</Param>
    <Param name="iLoopbackErrorCount">0</Param>
    <Param name="iLoopbackStatus">0</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "XGLoopback",
        "param": [
          {
            "name": "eXGLoopbackTestEnable",
            "#text": "XGLOOPBACK_OFF"
          },
          {
            "name": "bXGLoopbackTrigError",
            "#text": "false"
          },
          {
            "name": "iLoopbackRxCount",
            "#text": "0"
          },
          {
            "name": "iLoopbackErrorCount",
            "#text": "0"
          },
          {
            "name": "iLoopbackStatus",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="XGLoopback">
    <Param name="eXGLoopbackTestEnable">XGLOOPBACK_OFF</Param>
    <Param name="bXGLoopbackTrigError">false</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "XGLoopback",
        "param": [
          {
            "name": "eXGLoopbackTestEnable",
            "#text": "XGLOOPBACK_OFF"
          },
          {
            "name": "bXGLoopbackTrigError",
            "#text": "false"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="XGLoopback">
    <Param name="iLoopbackRxCount">0</Param>
    <Param name="iLoopbackErrorCount">0</Param>
    <Param name="iLoopbackStatus">0</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "XGLoopback",
        "param": [
          {
            "name": "iLoopbackRxCount",
            "#text": "0"
          },
          {
            "name": "iLoopbackErrorCount",
            "#text": "0"
          },
          {
            "name": "iLoopbackStatus",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

## 1.5 Information-Commands

### 1.5.1 ClientInfo  <a id="ClientInfo"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | string | zClientAddress | IPv4 client IP address (512 chars max.). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="ClientInfo">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ClientInfo"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="ClientInfo">
    <Param name="zClientAddress">10.11.12.13</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ClientInfo",
        "param": {
          "name": "zClientAddress",
          "#text": "10.11.12.13"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.5.2 DeviceInfo  <a id="DeviceInfo"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | eDEVICE_INFO | eDeviceInfo | Type of device info. |
| out | string | aDeviceInfoList[50] | Array with detailed information (50 elements max.). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="DeviceInfo">
    <Param name="eDeviceInfo">DEV_INFO_FREQUENCY_MIN_MAX</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "DeviceInfo",
        "param": {
          "name": "eDeviceInfo",
          "#text": "DEV_INFO_FREQUENCY_MIN_MAX"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="DeviceInfo">
    <Param name="eDeviceInfo">DEV_INFO_FREQUENCY_MIN_MAX</Param>
    <Array name="aDeviceInfoList">
      <Param name="zResult">8000</Param>
      <!-- ... -->
    </Array>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "DeviceInfo",
        "param": {
          "name": "eDeviceInfo",
          "#text": "DEV_INFO_FREQUENCY_MIN_MAX"
        },
        "array": {
          "name": "aDeviceInfoList",
          "param": {
            "name": "zResult",
            "#text": "8000"
          }
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.5.3 FrequencySwitchPoints  <a id="FrequencySwitchPoints"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | sFREQ_TIME_PAIR | aiFrequencySwitchPointArray[50] | Array with all frequency switch points (50 max.). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="FrequencySwitchPoints">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "FrequencySwitchPoints"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="FrequencySwitchPoints">
    <Array name="aiFrequencySwitchPointArray">
      <Struct name="sFreqTimePair">
        <Param name="iFrequency">390000000</Param>
        <Param name="iTime">20000</Param>
      </Struct>
      <!-- ... -->
    </Array>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "FrequencySwitchPoints",
        "array": {
          "name": "aiFrequencySwitchPointArray",
          "struct": {
            "name": "sFreqTimePair",
            "param": [
              {
                "name": "iFrequency",
                "#text": "390000000"
              },
              {
                "name": "iTime",
                "#text": "20000"
              }
            ]
          }
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.5.4 HwInfo  <a id="HwInfo"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | sHW_INFO | asHwInfo[64] | Array with detailed information on each hardware module (64 max.). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="HwInfo">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "HwInfo"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="HwInfo">
    <Array name="asHwInfo">
      <Struct name="sHwInfo">
        <Param name="eHwType">HW_DF_ANTENNA</Param>
        <Param name="eHwStatus">HW_STATUS_OK</Param>
        <Param name="iCode">19</Param>
        <Param name="iHandle">0</Param>
        <Param name="iPort">6</Param>
        <Struct name="sVersion">
          <Param name="iMainVersion">3</Param>
          <Param name="iSubVersion">0</Param>
        </Struct>
        <Param name="zName">ADD153SR</Param>
      </Struct>
      <!-- ... -->
    </Array>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "HwInfo",
        "array": {
          "name": "asHwInfo",
          "struct": {
            "name": "sHwInfo",
            "param": [
              {
                "name": "eHwType",
                "#text": "HW_DF_ANTENNA"
              },
              {
                "name": "eHwStatus",
                "#text": "HW_STATUS_OK"
              },
              {
                "name": "iCode",
                "#text": "19"
              },
              {
                "name": "iHandle",
                "#text": "0"
              },
              {
                "name": "iPort",
                "#text": "6"
              },
              {
                "name": "zName",
                "#text": "ADD153SR"
              }
            ],
            "struct": {
              "name": "sVersion",
              "param": [
                {
                  "name": "iMainVersion",
                  "#text": "3"
                },
                {
                  "name": "iSubVersion",
                  "#text": "0"
                }
              ]
            }
          }
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.5.5 HwRefresh  <a id="HwRefresh"></a>

_No members (command takes no parameters)._

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="HwRefresh">
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "HwRefresh"
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="HwRefresh">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "HwRefresh"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.5.6 Identification  <a id="Identification"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | string | zIdn | Device identification (128 chars max.): manufacturer, device name, serial number,
                        firmware version, firmware ID number. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="Identification">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "Identification"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="Identification">
    <Param name="zIdn">ROHDE&SCHWARZ,DDF550,100.003/002,V01.00-4074.2183.00</Param>
  </Command>
</Reply>
```

> **ICD example is not well-formed XML** (not well-formed (invalid token): line 3, column 36). This is a defect in the ICD's own example text (a literal, unescaped `&` -- e.g. `ROHDE&SCHWARZ` instead of `ROHDE&amp;SCHWARZ`), not a bug in the extraction. pugixml's default (strict) parsing -- the mode `ddf550_parser.cpp` actually uses -- would reject this exact byte sequence too and fall back to the malformed-XML envelope (`msg_kind: "malformed"`) instead of a parsed reply. If real hardware sends unescaped `&` in string fields (e.g. device IDN strings), that is a real parser gap worth flagging upstream.

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.5.7 InterfaceVersion  <a id="InterfaceVersion"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | string | zInterfaceVersion | Version number (24 chars max.) of Remote Command Interface used on the R&S DDFx. |
| out | string | zInterfaceDate | Date of release (24 chars max.) of current version. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="InterfaceVersion">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "InterfaceVersion"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="InterfaceVersion">
    <Param name="zInterfaceVersion">1.22</Param>
    <Param name="zInterfaceDate">2010-11-24</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "InterfaceVersion",
        "param": [
          {
            "name": "zInterfaceVersion",
            "#text": "1.22"
          },
          {
            "name": "zInterfaceDate",
            "#text": "2010-11-24"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.5.8 ModuleInfo  <a id="ModuleInfo"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | sMODULE_INFO | asModuleInfo[60] | Array with information on all installed hardware modules (50 max.). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="ModuleInfo">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ModuleInfo"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="ModuleInfo">
    <Array name="asModuleInfo">
      <Struct name="sModuleInfo">
        <Param name="zPartNumber">4066.1900.02</Param>
        <Param name="iHwCode">1</Param>
        <Param name="zProductIndex">3.05</Param>
        <Param name="zSerialNumber">100086/100086</Param>
        <Param name="zProductDate">2008-03-26</Param>
        <Param name="zName">PRESEL VU</Param>
      </Struct>
      <!-- ... -->
    </Array>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ModuleInfo",
        "array": {
          "name": "asModuleInfo",
          "struct": {
            "name": "sModuleInfo",
            "param": [
              {
                "name": "zPartNumber",
                "#text": "4066.1900.02"
              },
              {
                "name": "iHwCode",
                "#text": "1"
              },
              {
                "name": "zProductIndex",
                "#text": "3.05"
              },
              {
                "name": "zSerialNumber",
                "#text": "100086/100086"
              },
              {
                "name": "zProductDate",
                "#text": "2008-03-26"
              },
              {
                "name": "zName",
                "#text": "PRESEL VU"
              }
            ]
          }
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.5.9 ModuleNames  <a id="ModuleNames"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | string | azModuleName[60] | Array with names of all installed hardware modules (50 max.). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="ModuleNames">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ModuleNames"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="ModuleNames">
    <Array name="azModuleName">
      <Param name="zModuleName">PRESEL VU</Param>
      <!-- ... -->
    </Array>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ModuleNames",
        "array": {
          "name": "azModuleName",
          "param": {
            "name": "zModuleName",
            "#text": "PRESEL VU"
          }
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.5.10 Options  <a id="Options"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | string | azOption[64] | Array with all installed options (64 max.). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="Options">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "Options"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="Options">
    <Array name="azOption">
      <Param name="zOption">COR</Param>
      <!-- ... -->
    </Array>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "Options",
        "array": {
          "name": "azOption",
          "param": {
            "name": "zOption",
            "#text": "COR"
          }
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.5.11 ReferenceFrequency  <a id="ReferenceFrequency"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | int64 | iFrequency | Reference frequency [Hz]. |
| out | eREFERENCE_SYNCH | eReferenceSynch | Status of synchronization of reference. |
| out | boolean | bWarmingUp | Reference frequency synthesizer warming up (true: warming up, false: warmed up). |
| out | boolean | bRefSettling | Synthesizer settling (true: settling, false: settled). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="ReferenceFrequency">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ReferenceFrequency"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="ReferenceFrequency">
    <Param name="iFrequency">10000000</Param>
    <Param name="eReferenceSynch">REFERENCE_SYNCH_INTERNAL</Param>
    <Param name="bWarmingUp">false</Param>
    <Param name="bRefSettling">false</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ReferenceFrequency",
        "param": [
          {
            "name": "iFrequency",
            "#text": "10000000"
          },
          {
            "name": "eReferenceSynch",
            "#text": "REFERENCE_SYNCH_INTERNAL"
          },
          {
            "name": "bWarmingUp",
            "#text": "false"
          },
          {
            "name": "bRefSettling",
            "#text": "false"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.5.12 SoftwareVersion  <a id="SoftwareVersion"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | sSW_VERSION | asSwVersion[80] | Array with software version numbers of all installed hardware modules (80 max.). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="SoftwareVersion">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SoftwareVersion"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="SoftwareVersion">
    <Array name="asSwVersion">
      <Struct name="sSwVersion">
        <Param name="zModuleName">ADC_BOARD1_FPGA</Param>
        <Param name="zSwVersion">V01.00 2010-05-18</Param>
      </Struct>
      <!-- ... -->
    </Array>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SoftwareVersion",
        "array": {
          "name": "asSwVersion",
          "struct": {
            "name": "sSwVersion",
            "param": [
              {
                "name": "zModuleName",
                "#text": "ADC_BOARD1_FPGA"
              },
              {
                "name": "zSwVersion",
                "#text": "V01.00 2010-05-18"
              }
            ]
          }
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

## 1.6 Peripherals-Commands

### 1.6.1 AntennaControl  <a id="AntennaControl"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eANT_CTRL_MODE | eAntennaControlMode | Antenna control mode (Auto or Manual). |
| i/o | eHF_INPUT | eHfInput | HF antenna signal input. |
| i/o | int64 | iHfLimit | HF border frequency [Hz] (upper frequency limit for HF option). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="AntennaControl">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaControl"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="AntennaControl">
    <Param name="eAntennaControlMode">ANT_CTRL_MODE_AUTO</Param>
    <Param name="eHfInput">HF_INPUT_HF1</Param>
    <Param name="iHfLimit">20000000</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaControl",
        "param": [
          {
            "name": "eAntennaControlMode",
            "#text": "ANT_CTRL_MODE_AUTO"
          },
          {
            "name": "eHfInput",
            "#text": "HF_INPUT_HF1"
          },
          {
            "name": "iHfLimit",
            "#text": "20000000"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="AntennaControl">
    <Param name="eAntennaControlMode">ANT_CTRL_MODE_AUTO</Param>
    <Param name="eHfInput">HF_INPUT_HF1</Param>
    <Param name="iHfLimit">20000000</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AntennaControl",
        "param": [
          {
            "name": "eAntennaControlMode",
            "#text": "ANT_CTRL_MODE_AUTO"
          },
          {
            "name": "eHfInput",
            "#text": "HF_INPUT_HF1"
          },
          {
            "name": "iHfLimit",
            "#text": "20000000"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="AntennaControl">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AntennaControl"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.2 AntennaProperties  <a id="AntennaProperties"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | string | zAntennaName | Name (24 chars max.) of antenna to address. |
| out | int32 | iAntCode | Antenna code. |
| out | int64 | iFreqBegin | Lower border [Hz] (included) of entire antenna frequency range. |
| out | int64 | iFreqEnd | Upper border [Hz] (included) of entire antenna frequency range. |
| out | boolean | bGpsAvailable | Built-in GPS receiver available (true: available, false: not available). |
| out | boolean | bTestElementAvailable | Built-in antenna test radiator available (ditto). |
| out | sANT_RANGE_PROP | asAntRangeProp[48] | Array of antenna properties for different frequency ranges of antenna (48 max.). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="AntennaProperties">
    <Param name="zAntennaName">ADD153SR</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaProperties",
        "param": {
          "name": "zAntennaName",
          "#text": "ADD153SR"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="AntennaProperties">
    <Param name="zAntennaName">ADD153SR</Param>
    <Param name="iAntCode">19</Param>
    <Param name="iFreqBegin">300000</Param>
    <Param name="iFreqEnd">30000000</Param>
    <Param name="bGpsAvailable">false</Param>
    <Param name="bTestElementAvailable">false</Param>
    <Array name="asAntRangeProp">
      <Struct name="sAntRangeProp">
        <Param name="bAntPreAmp">false</Param>
        <Param name="bAntElevation">false</Param>
        <Param name="iFreqRangeBegin">300000</Param>
        <Param name="iFreqRangeEnd">30000000</Param>
        <Param name="eInputRange">INPUT_VUHF</Param>
        <Param name="eDfAlt">DFALT_AUTO</Param>
        <Param name="eAntPol">POL_VERTICAL</Param>
      </Struct>
      <!-- ... -->
    </Array>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaProperties",
        "param": [
          {
            "name": "zAntennaName",
            "#text": "ADD153SR"
          },
          {
            "name": "iAntCode",
            "#text": "19"
          },
          {
            "name": "iFreqBegin",
            "#text": "300000"
          },
          {
            "name": "iFreqEnd",
            "#text": "30000000"
          },
          {
            "name": "bGpsAvailable",
            "#text": "false"
          },
          {
            "name": "bTestElementAvailable",
            "#text": "false"
          }
        ],
        "array": {
          "name": "asAntRangeProp",
          "struct": {
            "name": "sAntRangeProp",
            "param": [
              {
                "name": "bAntPreAmp",
                "#text": "false"
              },
              {
                "name": "bAntElevation",
                "#text": "false"
              },
              {
                "name": "iFreqRangeBegin",
                "#text": "300000"
              },
              {
                "name": "iFreqRangeEnd",
                "#text": "30000000"
              },
              {
                "name": "eInputRange",
                "#text": "INPUT_VUHF"
              },
              {
                "name": "eDfAlt",
                "#text": "DFALT_AUTO"
              },
              {
                "name": "eAntPol",
                "#text": "POL_VERTICAL"
              }
            ]
          }
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.3 AntennaRxCatalog  <a id="AntennaRxCatalog"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | sKFACTOR | asKFactor[256] | Array with all available antenna factor data records for Rx antennas (256 max.). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="AntennaRxCatalog">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaRxCatalog"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="AntennaRxCatalog">
    <Array name="asKFactor">
      <Struct name="sKFactor">
        <Param name="iAntNo">10</Param>
        <Param name="sName">HK033</Param>
        <Param name="iFreqRangeBegin">80000000</Param>
        <Param name="iFreqRangeEnd">2000000000</Param>
        <Param name="iAntParams">23</Param>
      </Struct>
      <!-- ... -->
    </Array>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaRxCatalog",
        "array": {
          "name": "asKFactor",
          "struct": {
            "name": "sKFactor",
            "param": [
              {
                "name": "iAntNo",
                "#text": "10"
              },
              {
                "name": "sName",
                "#text": "HK033"
              },
              {
                "name": "iFreqRangeBegin",
                "#text": "80000000"
              },
              {
                "name": "iFreqRangeEnd",
                "#text": "2000000000"
              },
              {
                "name": "iAntParams",
                "#text": "23"
              }
            ]
          }
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.4 AntennaRxDefine  <a id="AntennaRxDefine"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | string | zAntennaName | Name (24 chars max.) of Rx antenna to address. |
| i/o | string | zAntennaModel | Name (24 chars max.) of antenna model (from AntennaRxCatalog). |
| i/o | int64 | iFreqBegin | Lower border frequency [Hz] (included). |
| i/o | int64 | iFreqEnd | Upper border frequency [Hz] (included). |
| i/o | eANT_POL | eAntPol | Antenna polarization. |
| i/o | int64 | iCtrlPort | Output of ctrl port. |
| i/o | int64 | iHfLimit | Upper limit of HF Converter [Hz]. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="AntennaRxDefine">
    <Param name="zAntennaName">HK033_V1</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaRxDefine",
        "param": {
          "name": "zAntennaName",
          "#text": "HK033_V1"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="AntennaRxDefine">
    <Param name="zAntennaName">HK033_V1</Param>
    <Param name="zAntennaModel">HK033</Param>
    <Param name="iFreqBegin">80000000</Param>
    <Param name="iFreqEnd">2000000000</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="iCtrlPort">16</Param>
    <Param name="iHfLimit">20000000</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaRxDefine",
        "param": [
          {
            "name": "zAntennaName",
            "#text": "HK033_V1"
          },
          {
            "name": "zAntennaModel",
            "#text": "HK033"
          },
          {
            "name": "iFreqBegin",
            "#text": "80000000"
          },
          {
            "name": "iFreqEnd",
            "#text": "2000000000"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "iCtrlPort",
            "#text": "16"
          },
          {
            "name": "iHfLimit",
            "#text": "20000000"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="AntennaRxDefine">
    <Param name="zAntennaName">HK033_V1</Param>
    <Param name="zAntennaModel">HK033</Param>
    <Param name="iFreqBegin">80000000</Param>
    <Param name="iFreqEnd">2000000000</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="iCtrlPort">16</Param>
    <Param name="iHfLimit">20000000</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AntennaRxDefine",
        "param": [
          {
            "name": "zAntennaName",
            "#text": "HK033_V1"
          },
          {
            "name": "zAntennaModel",
            "#text": "HK033"
          },
          {
            "name": "iFreqBegin",
            "#text": "80000000"
          },
          {
            "name": "iFreqEnd",
            "#text": "2000000000"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "iCtrlPort",
            "#text": "16"
          },
          {
            "name": "iHfLimit",
            "#text": "20000000"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="AntennaRxDefine">
    <Param name="zAntennaName">HK033_V1</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AntennaRxDefine",
        "param": {
          "name": "zAntennaName",
          "#text": "HK033_V1"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.5 AntennaRxDelete  <a id="AntennaRxDelete"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | string | zAntennaName | Name (24 chars max.) of Rx antenna to delete. |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="AntennaRxDelete">
    <Param name="zAntennaName">ADD153SR</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AntennaRxDelete",
        "param": {
          "name": "zAntennaName",
          "#text": "ADD153SR"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="AntennaRxDelete">
    <Param name="zAntennaName">ADD153SR</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AntennaRxDelete",
        "param": {
          "name": "zAntennaName",
          "#text": "ADD153SR"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.6 AntennaSetup  <a id="AntennaSetup"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | string | zAntennaName | Name (24 chars max.) of antenna to address. |
| i/o | int64 | iFreqBegin | Lower border [Hz] of antenna frequency range. |
| i/o | int64 | iFreqEnd | Upper border; if not exceeding lower border, antenna will not be used at all. |
| i/o | string | zCompassName | Name (24 chars max.) of connected compass (empty string if no compass connected). |
| i/o | int32 | iNorthCorrection | Global north correction offset [1/10 °] for antenna. |
| i/o | int32 | iRollCorrection | Global roll correction offset [1/10 °] for antenna. |
| i/o | int32 | iPitchCorrection | Global pitch correction offset [1/10 °] for antenna. |
| i/o | eHF_INPUT | eHfInput | HF antenna signal input for antenna. |
| i/o | int32 | iCtrlPort | Antenna control signal to output via AUX (X17), Pin 28 to 35 (if AUX control mode
                        is ANTENNA). |
| i/o | boolean | bGpsRead | Use GPS receiver of antenna (true: use, false: ignore). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="AntennaSetup">
    <Param name="zAntennaName">ADD153SR</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaSetup",
        "param": {
          "name": "zAntennaName",
          "#text": "ADD153SR"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="AntennaSetup">
    <Param name="zAntennaName">ADD153SR</Param>
    <Param name="iFreqBegin">90000000</Param>
    <Param name="iFreqEnd">100000000</Param>
    <Param name="zCompassName">GH150@ADD153SR</Param>
    <Param name="iNorthCorrection">3590</Param>
    <Param name="iRollCorrection">3590</Param>
    <Param name="iPitchCorrection">3590</Param>
    <Param name="eHfInput">HF_INPUT_HF1</Param>
    <Param name="iCtrlPort">16</Param>
    <Param name="bGpsRead">true</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaSetup",
        "param": [
          {
            "name": "zAntennaName",
            "#text": "ADD153SR"
          },
          {
            "name": "iFreqBegin",
            "#text": "90000000"
          },
          {
            "name": "iFreqEnd",
            "#text": "100000000"
          },
          {
            "name": "zCompassName",
            "#text": "GH150@ADD153SR"
          },
          {
            "name": "iNorthCorrection",
            "#text": "3590"
          },
          {
            "name": "iRollCorrection",
            "#text": "3590"
          },
          {
            "name": "iPitchCorrection",
            "#text": "3590"
          },
          {
            "name": "eHfInput",
            "#text": "HF_INPUT_HF1"
          },
          {
            "name": "iCtrlPort",
            "#text": "16"
          },
          {
            "name": "bGpsRead",
            "#text": "true"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="AntennaSetup">
    <Param name="zAntennaName">ADD153SR</Param>
    <Param name="iFreqBegin">90000000</Param>
    <Param name="iFreqEnd">100000000</Param>
    <Param name="zCompassName">GH150@ADD153SR</Param>
    <Param name="iNorthCorrection">3590</Param>
    <Param name="iRollCorrection">3590</Param>
    <Param name="iPitchCorrection">3590</Param>
    <Param name="eHfInput">HF_INPUT_HF1</Param>
    <Param name="iCtrlPort">16</Param>
    <Param name="bGpsRead">true</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AntennaSetup",
        "param": [
          {
            "name": "zAntennaName",
            "#text": "ADD153SR"
          },
          {
            "name": "iFreqBegin",
            "#text": "90000000"
          },
          {
            "name": "iFreqEnd",
            "#text": "100000000"
          },
          {
            "name": "zCompassName",
            "#text": "GH150@ADD153SR"
          },
          {
            "name": "iNorthCorrection",
            "#text": "3590"
          },
          {
            "name": "iRollCorrection",
            "#text": "3590"
          },
          {
            "name": "iPitchCorrection",
            "#text": "3590"
          },
          {
            "name": "eHfInput",
            "#text": "HF_INPUT_HF1"
          },
          {
            "name": "iCtrlPort",
            "#text": "16"
          },
          {
            "name": "bGpsRead",
            "#text": "true"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="AntennaSetup">
    <Param name="zAntennaName">ADD153SR</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AntennaSetup",
        "param": {
          "name": "zAntennaName",
          "#text": "ADD153SR"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.7 AntennaUsed  <a id="AntennaUsed"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | string | zAntennaName | Antenna name (24 chars max.). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="AntennaUsed">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaUsed"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="AntennaUsed">
    <Param name="zAntennaName">ADD153SR</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AntennaUsed",
        "param": {
          "name": "zAntennaName",
          "#text": "ADD153SR"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.8 CompassCmd  <a id="CompassCmd"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | string | zCompassName | Name string (24 chars max.) of compass to address. |
| id | string | zCompassCmd | Command string (80 chars max.) to send to compass. |
| out | string | zCompassReply | Answer string (100 chars max.) from compass to compass command. |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="CompassCmd">
    <Param name="zCompassName">GH150@ADD153SR</Param>
    <Param name="zCompassCmd">?w</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CompassCmd",
        "param": [
          {
            "name": "zCompassName",
            "#text": "GH150@ADD153SR"
          },
          {
            "name": "zCompassCmd",
            "#text": "?w"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="CompassCmd">
    <Param name="zCompassName">GH150@ADD153SR</Param>
    <Param name="zCompassCmd">?w</Param>
    <Param name="zCompassReply">>?w 4100014,D,C,C100,04/10/14*2D</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CompassCmd",
        "param": [
          {
            "name": "zCompassName",
            "#text": "GH150@ADD153SR"
          },
          {
            "name": "zCompassCmd",
            "#text": "?w"
          },
          {
            "name": "zCompassReply",
            "#text": ">?w 4100014,D,C,C100,04/10/14*2D"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.9 CompassHeading  <a id="CompassHeading"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | string | zCompassName | Name (24 chars max.) of compass to address. |
| out | int32 | iHeading | Current heading of compass [1/10 °]. |
| out | eHEADING_TYPE | eHeadingType | Type of heading. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="CompassHeading">
    <Param name="zCompassName">GH150@ADD153SR</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CompassHeading",
        "param": {
          "name": "zCompassName",
          "#text": "GH150@ADD153SR"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="CompassHeading">
    <Param name="zCompassName">GH150@ADD153SR</Param>
    <Param name="iHeading">3475</Param>
    <Param name="eHeadingType">HEADING_TYPE_COMPASS</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CompassHeading",
        "param": [
          {
            "name": "zCompassName",
            "#text": "GH150@ADD153SR"
          },
          {
            "name": "iHeading",
            "#text": "3475"
          },
          {
            "name": "eHeadingType",
            "#text": "HEADING_TYPE_COMPASS"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.10 CompassProperties  <a id="CompassProperties"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | string | zCompassName | Name (24 chars max.) of compass to address. |
| out | eCOMPASS_CODE | eCompassCode | Type of compass. |
| out | string | zAntennaName | Type (24 chars max.) of antenna the compass is connected to (empty string if compass
                        is not an antenna compass). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="CompassProperties">
    <Param name="zCompassName">GH150@ADD153SR</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CompassProperties",
        "param": {
          "name": "zCompassName",
          "#text": "GH150@ADD153SR"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="CompassProperties">
    <Param name="zCompassName">GH150@ADD153SR</Param>
    <Param name="eCompassCode">COMPASS_GH150</Param>
    <Param name="zAntennaName">ADD153SR</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CompassProperties",
        "param": [
          {
            "name": "zCompassName",
            "#text": "GH150@ADD153SR"
          },
          {
            "name": "eCompassCode",
            "#text": "COMPASS_GH150"
          },
          {
            "name": "zAntennaName",
            "#text": "ADD153SR"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.11 CompassSetup  <a id="CompassSetup"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | string | zCompassName | Name (24 chars max.) of compass to address. |
| i/o | int32 | iHeadingOffset | Heading offset [1/10 °] (range 0 to 3599: 0° to 359.9°): will be added to the compass
                        value. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="CompassSetup">
    <Param name="zCompassName">GH150@ADD153SR</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CompassSetup",
        "param": {
          "name": "zCompassName",
          "#text": "GH150@ADD153SR"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="CompassSetup">
    <Param name="zCompassName">GH150@ADD153SR</Param>
    <Param name="iHeadingOffset">14</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CompassSetup",
        "param": [
          {
            "name": "zCompassName",
            "#text": "GH150@ADD153SR"
          },
          {
            "name": "iHeadingOffset",
            "#text": "14"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="CompassSetup">
    <Param name="zCompassName">GH150@ADD153SR</Param>
    <Param name="iHeadingOffset">14</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CompassSetup",
        "param": [
          {
            "name": "zCompassName",
            "#text": "GH150@ADD153SR"
          },
          {
            "name": "iHeadingOffset",
            "#text": "14"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="CompassSetup">
    <Param name="zCompassName">GH150@ADD153SR</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CompassSetup",
        "param": {
          "name": "zCompassName",
          "#text": "GH150@ADD153SR"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.12 GPSProperties  <a id="GPSProperties"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | string | zGPSName | Name (24 chars max.) of GPS receiver to address. |
| out | eGPS_CODE | eGPSCode | GPS receiver type. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="GPSProperties">
    <Param name="zGPSName">GPS_NMEA</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "GPSProperties",
        "param": {
          "name": "zGPSName",
          "#text": "GPS_NMEA"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="GPSProperties">
    <Param name="zGPSName">GPS_NMEA</Param>
    <Param name="eGPSCode">GPS_NMEA</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "GPSProperties",
        "param": [
          {
            "name": "zGPSName",
            "#text": "GPS_NMEA"
          },
          {
            "name": "eGPSCode",
            "#text": "GPS_NMEA"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.13 GPSSetup  <a id="GPSSetup"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | string | zGPSName | Name (24 chars max.) of GPS receiver to address. |
| i/o | eGPS_EDGE | eGpsEdge | Selected edge (always raising) of PPS. |
| i/o | int32 | eGpsTimeOffset | Timing offset for GPS/NMEA [ms] (0 to 999; only with clock start internal: command
                        "ClockSettings: eClockStart=CLOCK_START_AUTO"). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="GPSSetup">
    <Param name="zGPSName">GPS_NMEA</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "GPSSetup",
        "param": {
          "name": "zGPSName",
          "#text": "GPS_NMEA"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="GPSSetup">
    <Param name="zGPSName">GPS_NMEA</Param>
    <Param name="eGpsEdge">EDGE_RAISING</Param>
    <Param name="eGpsTimeOffset">0</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "GPSSetup",
        "param": [
          {
            "name": "zGPSName",
            "#text": "GPS_NMEA"
          },
          {
            "name": "eGpsEdge",
            "#text": "EDGE_RAISING"
          },
          {
            "name": "eGpsTimeOffset",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="GPSSetup">
    <Param name="zGPSName">GPS_NMEA</Param>
    <Param name="eGpsEdge">EDGE_RAISING</Param>
    <Param name="eGpsTimeOffset">0</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "GPSSetup",
        "param": [
          {
            "name": "zGPSName",
            "#text": "GPS_NMEA"
          },
          {
            "name": "eGpsEdge",
            "#text": "EDGE_RAISING"
          },
          {
            "name": "eGpsTimeOffset",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="GPSSetup">
    <Param name="zGPSName">GPS_NMEA</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "GPSSetup",
        "param": {
          "name": "zGPSName",
          "#text": "GPS_NMEA"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.14 IGT  <a id="IGT"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eGPS_OPMODE | eGpsOpMode | IGT operation mode. |
| i/o | int32 | iGpsAvgAccuracy | Averaging accuracy [cm] (range 1 to 10000: 1 cm to 100 m). |
| i/o | int32 | iGpsAvgObserveTime | Nominal averaging time (timeout) [s] (range 0 to 2147483647: 0 s to 68 a 35 d 3 h
                        14 m 7 s). |
| i/o | int32 | iGpsAvgMinObserveTime | Averaging minimal observation time [s] (range 0 to 86400: 0 s to 1 d). |
| i/o | int32 | iGpsAntCableLength | Length of cable from GPS antenna [cm] (range 0 to 10000: 0 cm to 100 m). |
| i/o | eGPS_ANT_TYPE | eGpsAntType | Type of GPS antenna. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="IGT">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "IGT"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="IGT">
    <Param name="eGpsOpMode">GPS_OPMODE_FreeRun</Param>
    <Param name="iGpsAvgAccuracy">50</Param>
    <Param name="iGpsAvgObserveTime">0</Param>
    <Param name="iGpsAvgMinObserveTime">3600</Param>
    <Param name="iGpsAntCableLength">500</Param>
    <Param name="eGpsAntType">GPS_ANT_GPS_ANT_ACTIVE</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "IGT",
        "param": [
          {
            "name": "eGpsOpMode",
            "#text": "GPS_OPMODE_FreeRun"
          },
          {
            "name": "iGpsAvgAccuracy",
            "#text": "50"
          },
          {
            "name": "iGpsAvgObserveTime",
            "#text": "0"
          },
          {
            "name": "iGpsAvgMinObserveTime",
            "#text": "3600"
          },
          {
            "name": "iGpsAntCableLength",
            "#text": "500"
          },
          {
            "name": "eGpsAntType",
            "#text": "GPS_ANT_GPS_ANT_ACTIVE"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="IGT">
    <Param name="eGpsOpMode">GPS_OPMODE_FreeRun</Param>
    <Param name="iGpsAvgAccuracy">50</Param>
    <Param name="iGpsAvgObserveTime">0</Param>
    <Param name="iGpsAvgMinObserveTime">3600</Param>
    <Param name="iGpsAntCableLength">500</Param>
    <Param name="eGpsAntType">GPS_ANT_GPS_ANT_ACTIVE</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "IGT",
        "param": [
          {
            "name": "eGpsOpMode",
            "#text": "GPS_OPMODE_FreeRun"
          },
          {
            "name": "iGpsAvgAccuracy",
            "#text": "50"
          },
          {
            "name": "iGpsAvgObserveTime",
            "#text": "0"
          },
          {
            "name": "iGpsAvgMinObserveTime",
            "#text": "3600"
          },
          {
            "name": "iGpsAntCableLength",
            "#text": "500"
          },
          {
            "name": "eGpsAntType",
            "#text": "GPS_ANT_GPS_ANT_ACTIVE"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="IGT">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "IGT"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.15 IGTGnssState  <a id="IGTGnssState"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | int32 | iMaxActive | Maximum number of satellite systems permitted to be active at a time (IGT: 1; IGT2:
                        2). |
| i/o | eGNSS_STATE | eGPSState | Select or deselect GPS (on: selected, off: deselected). |
| i/o | eGNSS_STATE | eGlonassState | Select or deselect Glonass (on: selected, off: deselected). |
| i/o | eGNSS_STATE | eBeiDouState | Select or deselect Beidou (on: selected, off: deselected). |
| i/o | eGNSS_STATE | eGalileoState | Galileo currently not available (parameter not modifiable: reserved for future use). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="IGTGnssState">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "IGTGnssState"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="IGTGnssState">
    <Param name="iMaxActive">1</Param>
    <Param name="eGPSState">GNSS_STATE_GNSS_STATE_ON</Param>
    <Param name="eGlonassState">GNSS_STATE_GNSS_STATE_OFF</Param>
    <Param name="eBeiDouState">GNSS_STATE_GNSS_STATE_OFF</Param>
    <Param name="eGalileoState">GNSS_STATE_GNSS_STATE_NOT_AVAILABLE</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "IGTGnssState",
        "param": [
          {
            "name": "iMaxActive",
            "#text": "1"
          },
          {
            "name": "eGPSState",
            "#text": "GNSS_STATE_GNSS_STATE_ON"
          },
          {
            "name": "eGlonassState",
            "#text": "GNSS_STATE_GNSS_STATE_OFF"
          },
          {
            "name": "eBeiDouState",
            "#text": "GNSS_STATE_GNSS_STATE_OFF"
          },
          {
            "name": "eGalileoState",
            "#text": "GNSS_STATE_GNSS_STATE_NOT_AVAILABLE"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="IGTGnssState">
    <Param name="eGPSState">GNSS_STATE_GNSS_STATE_ON</Param>
    <Param name="eGlonassState">GNSS_STATE_GNSS_STATE_OFF</Param>
    <Param name="eBeiDouState">GNSS_STATE_GNSS_STATE_OFF</Param>
    <Param name="eGalileoState">GNSS_STATE_GNSS_STATE_NOT_AVAILABLE</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "IGTGnssState",
        "param": [
          {
            "name": "eGPSState",
            "#text": "GNSS_STATE_GNSS_STATE_ON"
          },
          {
            "name": "eGlonassState",
            "#text": "GNSS_STATE_GNSS_STATE_OFF"
          },
          {
            "name": "eBeiDouState",
            "#text": "GNSS_STATE_GNSS_STATE_OFF"
          },
          {
            "name": "eGalileoState",
            "#text": "GNSS_STATE_GNSS_STATE_NOT_AVAILABLE"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="IGTGnssState">
    <Param name="iMaxActive">1</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "IGTGnssState",
        "param": {
          "name": "iMaxActive",
          "#text": "1"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.16 IGTLocationFixed  <a id="IGTLocationFixed"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eDIRECTION | eLatRef | Latitude reference (north or south). |
| i/o | int32 | eLatDeg | Latitude degrees [°]. |
| i/o | int32 | eLatMin | Latitude minutes [1/1000000 min]. |
| i/o | eDIRECTION | eLonRef | Longitude reference (east or west). |
| i/o | int32 | eLonDeg | Longitude degrees [°]. |
| i/o | int32 | eLonMin | Longitude minutes [1/1000000 min]. |
| i/o | int32 | iAltitude | Altitude [cm] above MSL (Mean Sea Level). |
| i/o | boolean | bGeoSepValid | Geoidal separation (true: valid, false: invalid). |
| i/o | int32 | iGeoSepValue | Value of the geoidal separation [cm]. Difference between height above WGS84 ellipsoid
                        and height above mean sea level: heightWGS84 = heightMSL + geoidalSeparation. |
| i/o | string | zLocationName | Name (100 chars max.) of location: user defined string. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="IGTLocationFixed">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "IGTLocationFixed"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="IGTLocationFixed">
    <Param name="eLatRef">DIRECTION_NORTH</Param>
    <Param name="eLatDeg">48</Param>
    <Param name="eLatMin">7638480</Param>
    <Param name="eLonRef">DIRECTION_EAST</Param>
    <Param name="eLonDeg">11</Param>
    <Param name="eLonMin">36739260</Param>
    <Param name="iAltitude">54060</Param>
    <Param name="bGeoSepValid">true</Param>
    <Param name="iGeoSepValue">4620</Param>
    <Param name="zLocationName">MUNICH</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "IGTLocationFixed",
        "param": [
          {
            "name": "eLatRef",
            "#text": "DIRECTION_NORTH"
          },
          {
            "name": "eLatDeg",
            "#text": "48"
          },
          {
            "name": "eLatMin",
            "#text": "7638480"
          },
          {
            "name": "eLonRef",
            "#text": "DIRECTION_EAST"
          },
          {
            "name": "eLonDeg",
            "#text": "11"
          },
          {
            "name": "eLonMin",
            "#text": "36739260"
          },
          {
            "name": "iAltitude",
            "#text": "54060"
          },
          {
            "name": "bGeoSepValid",
            "#text": "true"
          },
          {
            "name": "iGeoSepValue",
            "#text": "4620"
          },
          {
            "name": "zLocationName",
            "#text": "MUNICH"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="IGTLocationFixed">
    <Param name="eLatRef">DIRECTION_NORTH</Param>
    <Param name="eLatDeg">48</Param>
    <Param name="eLatMin">7638480</Param>
    <Param name="eLonRef">DIRECTION_EAST</Param>
    <Param name="eLonDeg">11</Param>
    <Param name="eLonMin">36739260</Param>
    <Param name="iAltitude">54060</Param>
    <Param name="bGeoSepValid">true</Param>
    <Param name="iGeoSepValue">4620</Param>
    <Param name="zLocationName">MUNICH</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "IGTLocationFixed",
        "param": [
          {
            "name": "eLatRef",
            "#text": "DIRECTION_NORTH"
          },
          {
            "name": "eLatDeg",
            "#text": "48"
          },
          {
            "name": "eLatMin",
            "#text": "7638480"
          },
          {
            "name": "eLonRef",
            "#text": "DIRECTION_EAST"
          },
          {
            "name": "eLonDeg",
            "#text": "11"
          },
          {
            "name": "eLonMin",
            "#text": "36739260"
          },
          {
            "name": "iAltitude",
            "#text": "54060"
          },
          {
            "name": "bGeoSepValid",
            "#text": "true"
          },
          {
            "name": "iGeoSepValue",
            "#text": "4620"
          },
          {
            "name": "zLocationName",
            "#text": "MUNICH"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="IGTLocationFixed">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "IGTLocationFixed"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.17 IGTReset  <a id="IGTReset"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | eGPS_RESET | eGpsReset | Resets integrated GPS module. |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="IGTReset">
    <Param name="eGpsReset">GPS_RESET_GPS_RESET_COLD</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "IGTReset",
        "param": {
          "name": "eGpsReset",
          "#text": "GPS_RESET_GPS_RESET_COLD"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="IGTReset">
    <Param name="eGpsReset">GPS_RESET_GPS_RESET_COLD</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "IGTReset",
        "param": {
          "name": "eGpsReset",
          "#text": "GPS_RESET_GPS_RESET_COLD"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.18 IGTStatus  <a id="IGTStatus"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | eGPS_OPMODE_STATUS | eGpsOpModeStatus | GPS operation mode. |
| out | eGPS_ERROR | eGpsError | GPS error status. |
| out | eGPS_ANT_STATUS | eGpsAntStatus | GPS antenna error status. |
| out | int32 | eGpsAvgMeanStdDev | Momentary standard deviation [cm] of averaging in progress. |
| out | boolean | eGpsAvgStatusValid | GPS averaging observation time and GPS averaging mean 3D standard deviation valid
                        (true: valid, false: invalid). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="IGTStatus">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "IGTStatus"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="IGTStatus">
    <Param name="eGpsOpModeStatus">GPS_OPMODE_STAT_FREE_RUN</Param>
    <Param name="eGpsError">_GPS_NO_ERROR</Param>
    <Param name="eGpsAntStatus">GPS_ANT_STATUS_NO_ERROR</Param>
    <Param name="eGpsAvgMeanStdDev">27</Param>
    <Param name="eGpsAvgStatusValid">true</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "IGTStatus",
        "param": [
          {
            "name": "eGpsOpModeStatus",
            "#text": "GPS_OPMODE_STAT_FREE_RUN"
          },
          {
            "name": "eGpsError",
            "#text": "_GPS_NO_ERROR"
          },
          {
            "name": "eGpsAntStatus",
            "#text": "GPS_ANT_STATUS_NO_ERROR"
          },
          {
            "name": "eGpsAvgMeanStdDev",
            "#text": "27"
          },
          {
            "name": "eGpsAvgStatusValid",
            "#text": "true"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.19 SwCompassHeading  <a id="SwCompassHeading"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | int32 | iHeading | Heading of software compass [1/10 °]. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="SwCompassHeading">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SwCompassHeading"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="SwCompassHeading">
    <Param name="iHeading">3555</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SwCompassHeading",
        "param": {
          "name": "iHeading",
          "#text": "3555"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="SwCompassHeading">
    <Param name="iHeading">3555</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SwCompassHeading",
        "param": {
          "name": "iHeading",
          "#text": "3555"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="SwCompassHeading">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SwCompassHeading"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.6.20 XGStatus  <a id="XGStatus"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | boolean | iLinkUp | XG (10 Gbit/s) loopback interface available (true: yes, false: no). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="XGStatus">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "XGStatus"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="XGStatus">
    <Param name="iLinkUp">false</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "XGStatus",
        "param": {
          "name": "iLinkUp",
          "#text": "false"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

## 1.7 Rx-Commands

### 1.7.1 Abort  <a id="Abort"></a>

_No members (command takes no parameters)._

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="Abort">
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Abort"
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="Abort">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Abort"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.7.2 Initiate  <a id="Initiate"></a>

_No members (command takes no parameters)._

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="Initiate">
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Initiate"
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="Initiate">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Initiate"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.7.3 MeasureSettingsPScan  <a id="MeasureSettingsPScan"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | int64 | iFreqBegin | Start frequency [Hz] of range. |
| i/o | int64 | iFreqEnd | Stop frequency [Hz] of range. |
| i/o | ePSCAN_STEP | eStep | Frequency step of range. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="MeasureSettingsPScan">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "MeasureSettingsPScan"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="MeasureSettingsPScan">
    <Param name="iFreqBegin">100000000</Param>
    <Param name="iFreqEnd">400000000</Param>
    <Param name="eStep">PSCAN_STEP_1000</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "MeasureSettingsPScan",
        "param": [
          {
            "name": "iFreqBegin",
            "#text": "100000000"
          },
          {
            "name": "iFreqEnd",
            "#text": "400000000"
          },
          {
            "name": "eStep",
            "#text": "PSCAN_STEP_1000"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="MeasureSettingsPScan">
    <Param name="iFreqBegin">100000000</Param>
    <Param name="iFreqEnd">400000000</Param>
    <Param name="eStep">PSCAN_STEP_1000</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "MeasureSettingsPScan",
        "param": [
          {
            "name": "iFreqBegin",
            "#text": "100000000"
          },
          {
            "name": "iFreqEnd",
            "#text": "400000000"
          },
          {
            "name": "eStep",
            "#text": "PSCAN_STEP_1000"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="MeasureSettingsPScan">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "MeasureSettingsPScan"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.7.4 RxSettings  <a id="RxSettings"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | int32 | iMeasureTime | Measuring time [µs] (500 to 900000000 = 500 µs to 15 min; 0: use default times). |
| i/o | eMEASUREMODECP | eMeasureMode | Measuring mode. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="RxSettings">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "RxSettings"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="RxSettings">
    <Param name="iMeasureTime">500</Param>
    <Param name="eMeasureMode">MEASUREMODECP_CONT</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "RxSettings",
        "param": [
          {
            "name": "iMeasureTime",
            "#text": "500"
          },
          {
            "name": "eMeasureMode",
            "#text": "MEASUREMODECP_CONT"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="RxSettings">
    <Param name="iMeasureTime">500</Param>
    <Param name="eMeasureMode">MEASUREMODECP_CONT</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "RxSettings",
        "param": [
          {
            "name": "iMeasureTime",
            "#text": "500"
          },
          {
            "name": "eMeasureMode",
            "#text": "MEASUREMODECP_CONT"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="RxSettings">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "RxSettings"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

## 1.8 Scan-Commands

### 1.8.1 ScanRange  <a id="ScanRange"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | int32 | iScanRangeId | ID of scan range to address. |
| i/o | int64 | iFreqBegin | Start frequency [Hz]. |
| i/o | int64 | iFreqEnd | Stop frequency [Hz]. |
| i/o | eANT_POL | eAntPol | Antenna polarization of received signal. |
| i/o | eSPAN | eSpan | Realtime bandwidth (frequency span). If also using Short-Time Detector, .eSpan must
                        coincide for all scan ranges involved. |
| i/o | eDFPAN_STEP | eDfPanStep | DF channel spacing. |
| i/o | eAVERAGE_MODE | eAvgMode | Averaging mode. |
| i/o | eBLOCK_AVERAGING_SELECT | eBlockAveragingSelect | Select number of averaging cycles or time duration for block averaging. |
| i/o | int32 | iBlockAveragingCycles | Number of averaging cycles (if selected). |
| i/o | int32 | iBlockAveragingTime | Block averaging time [ms] (if selected). |
| i/o | int32 | iHopDwellTime | Time [ms] to dwell on each hop |
| i/o | int32 | iThreshold | Level threshold [dBµV]. |
| i/o | eSTATE | eAntPreAmp | Antenna preamplifier on or off. |
| i/o | eDF_ALT | eDfAlt | DF evaluation principle (DF alternative). |
| i/o | eWINDOW_TYPE | eWindowType | FFT window type (function). |
| i/o | eDFPAN_SELECTIVITY | eDfPanSelectivity | DF panorama selectivity. |
| i/o | eATT_SELECT | eAttSelect | Attenuation setting manual or automatic. |
| i/o | int32 | iAttValue | Attenuation [dB] (range 0 to 40) (if manual). |
| i/o | int32 | iAttHoldTime | Time duration [1/10 s] (range 0 to 100: 0 s to 10 s) attenuation value stays unchanged
                        if input level drops. |
| i/o | int32 | iSrEmitterEstimation | Super-resolution (SR) emitter estimation or predetermined emitter count for all channels
                        (0: work in auto mode; 1 to 9: force specific count)                  (option R&S
                        DDFx-SR, Super-Resolution, only). |
| out | int32 | iNumHops | Number of hops. |
| i/o | int64 | iTsTimeMeas | Time [ns] to measure one frequency hop (synchronous measurement mode) (-1: fastest
                        possible)                  (option R&S DDFx-TS, Time-Synchronous Scanning/Synchronization/Time
                        Synchronization, only; subject to change). |
| i/o | int64 | iTsTimeFreqChange | Time [ns] to switch to next frequency (synchronous measurement mode) (-1: fastest
                        possible) (option R&S DDFx-TS only; subject to change). |
| i/o | int64 | iTsTimeScanRange | Time [ns] to switch to next scan range (synchronous measurement mode) (-1: fastest
                        possible) (option R&S DDFx-TS only; subject to change). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="ScanRange">
    <Param name="iScanRangeId">23</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ScanRange",
        "param": {
          "name": "iScanRangeId",
          "#text": "23"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="ScanRange">
    <Param name="iScanRangeId">23</Param>
    <Param name="iFreqBegin">1000000000</Param>
    <Param name="iFreqEnd">1200000000</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="eSpan">IFPAN_FREQ_RANGE_80000</Param>
    <Param name="eDfPanStep">DFPAN_STEP_100KHZ</Param>
    <Param name="eAvgMode">DFSQU_OFF</Param>
    <Param name="eBlockAveragingSelect">BLOCK_AVERAGING_SELECT_TIME</Param>
    <Param name="iBlockAveragingCycles">200</Param>
    <Param name="iBlockAveragingTime">100</Param>
    <Param name="iHopDwellTime">500</Param>
    <Param name="iThreshold">10</Param>
    <Param name="eAntPreAmp">STATE_OFF</Param>
    <Param name="eDfAlt">DFALT_AUTO</Param>
    <Param name="eWindowType">DF_WINDOW_TYPE_BLACKMAN_HARRIS</Param>
    <Param name="eDfPanSelectivity">DFPAN_SELECTIVITY_AUTO</Param>
    <Param name="eAttSelect">ATT_MANUAL</Param>
    <Param name="iAttValue">0</Param>
    <Param name="iAttHoldTime">10</Param>
    <Param name="iSrEmitterEstimation">0</Param>
    <Param name="iNumHops">10</Param>
    <Param name="iTsTimeMeas">-1</Param>
    <Param name="iTsTimeFreqChange">-1</Param>
    <Param name="iTsTimeScanRange">-1</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ScanRange",
        "param": [
          {
            "name": "iScanRangeId",
            "#text": "23"
          },
          {
            "name": "iFreqBegin",
            "#text": "1000000000"
          },
          {
            "name": "iFreqEnd",
            "#text": "1200000000"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "eSpan",
            "#text": "IFPAN_FREQ_RANGE_80000"
          },
          {
            "name": "eDfPanStep",
            "#text": "DFPAN_STEP_100KHZ"
          },
          {
            "name": "eAvgMode",
            "#text": "DFSQU_OFF"
          },
          {
            "name": "eBlockAveragingSelect",
            "#text": "BLOCK_AVERAGING_SELECT_TIME"
          },
          {
            "name": "iBlockAveragingCycles",
            "#text": "200"
          },
          {
            "name": "iBlockAveragingTime",
            "#text": "100"
          },
          {
            "name": "iHopDwellTime",
            "#text": "500"
          },
          {
            "name": "iThreshold",
            "#text": "10"
          },
          {
            "name": "eAntPreAmp",
            "#text": "STATE_OFF"
          },
          {
            "name": "eDfAlt",
            "#text": "DFALT_AUTO"
          },
          {
            "name": "eWindowType",
            "#text": "DF_WINDOW_TYPE_BLACKMAN_HARRIS"
          },
          {
            "name": "eDfPanSelectivity",
            "#text": "DFPAN_SELECTIVITY_AUTO"
          },
          {
            "name": "eAttSelect",
            "#text": "ATT_MANUAL"
          },
          {
            "name": "iAttValue",
            "#text": "0"
          },
          {
            "name": "iAttHoldTime",
            "#text": "10"
          },
          {
            "name": "iSrEmitterEstimation",
            "#text": "0"
          },
          {
            "name": "iNumHops",
            "#text": "10"
          },
          {
            "name": "iTsTimeMeas",
            "#text": "-1"
          },
          {
            "name": "iTsTimeFreqChange",
            "#text": "-1"
          },
          {
            "name": "iTsTimeScanRange",
            "#text": "-1"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="ScanRange">
    <Param name="iScanRangeId">23</Param>
    <Param name="iFreqBegin">1000000000</Param>
    <Param name="iFreqEnd">1200000000</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="eSpan">IFPAN_FREQ_RANGE_80000</Param>
    <Param name="eDfPanStep">DFPAN_STEP_100KHZ</Param>
    <Param name="eAvgMode">DFSQU_OFF</Param>
    <Param name="eBlockAveragingSelect">BLOCK_AVERAGING_SELECT_TIME</Param>
    <Param name="iBlockAveragingCycles">200</Param>
    <Param name="iBlockAveragingTime">100</Param>
    <Param name="iHopDwellTime">500</Param>
    <Param name="iThreshold">10</Param>
    <Param name="eAntPreAmp">STATE_OFF</Param>
    <Param name="eDfAlt">DFALT_AUTO</Param>
    <Param name="eWindowType">DF_WINDOW_TYPE_BLACKMAN_HARRIS</Param>
    <Param name="eDfPanSelectivity">DFPAN_SELECTIVITY_AUTO</Param>
    <Param name="eAttSelect">ATT_MANUAL</Param>
    <Param name="iAttValue">0</Param>
    <Param name="iAttHoldTime">10</Param>
    <Param name="iSrEmitterEstimation">0</Param>
    <Param name="iTsTimeMeas">-1</Param>
    <Param name="iTsTimeFreqChange">-1</Param>
    <Param name="iTsTimeScanRange">-1</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ScanRange",
        "param": [
          {
            "name": "iScanRangeId",
            "#text": "23"
          },
          {
            "name": "iFreqBegin",
            "#text": "1000000000"
          },
          {
            "name": "iFreqEnd",
            "#text": "1200000000"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "eSpan",
            "#text": "IFPAN_FREQ_RANGE_80000"
          },
          {
            "name": "eDfPanStep",
            "#text": "DFPAN_STEP_100KHZ"
          },
          {
            "name": "eAvgMode",
            "#text": "DFSQU_OFF"
          },
          {
            "name": "eBlockAveragingSelect",
            "#text": "BLOCK_AVERAGING_SELECT_TIME"
          },
          {
            "name": "iBlockAveragingCycles",
            "#text": "200"
          },
          {
            "name": "iBlockAveragingTime",
            "#text": "100"
          },
          {
            "name": "iHopDwellTime",
            "#text": "500"
          },
          {
            "name": "iThreshold",
            "#text": "10"
          },
          {
            "name": "eAntPreAmp",
            "#text": "STATE_OFF"
          },
          {
            "name": "eDfAlt",
            "#text": "DFALT_AUTO"
          },
          {
            "name": "eWindowType",
            "#text": "DF_WINDOW_TYPE_BLACKMAN_HARRIS"
          },
          {
            "name": "eDfPanSelectivity",
            "#text": "DFPAN_SELECTIVITY_AUTO"
          },
          {
            "name": "eAttSelect",
            "#text": "ATT_MANUAL"
          },
          {
            "name": "iAttValue",
            "#text": "0"
          },
          {
            "name": "iAttHoldTime",
            "#text": "10"
          },
          {
            "name": "iSrEmitterEstimation",
            "#text": "0"
          },
          {
            "name": "iTsTimeMeas",
            "#text": "-1"
          },
          {
            "name": "iTsTimeFreqChange",
            "#text": "-1"
          },
          {
            "name": "iTsTimeScanRange",
            "#text": "-1"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="ScanRange">
    <Param name="iScanRangeId">23</Param>
    <Param name="iNumHops">10</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ScanRange",
        "param": [
          {
            "name": "iScanRangeId",
            "#text": "23"
          },
          {
            "name": "iNumHops",
            "#text": "10"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.8.2 ScanRangeAdd  <a id="ScanRangeAdd"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | int64 | iFreqBegin | Start frequency [Hz]. |
| id | int64 | iFreqEnd | Stop frequency [Hz]. |
| auto | eANT_POL | eAntPol | Antenna polarization of received signal.
                        (default value: 
                        POL_VERTICAL) |
| id | eSPAN | eSpan | Realtime bandwidth (frequency span). If also using Short-Time Detector, .eSpan must
                        coincide for all scan ranges involved. |
| id | eDFPAN_STEP | eDfPanStep | DF channel spacing. |
| auto | eAVERAGE_MODE | eAvgMode | Averaging mode.
                        (default value: 
                        DFSQU_OFF) |
| auto | eBLOCK_AVERAGING_SELECT | eBlockAveragingSelect | Select number of averaging cycles or time duration for block averaging.
                        (default value: 
                        BLOCK_AVERAGING_SELECT_TIME) |
| auto | int32 | iBlockAveragingCycles | Number of averaging cycles (if selected).
                        (default value: 
                        200) |
| auto | int32 | iBlockAveragingTime | Block averaging time [ms] (if selected).
                        (default value: 
                        100) |
| auto | int32 | iHopDwellTime | Time [ms] to dwell on each hop
                        (default value: 
                        0) |
| auto | int32 | iThreshold | Level threshold [dBµV].
                        (default value: 
                        10) |
| auto | eSTATE | eAntPreAmp | Antenna preamplifier on or off.
                        (default value: 
                        STATE_OFF) |
| auto | eDF_ALT | eDfAlt | DF evaluation principle (DF alternative).
                        (default value: 
                        DFALT_AUTO) |
| auto | eWINDOW_TYPE | eWindowType | FFT window type (function).
                        (default value: 
                        DF_WINDOW_TYPE_BLACKMAN_HARRIS) |
| auto | eDFPAN_SELECTIVITY | eDfPanSelectivity | DF panorama selectivity.
                        (default value: 
                        DFPAN_SELECTIVITY_AUTO) |
| auto | eATT_SELECT | eAttSelect | Attenuation setting manual or automatic.
                        (default value: 
                        ATT_MANUAL) |
| auto | int32 | iAttValue | Attenuation [dB] (range 0 to 40) (if manual).
                        (default value: 
                        0) |
| auto | int32 | iAttHoldTime | Time duration [1/10 s] (range 0 to 100: 0 s to 10 s) attenuation value stays unchanged
                        if input level drops.
                        (default value: 
                        10) |
| auto | int32 | iSrEmitterEstimation | Super-resolution (SR) emitter estimation or predetermined emitter count for all channels
                        (0: work in auto mode; 1 to 9: force specific count)                  (option R&S
                        DDFx-SR, Super-Resolution, only).
                        (default value: 
                        0) |
| out | int32 | iScanRangeId | ID of scan range created. |
| out | int32 | iNumHops | Number of hops. |
| out | int64 | iTsTimeMeas | Time [ns] to measure one frequency hop (synchronous measurement mode) (-1: fastest
                        possible)                  (option R&S DDFx-TS, Time-Synchronous Scanning/Synchronization/Time
                        Synchronization, only; subject to change). |
| out | int64 | iTsTimeFreqChange | Time [ns] to switch to next frequency (synchronous measurement mode) (-1: fastest
                        possible) (option R&S DDFx-TS only; subject to change). |
| out | int64 | iTsTimeScanRange | Time [ns] to switch to next scan range (synchronous measurement mode) (-1: fastest
                        possible) (option R&S DDFx-TS only; subject to change). |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="ScanRangeAdd">
    <Param name="iFreqBegin">1000000000</Param>
    <Param name="iFreqEnd">1200000000</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="eSpan">IFPAN_FREQ_RANGE_80000</Param>
    <Param name="eDfPanStep">DFPAN_STEP_100KHZ</Param>
    <Param name="eAvgMode">DFSQU_OFF</Param>
    <Param name="eBlockAveragingSelect">BLOCK_AVERAGING_SELECT_TIME</Param>
    <Param name="iBlockAveragingCycles">200</Param>
    <Param name="iBlockAveragingTime">100</Param>
    <Param name="iHopDwellTime">0</Param>
    <Param name="iThreshold">10</Param>
    <Param name="eAntPreAmp">STATE_OFF</Param>
    <Param name="eDfAlt">DFALT_AUTO</Param>
    <Param name="eWindowType">DF_WINDOW_TYPE_BLACKMAN_HARRIS</Param>
    <Param name="eDfPanSelectivity">DFPAN_SELECTIVITY_AUTO</Param>
    <Param name="eAttSelect">ATT_MANUAL</Param>
    <Param name="iAttValue">0</Param>
    <Param name="iAttHoldTime">10</Param>
    <Param name="iSrEmitterEstimation">0</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ScanRangeAdd",
        "param": [
          {
            "name": "iFreqBegin",
            "#text": "1000000000"
          },
          {
            "name": "iFreqEnd",
            "#text": "1200000000"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "eSpan",
            "#text": "IFPAN_FREQ_RANGE_80000"
          },
          {
            "name": "eDfPanStep",
            "#text": "DFPAN_STEP_100KHZ"
          },
          {
            "name": "eAvgMode",
            "#text": "DFSQU_OFF"
          },
          {
            "name": "eBlockAveragingSelect",
            "#text": "BLOCK_AVERAGING_SELECT_TIME"
          },
          {
            "name": "iBlockAveragingCycles",
            "#text": "200"
          },
          {
            "name": "iBlockAveragingTime",
            "#text": "100"
          },
          {
            "name": "iHopDwellTime",
            "#text": "0"
          },
          {
            "name": "iThreshold",
            "#text": "10"
          },
          {
            "name": "eAntPreAmp",
            "#text": "STATE_OFF"
          },
          {
            "name": "eDfAlt",
            "#text": "DFALT_AUTO"
          },
          {
            "name": "eWindowType",
            "#text": "DF_WINDOW_TYPE_BLACKMAN_HARRIS"
          },
          {
            "name": "eDfPanSelectivity",
            "#text": "DFPAN_SELECTIVITY_AUTO"
          },
          {
            "name": "eAttSelect",
            "#text": "ATT_MANUAL"
          },
          {
            "name": "iAttValue",
            "#text": "0"
          },
          {
            "name": "iAttHoldTime",
            "#text": "10"
          },
          {
            "name": "iSrEmitterEstimation",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="ScanRangeAdd">
    <Param name="iFreqBegin">1000000000</Param>
    <Param name="iFreqEnd">1200000000</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="eSpan">IFPAN_FREQ_RANGE_80000</Param>
    <Param name="eDfPanStep">DFPAN_STEP_100KHZ</Param>
    <Param name="eAvgMode">DFSQU_OFF</Param>
    <Param name="eBlockAveragingSelect">BLOCK_AVERAGING_SELECT_TIME</Param>
    <Param name="iBlockAveragingCycles">200</Param>
    <Param name="iBlockAveragingTime">100</Param>
    <Param name="iHopDwellTime">0</Param>
    <Param name="iThreshold">10</Param>
    <Param name="eAntPreAmp">STATE_OFF</Param>
    <Param name="eDfAlt">DFALT_AUTO</Param>
    <Param name="eWindowType">DF_WINDOW_TYPE_BLACKMAN_HARRIS</Param>
    <Param name="eDfPanSelectivity">DFPAN_SELECTIVITY_AUTO</Param>
    <Param name="eAttSelect">ATT_MANUAL</Param>
    <Param name="iAttValue">0</Param>
    <Param name="iAttHoldTime">10</Param>
    <Param name="iSrEmitterEstimation">0</Param>
    <Param name="iScanRangeId">23</Param>
    <Param name="iNumHops">10</Param>
    <Param name="iTsTimeMeas">-1</Param>
    <Param name="iTsTimeFreqChange">-1</Param>
    <Param name="iTsTimeScanRange">-1</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ScanRangeAdd",
        "param": [
          {
            "name": "iFreqBegin",
            "#text": "1000000000"
          },
          {
            "name": "iFreqEnd",
            "#text": "1200000000"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "eSpan",
            "#text": "IFPAN_FREQ_RANGE_80000"
          },
          {
            "name": "eDfPanStep",
            "#text": "DFPAN_STEP_100KHZ"
          },
          {
            "name": "eAvgMode",
            "#text": "DFSQU_OFF"
          },
          {
            "name": "eBlockAveragingSelect",
            "#text": "BLOCK_AVERAGING_SELECT_TIME"
          },
          {
            "name": "iBlockAveragingCycles",
            "#text": "200"
          },
          {
            "name": "iBlockAveragingTime",
            "#text": "100"
          },
          {
            "name": "iHopDwellTime",
            "#text": "0"
          },
          {
            "name": "iThreshold",
            "#text": "10"
          },
          {
            "name": "eAntPreAmp",
            "#text": "STATE_OFF"
          },
          {
            "name": "eDfAlt",
            "#text": "DFALT_AUTO"
          },
          {
            "name": "eWindowType",
            "#text": "DF_WINDOW_TYPE_BLACKMAN_HARRIS"
          },
          {
            "name": "eDfPanSelectivity",
            "#text": "DFPAN_SELECTIVITY_AUTO"
          },
          {
            "name": "eAttSelect",
            "#text": "ATT_MANUAL"
          },
          {
            "name": "iAttValue",
            "#text": "0"
          },
          {
            "name": "iAttHoldTime",
            "#text": "10"
          },
          {
            "name": "iSrEmitterEstimation",
            "#text": "0"
          },
          {
            "name": "iScanRangeId",
            "#text": "23"
          },
          {
            "name": "iNumHops",
            "#text": "10"
          },
          {
            "name": "iTsTimeMeas",
            "#text": "-1"
          },
          {
            "name": "iTsTimeFreqChange",
            "#text": "-1"
          },
          {
            "name": "iTsTimeScanRange",
            "#text": "-1"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.8.3 ScanRangeDelete  <a id="ScanRangeDelete"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | int32 | iScanRangeId | ID of scan range to address (delete). |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="ScanRangeDelete">
    <Param name="iScanRangeId">23</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ScanRangeDelete",
        "param": {
          "name": "iScanRangeId",
          "#text": "23"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="ScanRangeDelete">
    <Param name="iScanRangeId">23</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ScanRangeDelete",
        "param": {
          "name": "iScanRangeId",
          "#text": "23"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.8.4 ScanRangeDeleteAll  <a id="ScanRangeDeleteAll"></a>

_No members (command takes no parameters)._

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="ScanRangeDeleteAll">
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ScanRangeDeleteAll"
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="ScanRangeDeleteAll">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ScanRangeDeleteAll"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.8.5 ScanRangeNext  <a id="ScanRangeNext"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | int32 | iScanRangeId | ID of scan range to address (scan range to start out from; -1: 1st scan range). |
| out | int32 | iNextScanRangeId | ID of next scan range (from addressed scan range; -1: no next scan range existing,
                        ignore all subsequent indications). |
| out | int64 | iFreqBegin | Start frequency [Hz]. |
| out | int64 | iFreqEnd | Stop frequency [Hz]. |
| out | eANT_POL | eAntPol | Antenna polarization of received signal. |
| out | eSPAN | eSpan | Realtime bandwidth (frequency span). If also using Short-Time Detector, .eSpan must
                        coincide for all scan ranges involved. |
| out | eDFPAN_STEP | eDfPanStep | DF channel spacing. |
| out | eAVERAGE_MODE | eAvgMode | Averaging mode. |
| out | eBLOCK_AVERAGING_SELECT | eBlockAveragingSelect | Number of averaging cycles or time duration for block averaging selected. |
| out | int32 | iBlockAveragingCycles | Number of averaging cycles (if selected). |
| out | int32 | iBlockAveragingTime | Block averaging time [ms] (if selected). |
| out | int32 | iHopDwellTime | Time [ms] to dwell on each hop |
| out | int32 | iThreshold | Level threshold [dBµV]. |
| out | eSTATE | eAntPreAmp | Antenna preamplifier on or off. |
| out | eDF_ALT | eDfAlt | DF evaluation principle (DF alternative). |
| out | eWINDOW_TYPE | eWindowType | FFT window type (function). |
| out | eDFPAN_SELECTIVITY | eDfPanSelectivity | DF panorama selectivity. |
| out | eATT_SELECT | eAttSelect | Attenuation setting manual or automatic. |
| out | int32 | iAttValue | Attenuation [dB] (range 0 to 40) (if manual). |
| out | int32 | iAttHoldTime | Time duration [1/10 s] (range 0 to 100: 0 s to 10 s) attenuation value stays unchanged
                        if input level drops. |
| out | int32 | iSrEmitterEstimation | Super-resolution (SR) emitter estimation or predetermined emitter count for all channels
                        (0: work in auto mode; 1 to 9: force specific count)                  (option R&S
                        DDFx-SR, Super-Resolution, only). |
| out | int32 | iNumHops | Number of hops. |
| out | int64 | iTsTimeMeas | Time [ns] to measure one frequency hop (synchronous measurement mode) (-1: fastest
                        possible)                  (option R&S DDFx-TS, Time-Synchronous Scanning/Synchronization/Time
                        Synchronization, only; subject to change). |
| out | int64 | iTsTimeFreqChange | Time [ns] to switch to next frequency (synchronous measurement mode) (-1: fastest
                        possible) (option R&S DDFx-TS only; subject to change). |
| out | int64 | iTsTimeScanRange | Time [ns] to switch to next scan range (synchronous measurement mode) (-1: fastest
                        possible) (option R&S DDFx-TS only; subject to change). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="ScanRangeNext">
    <Param name="iScanRangeId">23</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ScanRangeNext",
        "param": {
          "name": "iScanRangeId",
          "#text": "23"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="ScanRangeNext">
    <Param name="iScanRangeId">23</Param>
    <Param name="iNextScanRangeId">24</Param>
    <Param name="iFreqBegin">1000000000</Param>
    <Param name="iFreqEnd">1200000000</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="eSpan">IFPAN_FREQ_RANGE_80000</Param>
    <Param name="eDfPanStep">DFPAN_STEP_100KHZ</Param>
    <Param name="eAvgMode">DFSQU_OFF</Param>
    <Param name="eBlockAveragingSelect">BLOCK_AVERAGING_SELECT_TIME</Param>
    <Param name="iBlockAveragingCycles">200</Param>
    <Param name="iBlockAveragingTime">100</Param>
    <Param name="iHopDwellTime">500</Param>
    <Param name="iThreshold">10</Param>
    <Param name="eAntPreAmp">STATE_OFF</Param>
    <Param name="eDfAlt">DFALT_AUTO</Param>
    <Param name="eWindowType">DF_WINDOW_TYPE_BLACKMAN_HARRIS</Param>
    <Param name="eDfPanSelectivity">DFPAN_SELECTIVITY_AUTO</Param>
    <Param name="eAttSelect">ATT_MANUAL</Param>
    <Param name="iAttValue">0</Param>
    <Param name="iAttHoldTime">10</Param>
    <Param name="iSrEmitterEstimation">0</Param>
    <Param name="iNumHops">10</Param>
    <Param name="iTsTimeMeas">-1</Param>
    <Param name="iTsTimeFreqChange">-1</Param>
    <Param name="iTsTimeScanRange">-1</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ScanRangeNext",
        "param": [
          {
            "name": "iScanRangeId",
            "#text": "23"
          },
          {
            "name": "iNextScanRangeId",
            "#text": "24"
          },
          {
            "name": "iFreqBegin",
            "#text": "1000000000"
          },
          {
            "name": "iFreqEnd",
            "#text": "1200000000"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "eSpan",
            "#text": "IFPAN_FREQ_RANGE_80000"
          },
          {
            "name": "eDfPanStep",
            "#text": "DFPAN_STEP_100KHZ"
          },
          {
            "name": "eAvgMode",
            "#text": "DFSQU_OFF"
          },
          {
            "name": "eBlockAveragingSelect",
            "#text": "BLOCK_AVERAGING_SELECT_TIME"
          },
          {
            "name": "iBlockAveragingCycles",
            "#text": "200"
          },
          {
            "name": "iBlockAveragingTime",
            "#text": "100"
          },
          {
            "name": "iHopDwellTime",
            "#text": "500"
          },
          {
            "name": "iThreshold",
            "#text": "10"
          },
          {
            "name": "eAntPreAmp",
            "#text": "STATE_OFF"
          },
          {
            "name": "eDfAlt",
            "#text": "DFALT_AUTO"
          },
          {
            "name": "eWindowType",
            "#text": "DF_WINDOW_TYPE_BLACKMAN_HARRIS"
          },
          {
            "name": "eDfPanSelectivity",
            "#text": "DFPAN_SELECTIVITY_AUTO"
          },
          {
            "name": "eAttSelect",
            "#text": "ATT_MANUAL"
          },
          {
            "name": "iAttValue",
            "#text": "0"
          },
          {
            "name": "iAttHoldTime",
            "#text": "10"
          },
          {
            "name": "iSrEmitterEstimation",
            "#text": "0"
          },
          {
            "name": "iNumHops",
            "#text": "10"
          },
          {
            "name": "iTsTimeMeas",
            "#text": "-1"
          },
          {
            "name": "iTsTimeFreqChange",
            "#text": "-1"
          },
          {
            "name": "iTsTimeScanRange",
            "#text": "-1"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.8.6 SweepTime  <a id="SweepTime"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | int64 | iSweepTime | Sweep time [ns]. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="SweepTime">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SweepTime"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="SweepTime">
    <Param name="iSweepTime">409261552</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SweepTime",
        "param": {
          "name": "iSweepTime",
          "#text": "409261552"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

## 1.9 Search-Commands

### 1.9.1 Continue  <a id="Continue"></a>

_No members (command takes no parameters)._

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="Continue">
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Continue"
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="Continue">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Continue"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.9.2 Hold  <a id="Hold"></a>

_No members (command takes no parameters)._

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="Hold">
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Hold"
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="Hold">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Hold"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.9.3 SearchRange  <a id="SearchRange"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | int32 | iSearchRangeId | ID of search range to address. |
| i/o | int64 | iFreqBegin | Start frequency [Hz]. |
| i/o | int64 | iFreqEnd | Stop frequency [Hz]. |
| i/o | int64 | iFreqStep | Step frequency [Hz]. |
| i/o | eSPAN | eSpan | Realtime bandwidth (frequency span). |
| i/o | eDFPAN_STEP | eDfPanStep | DF channel spacing. |
| i/o | eAVERAGE_MODE | eAvgMode | Averaging mode. |
| i/o | eBLOCK_AVERAGING_SELECT | eBlockAveragingSelect | Select number of averaging cycles or time duration for block averaging. |
| i/o | int32 | iBlockAveragingCycles | Number of averaging cycles (if selected). |
| i/o | int32 | iBlockAveragingTime | Block averaging time [ms] (if selected). |
| i/o | int32 | iThreshold | Level threshold [dBµV]. |
| i/o | eANT_POL | eAntPol | Antenna polarization. |
| i/o | eSTATE | eAntPreAmp | Antenna preamplifier on or off. |
| i/o | eDF_ALT | eDfAlt | DF evaluation principle (DF alternative). |
| i/o | eWINDOW_TYPE | eWindowType | FFT window type (function). |
| i/o | eDFPAN_SELECTIVITY | eDfPanSelectivity | DF panorama selectivity. |
| i/o | eATT_SELECT | eAttSelect | Attenuation setting manual or automatic. |
| i/o | int32 | iAttValue | Attenuation [dB] (range 0 to 40) (if manual). |
| i/o | int32 | iAttHoldTime | Time duration [1/10 s] (range 0 to 100: 0 s to 10 s) attenuation value stays unchanged
                        if input level drops. |
| i/o | eDEMODULATION | eDemodulation | Demodulation mode. |
| i/o | int64 | iBfoFrequency | BFO frequency [Hz] (only for demodulation mode CW). |
| i/o | eAF_BANDWIDTH | eAfBandwidth | Demodulation bandwidth. |
| i/o | int32 | iAfThreshold | Demodulation threshold [dBµV]. |
| i/o | boolean | bUseAfThreshold | Level threshold for demodulation active (true: active, false: not active, i.e. all
                        channels regardless of their level will be demodulated. |
| i/o | int64 | iPassbandFrequency | SSB passband frequency [Hz] (only for demodulation mode USB or LSB). |
| out | int32 | iNumHops | Number of hops. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="SearchRange">
    <Param name="iSearchRangeId">73</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SearchRange",
        "param": {
          "name": "iSearchRangeId",
          "#text": "73"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="SearchRange">
    <Param name="iSearchRangeId">73</Param>
    <Param name="iFreqBegin">87500000</Param>
    <Param name="iFreqEnd">107000000</Param>
    <Param name="iFreqStep">100000</Param>
    <Param name="eSpan">IFPAN_FREQ_RANGE_80000</Param>
    <Param name="eDfPanStep">DFPAN_STEP_100KHZ</Param>
    <Param name="eAvgMode">DFSQU_OFF</Param>
    <Param name="eBlockAveragingSelect">BLOCK_AVERAGING_SELECT_TIME</Param>
    <Param name="iBlockAveragingCycles">200</Param>
    <Param name="iBlockAveragingTime">100</Param>
    <Param name="iThreshold">10</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="eAntPreAmp">STATE_OFF</Param>
    <Param name="eDfAlt">DFALT_AUTO</Param>
    <Param name="eWindowType">DF_WINDOW_TYPE_BLACKMAN_HARRIS</Param>
    <Param name="eDfPanSelectivity">DFPAN_SELECTIVITY_AUTO</Param>
    <Param name="eAttSelect">ATT_MANUAL</Param>
    <Param name="iAttValue">0</Param>
    <Param name="iAttHoldTime">10</Param>
    <Param name="eDemodulation">MOD_AM</Param>
    <Param name="iBfoFrequency">1000</Param>
    <Param name="eAfBandwidth">BW_25</Param>
    <Param name="iAfThreshold">0</Param>
    <Param name="bUseAfThreshold">false</Param>
    <Param name="iPassbandFrequency">0</Param>
    <Param name="iNumHops">10</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SearchRange",
        "param": [
          {
            "name": "iSearchRangeId",
            "#text": "73"
          },
          {
            "name": "iFreqBegin",
            "#text": "87500000"
          },
          {
            "name": "iFreqEnd",
            "#text": "107000000"
          },
          {
            "name": "iFreqStep",
            "#text": "100000"
          },
          {
            "name": "eSpan",
            "#text": "IFPAN_FREQ_RANGE_80000"
          },
          {
            "name": "eDfPanStep",
            "#text": "DFPAN_STEP_100KHZ"
          },
          {
            "name": "eAvgMode",
            "#text": "DFSQU_OFF"
          },
          {
            "name": "eBlockAveragingSelect",
            "#text": "BLOCK_AVERAGING_SELECT_TIME"
          },
          {
            "name": "iBlockAveragingCycles",
            "#text": "200"
          },
          {
            "name": "iBlockAveragingTime",
            "#text": "100"
          },
          {
            "name": "iThreshold",
            "#text": "10"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "eAntPreAmp",
            "#text": "STATE_OFF"
          },
          {
            "name": "eDfAlt",
            "#text": "DFALT_AUTO"
          },
          {
            "name": "eWindowType",
            "#text": "DF_WINDOW_TYPE_BLACKMAN_HARRIS"
          },
          {
            "name": "eDfPanSelectivity",
            "#text": "DFPAN_SELECTIVITY_AUTO"
          },
          {
            "name": "eAttSelect",
            "#text": "ATT_MANUAL"
          },
          {
            "name": "iAttValue",
            "#text": "0"
          },
          {
            "name": "iAttHoldTime",
            "#text": "10"
          },
          {
            "name": "eDemodulation",
            "#text": "MOD_AM"
          },
          {
            "name": "iBfoFrequency",
            "#text": "1000"
          },
          {
            "name": "eAfBandwidth",
            "#text": "BW_25"
          },
          {
            "name": "iAfThreshold",
            "#text": "0"
          },
          {
            "name": "bUseAfThreshold",
            "#text": "false"
          },
          {
            "name": "iPassbandFrequency",
            "#text": "0"
          },
          {
            "name": "iNumHops",
            "#text": "10"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="SearchRange">
    <Param name="iSearchRangeId">73</Param>
    <Param name="iFreqBegin">87500000</Param>
    <Param name="iFreqEnd">107000000</Param>
    <Param name="iFreqStep">100000</Param>
    <Param name="eSpan">IFPAN_FREQ_RANGE_80000</Param>
    <Param name="eDfPanStep">DFPAN_STEP_100KHZ</Param>
    <Param name="eAvgMode">DFSQU_OFF</Param>
    <Param name="eBlockAveragingSelect">BLOCK_AVERAGING_SELECT_TIME</Param>
    <Param name="iBlockAveragingCycles">200</Param>
    <Param name="iBlockAveragingTime">100</Param>
    <Param name="iThreshold">10</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="eAntPreAmp">STATE_OFF</Param>
    <Param name="eDfAlt">DFALT_AUTO</Param>
    <Param name="eWindowType">DF_WINDOW_TYPE_BLACKMAN_HARRIS</Param>
    <Param name="eDfPanSelectivity">DFPAN_SELECTIVITY_AUTO</Param>
    <Param name="eAttSelect">ATT_MANUAL</Param>
    <Param name="iAttValue">0</Param>
    <Param name="iAttHoldTime">10</Param>
    <Param name="eDemodulation">MOD_AM</Param>
    <Param name="iBfoFrequency">1000</Param>
    <Param name="eAfBandwidth">BW_25</Param>
    <Param name="iAfThreshold">0</Param>
    <Param name="bUseAfThreshold">false</Param>
    <Param name="iPassbandFrequency">0</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SearchRange",
        "param": [
          {
            "name": "iSearchRangeId",
            "#text": "73"
          },
          {
            "name": "iFreqBegin",
            "#text": "87500000"
          },
          {
            "name": "iFreqEnd",
            "#text": "107000000"
          },
          {
            "name": "iFreqStep",
            "#text": "100000"
          },
          {
            "name": "eSpan",
            "#text": "IFPAN_FREQ_RANGE_80000"
          },
          {
            "name": "eDfPanStep",
            "#text": "DFPAN_STEP_100KHZ"
          },
          {
            "name": "eAvgMode",
            "#text": "DFSQU_OFF"
          },
          {
            "name": "eBlockAveragingSelect",
            "#text": "BLOCK_AVERAGING_SELECT_TIME"
          },
          {
            "name": "iBlockAveragingCycles",
            "#text": "200"
          },
          {
            "name": "iBlockAveragingTime",
            "#text": "100"
          },
          {
            "name": "iThreshold",
            "#text": "10"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "eAntPreAmp",
            "#text": "STATE_OFF"
          },
          {
            "name": "eDfAlt",
            "#text": "DFALT_AUTO"
          },
          {
            "name": "eWindowType",
            "#text": "DF_WINDOW_TYPE_BLACKMAN_HARRIS"
          },
          {
            "name": "eDfPanSelectivity",
            "#text": "DFPAN_SELECTIVITY_AUTO"
          },
          {
            "name": "eAttSelect",
            "#text": "ATT_MANUAL"
          },
          {
            "name": "iAttValue",
            "#text": "0"
          },
          {
            "name": "iAttHoldTime",
            "#text": "10"
          },
          {
            "name": "eDemodulation",
            "#text": "MOD_AM"
          },
          {
            "name": "iBfoFrequency",
            "#text": "1000"
          },
          {
            "name": "eAfBandwidth",
            "#text": "BW_25"
          },
          {
            "name": "iAfThreshold",
            "#text": "0"
          },
          {
            "name": "bUseAfThreshold",
            "#text": "false"
          },
          {
            "name": "iPassbandFrequency",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="SearchRange">
    <Param name="iSearchRangeId">73</Param>
    <Param name="iNumHops">10</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SearchRange",
        "param": [
          {
            "name": "iSearchRangeId",
            "#text": "73"
          },
          {
            "name": "iNumHops",
            "#text": "10"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.9.4 SearchRangeAdd  <a id="SearchRangeAdd"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | int64 | iFreqBegin | Start frequency [Hz]. |
| id | int64 | iFreqEnd | Stop frequency [Hz]. |
| id | int64 | iFreqStep | Step frequency [Hz]. |
| id | eSPAN | eSpan | Realtime bandwidth (frequency span). |
| id | eDFPAN_STEP | eDfPanStep | DF channel spacing. |
| auto | eAVERAGE_MODE | eAvgMode | Averaging mode.
                        (default value: 
                        DFSQU_OFF) |
| auto | eBLOCK_AVERAGING_SELECT | eBlockAveragingSelect | Select number of averaging cycles or time duration for block averaging.
                        (default value: 
                        BLOCK_AVERAGING_SELECT_TIME) |
| auto | int32 | iBlockAveragingCycles | Number of averaging cycles (if selected).
                        (default value: 
                        200) |
| auto | int32 | iBlockAveragingTime | Block averaging time [ms] (if selected).
                        (default value: 
                        100) |
| auto | int32 | iThreshold | Level threshold [dBµV].
                        (default value: 
                        10) |
| auto | eANT_POL | eAntPol | Antenna polarization.
                        (default value: 
                        POL_VERTICAL) |
| auto | eSTATE | eAntPreAmp | Antenna preamplifier on or off.
                        (default value: 
                        STATE_OFF) |
| auto | eDF_ALT | eDfAlt | DF evaluation principle (DF alternative).
                        (default value: 
                        DFALT_AUTO) |
| auto | eWINDOW_TYPE | eWindowType | FFT windowtype (function).
                        (default value: 
                        DF_WINDOW_TYPE_BLACKMAN_HARRIS) |
| auto | eDFPAN_SELECTIVITY | eDfPanSelectivity | DF panorama selectivity.
                        (default value: 
                        DFPAN_SELECTIVITY_AUTO) |
| auto | eATT_SELECT | eAttSelect | Attenuation setting manual or automatic.
                        (default value: 
                        ATT_MANUAL) |
| auto | int32 | iAttValue | Attenuation [dB] (range: 0 to 40) (if manual).
                        (default value: 
                        0) |
| auto | int32 | iAttHoldTime | Time duration [1/10 s] (range: 0 to 100: 0 s to 10 s) attenuation value stays unchanged
                        if input level drops.
                        (default value: 
                        10) |
| auto | eDEMODULATION | eDemodulation | Demodulation mode.
                        (default value: 
                        MOD_AM) |
| auto | int64 | iBfoFrequency | BFO frequency [Hz] (only for demodulation mode CW).
                        (default value: 
                        1000) |
| auto | eAF_BANDWIDTH | eAfBandwidth | Demodulation bandwidth.
                        (default value: 
                        BW_25) |
| auto | int32 | iAfThreshold | Demodulation threshold [dBµV].
                        (default value: 
                        0) |
| auto | boolean | bUseAfThreshold | Level threshold for demodulation active (true: active, false: not active, i.e. all
                        channels regardless of their level will be demodulated.
                        (default value: 
                        false) |
| auto | int64 | iPassbandFrequency | SSB passband frequency [Hz] (only for demodulation mode USB or LSB).
                        (default value: 
                        0) |
| out | int32 | iSearchRangeId | ID of search range created. |
| out | int32 | iNumHops | Number of hops. |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="SearchRangeAdd">
    <Param name="iFreqBegin">87500000</Param>
    <Param name="iFreqEnd">107000000</Param>
    <Param name="iFreqStep">100000</Param>
    <Param name="eSpan">IFPAN_FREQ_RANGE_80000</Param>
    <Param name="eDfPanStep">DFPAN_STEP_100KHZ</Param>
    <Param name="eAvgMode">DFSQU_OFF</Param>
    <Param name="eBlockAveragingSelect">BLOCK_AVERAGING_SELECT_TIME</Param>
    <Param name="iBlockAveragingCycles">200</Param>
    <Param name="iBlockAveragingTime">100</Param>
    <Param name="iThreshold">10</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="eAntPreAmp">STATE_OFF</Param>
    <Param name="eDfAlt">DFALT_AUTO</Param>
    <Param name="eWindowType">DF_WINDOW_TYPE_BLACKMAN_HARRIS</Param>
    <Param name="eDfPanSelectivity">DFPAN_SELECTIVITY_AUTO</Param>
    <Param name="eAttSelect">ATT_MANUAL</Param>
    <Param name="iAttValue">0</Param>
    <Param name="iAttHoldTime">10</Param>
    <Param name="eDemodulation">MOD_AM</Param>
    <Param name="iBfoFrequency">1000</Param>
    <Param name="eAfBandwidth">BW_25</Param>
    <Param name="iAfThreshold">0</Param>
    <Param name="bUseAfThreshold">false</Param>
    <Param name="iPassbandFrequency">0</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SearchRangeAdd",
        "param": [
          {
            "name": "iFreqBegin",
            "#text": "87500000"
          },
          {
            "name": "iFreqEnd",
            "#text": "107000000"
          },
          {
            "name": "iFreqStep",
            "#text": "100000"
          },
          {
            "name": "eSpan",
            "#text": "IFPAN_FREQ_RANGE_80000"
          },
          {
            "name": "eDfPanStep",
            "#text": "DFPAN_STEP_100KHZ"
          },
          {
            "name": "eAvgMode",
            "#text": "DFSQU_OFF"
          },
          {
            "name": "eBlockAveragingSelect",
            "#text": "BLOCK_AVERAGING_SELECT_TIME"
          },
          {
            "name": "iBlockAveragingCycles",
            "#text": "200"
          },
          {
            "name": "iBlockAveragingTime",
            "#text": "100"
          },
          {
            "name": "iThreshold",
            "#text": "10"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "eAntPreAmp",
            "#text": "STATE_OFF"
          },
          {
            "name": "eDfAlt",
            "#text": "DFALT_AUTO"
          },
          {
            "name": "eWindowType",
            "#text": "DF_WINDOW_TYPE_BLACKMAN_HARRIS"
          },
          {
            "name": "eDfPanSelectivity",
            "#text": "DFPAN_SELECTIVITY_AUTO"
          },
          {
            "name": "eAttSelect",
            "#text": "ATT_MANUAL"
          },
          {
            "name": "iAttValue",
            "#text": "0"
          },
          {
            "name": "iAttHoldTime",
            "#text": "10"
          },
          {
            "name": "eDemodulation",
            "#text": "MOD_AM"
          },
          {
            "name": "iBfoFrequency",
            "#text": "1000"
          },
          {
            "name": "eAfBandwidth",
            "#text": "BW_25"
          },
          {
            "name": "iAfThreshold",
            "#text": "0"
          },
          {
            "name": "bUseAfThreshold",
            "#text": "false"
          },
          {
            "name": "iPassbandFrequency",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="SearchRangeAdd">
    <Param name="iFreqBegin">87500000</Param>
    <Param name="iFreqEnd">107000000</Param>
    <Param name="iFreqStep">100000</Param>
    <Param name="eSpan">IFPAN_FREQ_RANGE_80000</Param>
    <Param name="eDfPanStep">DFPAN_STEP_100KHZ</Param>
    <Param name="eAvgMode">DFSQU_OFF</Param>
    <Param name="eBlockAveragingSelect">BLOCK_AVERAGING_SELECT_TIME</Param>
    <Param name="iBlockAveragingCycles">200</Param>
    <Param name="iBlockAveragingTime">100</Param>
    <Param name="iThreshold">10</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="eAntPreAmp">STATE_OFF</Param>
    <Param name="eDfAlt">DFALT_AUTO</Param>
    <Param name="eWindowType">DF_WINDOW_TYPE_BLACKMAN_HARRIS</Param>
    <Param name="eDfPanSelectivity">DFPAN_SELECTIVITY_AUTO</Param>
    <Param name="eAttSelect">ATT_MANUAL</Param>
    <Param name="iAttValue">0</Param>
    <Param name="iAttHoldTime">10</Param>
    <Param name="eDemodulation">MOD_AM</Param>
    <Param name="iBfoFrequency">1000</Param>
    <Param name="eAfBandwidth">BW_25</Param>
    <Param name="iAfThreshold">0</Param>
    <Param name="bUseAfThreshold">false</Param>
    <Param name="iPassbandFrequency">0</Param>
    <Param name="iSearchRangeId">73</Param>
    <Param name="iNumHops">10</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SearchRangeAdd",
        "param": [
          {
            "name": "iFreqBegin",
            "#text": "87500000"
          },
          {
            "name": "iFreqEnd",
            "#text": "107000000"
          },
          {
            "name": "iFreqStep",
            "#text": "100000"
          },
          {
            "name": "eSpan",
            "#text": "IFPAN_FREQ_RANGE_80000"
          },
          {
            "name": "eDfPanStep",
            "#text": "DFPAN_STEP_100KHZ"
          },
          {
            "name": "eAvgMode",
            "#text": "DFSQU_OFF"
          },
          {
            "name": "eBlockAveragingSelect",
            "#text": "BLOCK_AVERAGING_SELECT_TIME"
          },
          {
            "name": "iBlockAveragingCycles",
            "#text": "200"
          },
          {
            "name": "iBlockAveragingTime",
            "#text": "100"
          },
          {
            "name": "iThreshold",
            "#text": "10"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "eAntPreAmp",
            "#text": "STATE_OFF"
          },
          {
            "name": "eDfAlt",
            "#text": "DFALT_AUTO"
          },
          {
            "name": "eWindowType",
            "#text": "DF_WINDOW_TYPE_BLACKMAN_HARRIS"
          },
          {
            "name": "eDfPanSelectivity",
            "#text": "DFPAN_SELECTIVITY_AUTO"
          },
          {
            "name": "eAttSelect",
            "#text": "ATT_MANUAL"
          },
          {
            "name": "iAttValue",
            "#text": "0"
          },
          {
            "name": "iAttHoldTime",
            "#text": "10"
          },
          {
            "name": "eDemodulation",
            "#text": "MOD_AM"
          },
          {
            "name": "iBfoFrequency",
            "#text": "1000"
          },
          {
            "name": "eAfBandwidth",
            "#text": "BW_25"
          },
          {
            "name": "iAfThreshold",
            "#text": "0"
          },
          {
            "name": "bUseAfThreshold",
            "#text": "false"
          },
          {
            "name": "iPassbandFrequency",
            "#text": "0"
          },
          {
            "name": "iSearchRangeId",
            "#text": "73"
          },
          {
            "name": "iNumHops",
            "#text": "10"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.9.5 SearchRangeDelete  <a id="SearchRangeDelete"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | int32 | iSearchRangeId | ID of search range to address (delete). |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="SearchRangeDelete">
    <Param name="iSearchRangeId">23</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SearchRangeDelete",
        "param": {
          "name": "iSearchRangeId",
          "#text": "23"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="SearchRangeDelete">
    <Param name="iSearchRangeId">23</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SearchRangeDelete",
        "param": {
          "name": "iSearchRangeId",
          "#text": "23"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.9.6 SearchRangeDeleteAll  <a id="SearchRangeDeleteAll"></a>

_No members (command takes no parameters)._

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="SearchRangeDeleteAll">
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SearchRangeDeleteAll"
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="SearchRangeDeleteAll">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SearchRangeDeleteAll"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.9.7 SearchRangeNext  <a id="SearchRangeNext"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | int32 | iSearchRangeId | ID of search range to address (search range to start out from; -1: 1st search range). |
| out | int32 | iNextSearchRangeId | ID of next search range (from addressed search range; -1: no next search range existing,
                        ignore all subsequent indications). |
| out | int64 | iFreqBegin | Start frequency [Hz]. |
| out | int64 | iFreqEnd | Stop frequency [Hz]. |
| out | int64 | iFreqStep | Step frequency [Hz]. |
| out | eSPAN | eSpan | Realtime bandwidth (frequency span). |
| out | eDFPAN_STEP | eDfPanStep | DF channel spacing. |
| out | eAVERAGE_MODE | eAvgMode | Averaging mode. |
| out | eBLOCK_AVERAGING_SELECT | eBlockAveragingSelect | Select number of averaging cycles or time duration for block averaging. |
| out | int32 | iBlockAveragingCycles | Number of averaging cycles (if selected). |
| out | int32 | iBlockAveragingTime | Block averaging time [ms] (if selected). |
| out | int32 | iThreshold | Level threshold [dBµV]. |
| out | eANT_POL | eAntPol | Antenna polarization. |
| out | eSTATE | eAntPreAmp | Antenna preamplifier on or off. |
| out | eDF_ALT | eDfAlt | DF evaluation principle (DF alternative). |
| out | eWINDOW_TYPE | eWindowType | FFT window type (function). |
| out | eDFPAN_SELECTIVITY | eDfPanSelectivity | DF panorama selectivity. |
| out | eATT_SELECT | eAttSelect | Attenuation setting manual or automatic. |
| out | int32 | iAttValue | Attenuation [dB] (range 0 to 40) (if manual). |
| out | int32 | iAttHoldTime | Time duration [1/10 s] (range 0 to 100: 0 s to 10 s) attenuation value stays unchanged
                        if input level drops. |
| out | eDEMODULATION | eDemodulation | Demodulation mode. |
| out | int64 | iBfoFrequency | BFO frequency [Hz] (only for demodulation mode CW). |
| out | eAF_BANDWIDTH | eAfBandwidth | Demodulation bandwidth. |
| out | int32 | iAfThreshold | Demodulation threshold [dBµV]. |
| out | boolean | bUseAfThreshold | Level threshold for demodulation active (true: active, false: not active, i.e. all
                        channels regardless of their level will be demodulated. |
| out | int64 | iPassbandFrequency | SSB passband frequency [Hz] (only for demodulation mode USB or LSB). |
| out | int32 | iNumHops | Number of hops. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="SearchRangeNext">
    <Param name="iSearchRangeId">73</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SearchRangeNext",
        "param": {
          "name": "iSearchRangeId",
          "#text": "73"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="SearchRangeNext">
    <Param name="iSearchRangeId">73</Param>
    <Param name="iNextSearchRangeId">37</Param>
    <Param name="iFreqBegin">87500000</Param>
    <Param name="iFreqEnd">107000000</Param>
    <Param name="iFreqStep">100000</Param>
    <Param name="eSpan">IFPAN_FREQ_RANGE_80000</Param>
    <Param name="eDfPanStep">DFPAN_STEP_100KHZ</Param>
    <Param name="eAvgMode">DFSQU_OFF</Param>
    <Param name="eBlockAveragingSelect">BLOCK_AVERAGING_SELECT_TIME</Param>
    <Param name="iBlockAveragingCycles">200</Param>
    <Param name="iBlockAveragingTime">100</Param>
    <Param name="iThreshold">10</Param>
    <Param name="eAntPol">POL_VERTICAL</Param>
    <Param name="eAntPreAmp">STATE_OFF</Param>
    <Param name="eDfAlt">DFALT_AUTO</Param>
    <Param name="eWindowType">DF_WINDOW_TYPE_BLACKMAN_HARRIS</Param>
    <Param name="eDfPanSelectivity">DFPAN_SELECTIVITY_AUTO</Param>
    <Param name="eAttSelect">ATT_MANUAL</Param>
    <Param name="iAttValue">0</Param>
    <Param name="iAttHoldTime">10</Param>
    <Param name="eDemodulation">MOD_AM</Param>
    <Param name="iBfoFrequency">1000</Param>
    <Param name="eAfBandwidth">BW_25</Param>
    <Param name="iAfThreshold">0</Param>
    <Param name="bUseAfThreshold">false</Param>
    <Param name="iPassbandFrequency">0</Param>
    <Param name="iNumHops">10</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SearchRangeNext",
        "param": [
          {
            "name": "iSearchRangeId",
            "#text": "73"
          },
          {
            "name": "iNextSearchRangeId",
            "#text": "37"
          },
          {
            "name": "iFreqBegin",
            "#text": "87500000"
          },
          {
            "name": "iFreqEnd",
            "#text": "107000000"
          },
          {
            "name": "iFreqStep",
            "#text": "100000"
          },
          {
            "name": "eSpan",
            "#text": "IFPAN_FREQ_RANGE_80000"
          },
          {
            "name": "eDfPanStep",
            "#text": "DFPAN_STEP_100KHZ"
          },
          {
            "name": "eAvgMode",
            "#text": "DFSQU_OFF"
          },
          {
            "name": "eBlockAveragingSelect",
            "#text": "BLOCK_AVERAGING_SELECT_TIME"
          },
          {
            "name": "iBlockAveragingCycles",
            "#text": "200"
          },
          {
            "name": "iBlockAveragingTime",
            "#text": "100"
          },
          {
            "name": "iThreshold",
            "#text": "10"
          },
          {
            "name": "eAntPol",
            "#text": "POL_VERTICAL"
          },
          {
            "name": "eAntPreAmp",
            "#text": "STATE_OFF"
          },
          {
            "name": "eDfAlt",
            "#text": "DFALT_AUTO"
          },
          {
            "name": "eWindowType",
            "#text": "DF_WINDOW_TYPE_BLACKMAN_HARRIS"
          },
          {
            "name": "eDfPanSelectivity",
            "#text": "DFPAN_SELECTIVITY_AUTO"
          },
          {
            "name": "eAttSelect",
            "#text": "ATT_MANUAL"
          },
          {
            "name": "iAttValue",
            "#text": "0"
          },
          {
            "name": "iAttHoldTime",
            "#text": "10"
          },
          {
            "name": "eDemodulation",
            "#text": "MOD_AM"
          },
          {
            "name": "iBfoFrequency",
            "#text": "1000"
          },
          {
            "name": "eAfBandwidth",
            "#text": "BW_25"
          },
          {
            "name": "iAfThreshold",
            "#text": "0"
          },
          {
            "name": "bUseAfThreshold",
            "#text": "false"
          },
          {
            "name": "iPassbandFrequency",
            "#text": "0"
          },
          {
            "name": "iNumHops",
            "#text": "10"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.9.8 SearchTimes  <a id="SearchTimes"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | int32 | iMeasTime | Measuring time [ms]: time to stay on a frequency until to decide whether to enter
                        temporary FFM or to go to next frequency. |
| i/o | int64 | iSignalTime | Minimum signal time [µs]: minimum signal duration to switch to temporary FFM. |
| i/o | int32 | iDwellTime | Dwell time [ms]: time the temporary FFM is to be held. (0: hold FFM until command
                        Continue received). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="SearchTimes">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SearchTimes"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="SearchTimes">
    <Param name="iMeasTime">10</Param>
    <Param name="iSignalTime">1000</Param>
    <Param name="iDwellTime">5000</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "SearchTimes",
        "param": [
          {
            "name": "iMeasTime",
            "#text": "10"
          },
          {
            "name": "iSignalTime",
            "#text": "1000"
          },
          {
            "name": "iDwellTime",
            "#text": "5000"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="SearchTimes">
    <Param name="iMeasTime">10</Param>
    <Param name="iSignalTime">1000</Param>
    <Param name="iDwellTime">5000</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SearchTimes",
        "param": [
          {
            "name": "iMeasTime",
            "#text": "10"
          },
          {
            "name": "iSignalTime",
            "#text": "1000"
          },
          {
            "name": "iDwellTime",
            "#text": "5000"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="SearchTimes">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "SearchTimes"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

## 1.10 System-Commands

### 1.10.1 AudioMode  <a id="AudioMode"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eAUDIO_MODE | eAudioMode | Audio mode (data format). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="AudioMode">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AudioMode"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="AudioMode">
    <Param name="eAudioMode">AUDIO_MODE_32KHZ_16BIT_STEREO</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AudioMode",
        "param": {
          "name": "eAudioMode",
          "#text": "AUDIO_MODE_32KHZ_16BIT_STEREO"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="AudioMode">
    <Param name="eAudioMode">AUDIO_MODE_32KHZ_16BIT_STEREO</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AudioMode",
        "param": {
          "name": "eAudioMode",
          "#text": "AUDIO_MODE_32KHZ_16BIT_STEREO"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="AudioMode">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AudioMode"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.2 AuxControl  <a id="AuxControl"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eAUX_CTRL_MODE | eControlPortMode | Mode of AUX control port. |
| i/o | int32 | iControlPortOutput | Output to port (range 0 to 255) if mode is manual. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="AuxControl">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AuxControl"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="AuxControl">
    <Param name="eControlPortMode">AUX_CTRL_MODE_ANTENNA</Param>
    <Param name="iControlPortOutput">7</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "AuxControl",
        "param": [
          {
            "name": "eControlPortMode",
            "#text": "AUX_CTRL_MODE_ANTENNA"
          },
          {
            "name": "iControlPortOutput",
            "#text": "7"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="AuxControl">
    <Param name="eControlPortMode">AUX_CTRL_MODE_ANTENNA</Param>
    <Param name="iControlPortOutput">7</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AuxControl",
        "param": [
          {
            "name": "eControlPortMode",
            "#text": "AUX_CTRL_MODE_ANTENNA"
          },
          {
            "name": "iControlPortOutput",
            "#text": "7"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="AuxControl">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "AuxControl"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.3 BlankingSettings  <a id="BlankingSettings"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eBLANKING_INPUT | eBlankingInput | Input of BLANKING signal: connector AUX, pin 9 (BLANKING), or TRIGGER. |
| i/o | eBLANKING_MODE | eBlankingMode | Blanking mode: suppress antenna inputs during active BLANKING signal, just indicate
                        or ignore signal. |
| i/o | eBLANKING_POLARITY | eBlankingPolarity | Blanking polarity: active low or high. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="BlankingSettings">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "BlankingSettings"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="BlankingSettings">
    <Param name="eBlankingInput">BLANKING_INPUT_AUX</Param>
    <Param name="eBlankingMode">BLANKING_MODE_SUSPEND</Param>
    <Param name="eBlankingPolarity">BLANKING_POLARITY_LOW</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "BlankingSettings",
        "param": [
          {
            "name": "eBlankingInput",
            "#text": "BLANKING_INPUT_AUX"
          },
          {
            "name": "eBlankingMode",
            "#text": "BLANKING_MODE_SUSPEND"
          },
          {
            "name": "eBlankingPolarity",
            "#text": "BLANKING_POLARITY_LOW"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="BlankingSettings">
    <Param name="eBlankingInput">BLANKING_INPUT_AUX</Param>
    <Param name="eBlankingMode">BLANKING_MODE_SUSPEND</Param>
    <Param name="eBlankingPolarity">BLANKING_POLARITY_LOW</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "BlankingSettings",
        "param": [
          {
            "name": "eBlankingInput",
            "#text": "BLANKING_INPUT_AUX"
          },
          {
            "name": "eBlankingMode",
            "#text": "BLANKING_MODE_SUSPEND"
          },
          {
            "name": "eBlankingPolarity",
            "#text": "BLANKING_POLARITY_LOW"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="BlankingSettings">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "BlankingSettings"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.4 ClockSettings  <a id="ClockSettings"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eCLOCK_START | eClockStart | Clock start mode: how clock will be started once it was set. |
| out | eCLOCK_ORIGIN | eClockOrigin | Origin of timing data clock has been set to on latest setting. |
| out | string | zClockLastSet | Timestamp (100 chars max.) of latest clock setting. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="ClockSettings">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ClockSettings"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="ClockSettings">
    <Param name="eClockStart">CLOCK_START_AUTO</Param>
    <Param name="eClockOrigin">CLOCK_ORIGIN_MANUAL</Param>
    <Param name="zClockLastSet">2012-12-18 14:50:36</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ClockSettings",
        "param": [
          {
            "name": "eClockStart",
            "#text": "CLOCK_START_AUTO"
          },
          {
            "name": "eClockOrigin",
            "#text": "CLOCK_ORIGIN_MANUAL"
          },
          {
            "name": "zClockLastSet",
            "#text": "2012-12-18 14:50:36"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="ClockSettings">
    <Param name="eClockStart">CLOCK_START_AUTO</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ClockSettings",
        "param": {
          "name": "eClockStart",
          "#text": "CLOCK_START_AUTO"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="ClockSettings">
    <Param name="eClockOrigin">CLOCK_ORIGIN_MANUAL</Param>
    <Param name="zClockLastSet">2012-12-18 14:50:36</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ClockSettings",
        "param": [
          {
            "name": "eClockOrigin",
            "#text": "CLOCK_ORIGIN_MANUAL"
          },
          {
            "name": "zClockLastSet",
            "#text": "2012-12-18 14:50:36"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.5 CorrectionSet  <a id="CorrectionSet"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | int32 | iSet | Number of DF correction user data set in use/to be used. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="CorrectionSet">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CorrectionSet"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="CorrectionSet">
    <Param name="iSet">1</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CorrectionSet",
        "param": {
          "name": "iSet",
          "#text": "1"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="CorrectionSet">
    <Param name="iSet">1</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CorrectionSet",
        "param": {
          "name": "iSet",
          "#text": "1"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="CorrectionSet">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CorrectionSet"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.6 CorrectionState  <a id="CorrectionState"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | boolean | bCorrectAntFactor | Antenna factor correction (true: enabled, false: disabled). |
| i/o | boolean | bCorrectOmniPhase | Omniphase correction (ditto). |
| i/o | boolean | bCorrectAttenuation | Attenuation correction (ditto). |
| i/o | boolean | bCorrectAzimuth | Azimuth correction (ditto). |
| i/o | boolean | bCorrectAttitude | Attitude correction (true: consider nautical angles, false: do not consider). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="CorrectionState">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CorrectionState"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="CorrectionState">
    <Param name="bCorrectAntFactor">true</Param>
    <Param name="bCorrectOmniPhase">true</Param>
    <Param name="bCorrectAttenuation">true</Param>
    <Param name="bCorrectAzimuth">true</Param>
    <Param name="bCorrectAttitude">true</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "CorrectionState",
        "param": [
          {
            "name": "bCorrectAntFactor",
            "#text": "true"
          },
          {
            "name": "bCorrectOmniPhase",
            "#text": "true"
          },
          {
            "name": "bCorrectAttenuation",
            "#text": "true"
          },
          {
            "name": "bCorrectAzimuth",
            "#text": "true"
          },
          {
            "name": "bCorrectAttitude",
            "#text": "true"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="CorrectionState">
    <Param name="bCorrectAntFactor">true</Param>
    <Param name="bCorrectOmniPhase">true</Param>
    <Param name="bCorrectAttenuation">true</Param>
    <Param name="bCorrectAzimuth">true</Param>
    <Param name="bCorrectAttitude">true</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CorrectionState",
        "param": [
          {
            "name": "bCorrectAntFactor",
            "#text": "true"
          },
          {
            "name": "bCorrectOmniPhase",
            "#text": "true"
          },
          {
            "name": "bCorrectAttenuation",
            "#text": "true"
          },
          {
            "name": "bCorrectAzimuth",
            "#text": "true"
          },
          {
            "name": "bCorrectAttitude",
            "#text": "true"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="CorrectionState">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "CorrectionState"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.7 DateAndTime  <a id="DateAndTime"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | int32 | iYear | Year (4 digits). |
| i/o | int32 | iMonth | Month (range 1 to 12). |
| i/o | int32 | iDay | Day (range 1 to 31). |
| i/o | int32 | iHour | Hour (range 0 to 23). |
| i/o | int32 | iMinute | Minute (range 0 to 59). |
| i/o | int32 | iSecond | Second (range 0 to 59). |
| i/o | int32 | iTimezoneHours | Timezone offset vs. UTC: hours (range -12 to +12; positive: east, negative: west of
                        UTC). |
| i/o | int32 | iTimezoneMinutes | ditto: minutes (range -59 to +59). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="DateAndTime">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "DateAndTime"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="DateAndTime">
    <Param name="iYear">2013</Param>
    <Param name="iMonth">12</Param>
    <Param name="iDay">19</Param>
    <Param name="iHour">14</Param>
    <Param name="iMinute">17</Param>
    <Param name="iSecond">0</Param>
    <Param name="iTimezoneHours">1</Param>
    <Param name="iTimezoneMinutes">0</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "DateAndTime",
        "param": [
          {
            "name": "iYear",
            "#text": "2013"
          },
          {
            "name": "iMonth",
            "#text": "12"
          },
          {
            "name": "iDay",
            "#text": "19"
          },
          {
            "name": "iHour",
            "#text": "14"
          },
          {
            "name": "iMinute",
            "#text": "17"
          },
          {
            "name": "iSecond",
            "#text": "0"
          },
          {
            "name": "iTimezoneHours",
            "#text": "1"
          },
          {
            "name": "iTimezoneMinutes",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="DateAndTime">
    <Param name="iYear">2013</Param>
    <Param name="iMonth">12</Param>
    <Param name="iDay">19</Param>
    <Param name="iHour">14</Param>
    <Param name="iMinute">17</Param>
    <Param name="iSecond">0</Param>
    <Param name="iTimezoneHours">1</Param>
    <Param name="iTimezoneMinutes">0</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "DateAndTime",
        "param": [
          {
            "name": "iYear",
            "#text": "2013"
          },
          {
            "name": "iMonth",
            "#text": "12"
          },
          {
            "name": "iDay",
            "#text": "19"
          },
          {
            "name": "iHour",
            "#text": "14"
          },
          {
            "name": "iMinute",
            "#text": "17"
          },
          {
            "name": "iSecond",
            "#text": "0"
          },
          {
            "name": "iTimezoneHours",
            "#text": "1"
          },
          {
            "name": "iTimezoneMinutes",
            "#text": "0"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="DateAndTime">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "DateAndTime"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.8 Declination  <a id="Declination"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eDECLINATION_SOURCE | eDeclinationSource | Source of indication for declination                  (deviation of magnetic north
                        vs. true [geographic] north; m. N. to the right of g. N.). |
| i/o | int32 | iManualDeclination | Declination value [1/10 °] (range 0 to 3599: 0° to 359.9°) to be used if source is
                        manual. |
| out | int32 | iDeclinationValue | Declination value [1/10 °] (range 0 to 3599: 0° to 359.9°) currently in use. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="Declination">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "Declination"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="Declination">
    <Param name="eDeclinationSource">DECL_SOUR_GPS</Param>
    <Param name="iManualDeclination">125</Param>
    <Param name="iDeclinationValue">40</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "Declination",
        "param": [
          {
            "name": "eDeclinationSource",
            "#text": "DECL_SOUR_GPS"
          },
          {
            "name": "iManualDeclination",
            "#text": "125"
          },
          {
            "name": "iDeclinationValue",
            "#text": "40"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="Declination">
    <Param name="eDeclinationSource">DECL_SOUR_GPS</Param>
    <Param name="iManualDeclination">125</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Declination",
        "param": [
          {
            "name": "eDeclinationSource",
            "#text": "DECL_SOUR_GPS"
          },
          {
            "name": "iManualDeclination",
            "#text": "125"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="Declination">
    <Param name="iDeclinationValue">40</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Declination",
        "param": {
          "name": "iDeclinationValue",
          "#text": "40"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.9 DeviceLock  <a id="DeviceLock"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eSTATE | eDeviceLock | Lock device/device is locked (on: lock[ed], off: unlock[ed]). |
| i/o | string | zDeviceLockLabel | Arbitrary lock message (50 chars max.); can only be set by owner of lock. |
| out | string | zIPaddress | IP address (48 chars max.) of client owning the lock; "NONE" if unlocked. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="DeviceLock">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "DeviceLock"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="DeviceLock">
    <Param name="eDeviceLock">STATE_ON</Param>
    <Param name="zDeviceLockLabel">Currently locked for 1 hour</Param>
    <Param name="zIPaddress">192.168.50.10</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "DeviceLock",
        "param": [
          {
            "name": "eDeviceLock",
            "#text": "STATE_ON"
          },
          {
            "name": "zDeviceLockLabel",
            "#text": "Currently locked for 1 hour"
          },
          {
            "name": "zIPaddress",
            "#text": "192.168.50.10"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="DeviceLock">
    <Param name="eDeviceLock">STATE_ON</Param>
    <Param name="zDeviceLockLabel">Currently locked for 1 hour</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "DeviceLock",
        "param": [
          {
            "name": "eDeviceLock",
            "#text": "STATE_ON"
          },
          {
            "name": "zDeviceLockLabel",
            "#text": "Currently locked for 1 hour"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="DeviceLock">
    <Param name="zIPaddress">192.168.50.10</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "DeviceLock",
        "param": {
          "name": "zIPaddress",
          "#text": "192.168.50.10"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.10 IfMode  <a id="IfMode"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eIF_MODE | eIfMode | Type of IF data trace (off, IF or AMMOS IF) and data format (16 bit or 32 bit per
                        data value). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="IfMode">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "IfMode"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="IfMode">
    <Param name="eIfMode">IF_OFF</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "IfMode",
        "param": {
          "name": "eIfMode",
          "#text": "IF_OFF"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="IfMode">
    <Param name="eIfMode">IF_OFF</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "IfMode",
        "param": {
          "name": "eIfMode",
          "#text": "IF_OFF"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="IfMode">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "IfMode"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.11 LanSettings  <a id="LanSettings"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | string | zActIP | Current IP address (24 chars max.). |
| i/o | int32 | iActPort | Current default port. |
| i/o | string | zActSubnetmask | Current subnet mask (24 chars max.). |
| i/o | string | zActGateway | Current gateway (24 chars max.). |
| i/o | string | zPerIP | Static IP address (24 chars max.). |
| i/o | int32 | iPerPort | Static default port. |
| i/o | string | zPerSubnetmask | Static subnet mask (24 chars max.). |
| i/o | string | zPerGateway | Static gateway (24 chars max.). |
| i/o | int32 | iMTU | Device MTU size [einheit]. |
| i/o | boolean | bUseDHCP | Use DHCP or fixed (static) IP address (true: DHCP, false: static). |
| out | string | zMacAddress | Hardware specific Ethernet address (MAC) (24 chars max.). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="LanSettings">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "LanSettings"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="LanSettings">
    <Param name="zActIP">10.11.12.13</Param>
    <Param name="iActPort">5555</Param>
    <Param name="zActSubnetmask">255.255.255.000</Param>
    <Param name="zActGateway">10.11.12.13</Param>
    <Param name="zPerIP">10.11.12.14</Param>
    <Param name="iPerPort">5555</Param>
    <Param name="zPerSubnetmask">255.255.255.000</Param>
    <Param name="zPerGateway">10.11.12.13</Param>
    <Param name="iMTU">1500</Param>
    <Param name="bUseDHCP">true</Param>
    <Param name="zMacAddress">00-90-B8-10-01-15</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "LanSettings",
        "param": [
          {
            "name": "zActIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iActPort",
            "#text": "5555"
          },
          {
            "name": "zActSubnetmask",
            "#text": "255.255.255.000"
          },
          {
            "name": "zActGateway",
            "#text": "10.11.12.13"
          },
          {
            "name": "zPerIP",
            "#text": "10.11.12.14"
          },
          {
            "name": "iPerPort",
            "#text": "5555"
          },
          {
            "name": "zPerSubnetmask",
            "#text": "255.255.255.000"
          },
          {
            "name": "zPerGateway",
            "#text": "10.11.12.13"
          },
          {
            "name": "iMTU",
            "#text": "1500"
          },
          {
            "name": "bUseDHCP",
            "#text": "true"
          },
          {
            "name": "zMacAddress",
            "#text": "00-90-B8-10-01-15"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="LanSettings">
    <Param name="zActIP">10.11.12.13</Param>
    <Param name="iActPort">5555</Param>
    <Param name="zActSubnetmask">255.255.255.000</Param>
    <Param name="zActGateway">10.11.12.13</Param>
    <Param name="zPerIP">10.11.12.14</Param>
    <Param name="iPerPort">5555</Param>
    <Param name="zPerSubnetmask">255.255.255.000</Param>
    <Param name="zPerGateway">10.11.12.13</Param>
    <Param name="iMTU">1500</Param>
    <Param name="bUseDHCP">true</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "LanSettings",
        "param": [
          {
            "name": "zActIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iActPort",
            "#text": "5555"
          },
          {
            "name": "zActSubnetmask",
            "#text": "255.255.255.000"
          },
          {
            "name": "zActGateway",
            "#text": "10.11.12.13"
          },
          {
            "name": "zPerIP",
            "#text": "10.11.12.14"
          },
          {
            "name": "iPerPort",
            "#text": "5555"
          },
          {
            "name": "zPerSubnetmask",
            "#text": "255.255.255.000"
          },
          {
            "name": "zPerGateway",
            "#text": "10.11.12.13"
          },
          {
            "name": "iMTU",
            "#text": "1500"
          },
          {
            "name": "bUseDHCP",
            "#text": "true"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="LanSettings">
    <Param name="zMacAddress">00-90-B8-10-01-15</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "LanSettings",
        "param": {
          "name": "zMacAddress",
          "#text": "00-90-B8-10-01-15"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.12 LocationAndTimeSource  <a id="LocationAndTimeSource"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eLOC_TIME_SOURCE | eLocTimeSource | Source of location and timing data. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="LocationAndTimeSource">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "LocationAndTimeSource"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="LocationAndTimeSource">
    <Param name="eLocTimeSource">LOC_TIME_SRC_GPS</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "LocationAndTimeSource",
        "param": {
          "name": "eLocTimeSource",
          "#text": "LOC_TIME_SRC_GPS"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="LocationAndTimeSource">
    <Param name="eLocTimeSource">LOC_TIME_SRC_GPS</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "LocationAndTimeSource",
        "param": {
          "name": "eLocTimeSource",
          "#text": "LOC_TIME_SRC_GPS"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="LocationAndTimeSource">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "LocationAndTimeSource"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.13 LocationManual  <a id="LocationManual"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eDIRECTION | eLatRef | Latitude reference (north or south). |
| i/o | int32 | eLatDeg | Latitude degrees [°]. |
| i/o | int32 | eLatMin | Latitude minutes [1/1000000 min]. |
| i/o | eDIRECTION | eLonRef | Longitude reference (east or west). |
| i/o | int32 | eLonDeg | Longitude degrees [°]. |
| i/o | int32 | eLonMin | Longitude minutes [1/1000000 min]. |
| i/o | int32 | iAltitude | Altitude [cm] above MSL (Mean Sea Level). |
| i/o | boolean | bGeoSepValid | Geoidal separation (true: valid, false: invalid). |
| i/o | int32 | iGeoSepValue | Geoidal separation [cm]: height difference between "above WGS84 ellipsoid" and "above
                        mean sea level": heightWGS84 = heightMSL + geoidalSeparation. |
| i/o | string | zLocationName | Name of location: user defined string (100 chars max.); arbitrary content only intended
                        for user documentation, not for internal distinguishing of individual locations. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="LocationManual">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "LocationManual"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="LocationManual">
    <Param name="eLatRef">DIRECTION_NORTH</Param>
    <Param name="eLatDeg">48</Param>
    <Param name="eLatMin">7638480</Param>
    <Param name="eLonRef">DIRECTION_EAST</Param>
    <Param name="eLonDeg">11</Param>
    <Param name="eLonMin">36739260</Param>
    <Param name="iAltitude">54060</Param>
    <Param name="bGeoSepValid">true</Param>
    <Param name="iGeoSepValue">4620</Param>
    <Param name="zLocationName">Munich</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "LocationManual",
        "param": [
          {
            "name": "eLatRef",
            "#text": "DIRECTION_NORTH"
          },
          {
            "name": "eLatDeg",
            "#text": "48"
          },
          {
            "name": "eLatMin",
            "#text": "7638480"
          },
          {
            "name": "eLonRef",
            "#text": "DIRECTION_EAST"
          },
          {
            "name": "eLonDeg",
            "#text": "11"
          },
          {
            "name": "eLonMin",
            "#text": "36739260"
          },
          {
            "name": "iAltitude",
            "#text": "54060"
          },
          {
            "name": "bGeoSepValid",
            "#text": "true"
          },
          {
            "name": "iGeoSepValue",
            "#text": "4620"
          },
          {
            "name": "zLocationName",
            "#text": "Munich"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="LocationManual">
    <Param name="eLatRef">DIRECTION_NORTH</Param>
    <Param name="eLatDeg">48</Param>
    <Param name="eLatMin">7638480</Param>
    <Param name="eLonRef">DIRECTION_EAST</Param>
    <Param name="eLonDeg">11</Param>
    <Param name="eLonMin">36739260</Param>
    <Param name="iAltitude">54060</Param>
    <Param name="bGeoSepValid">true</Param>
    <Param name="iGeoSepValue">4620</Param>
    <Param name="zLocationName">Munich</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "LocationManual",
        "param": [
          {
            "name": "eLatRef",
            "#text": "DIRECTION_NORTH"
          },
          {
            "name": "eLatDeg",
            "#text": "48"
          },
          {
            "name": "eLatMin",
            "#text": "7638480"
          },
          {
            "name": "eLonRef",
            "#text": "DIRECTION_EAST"
          },
          {
            "name": "eLonDeg",
            "#text": "11"
          },
          {
            "name": "eLonMin",
            "#text": "36739260"
          },
          {
            "name": "iAltitude",
            "#text": "54060"
          },
          {
            "name": "bGeoSepValid",
            "#text": "true"
          },
          {
            "name": "iGeoSepValue",
            "#text": "4620"
          },
          {
            "name": "zLocationName",
            "#text": "Munich"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="LocationManual">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "LocationManual"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.14 NTP  <a id="NTP"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | string | zServer | IP address (48 chars max.) of NTP server to address. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="NTP">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "NTP"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="NTP">
    <Param name="zServer">192.168.50.10</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "NTP",
        "param": {
          "name": "zServer",
          "#text": "192.168.50.10"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="NTP">
    <Param name="zServer">192.168.50.10</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "NTP",
        "param": {
          "name": "zServer",
          "#text": "192.168.50.10"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="NTP">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "NTP"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.15 NmeaUdpPort  <a id="NmeaUdpPort"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | int32 | iPort | UDP port number. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="NmeaUdpPort">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "NmeaUdpPort"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="NmeaUdpPort">
    <Param name="iPort">10110</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "NmeaUdpPort",
        "param": {
          "name": "iPort",
          "#text": "10110"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="NmeaUdpPort">
    <Param name="iPort">10110</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "NmeaUdpPort",
        "param": {
          "name": "iPort",
          "#text": "10110"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="NmeaUdpPort">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "NmeaUdpPort"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.16 OCXO  <a id="OCXO"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | int32 | iCalValue | Calibration value (D/A converter output). |
| i/o | int32 | iCalYear | Date of calibration: year. |
| i/o | int32 | iCalMonth | Month. |
| i/o | int32 | iCalDay | Day. |
| i/o | boolean | bStore | Store calibration value and date of calibration permanently (true: store, false: do
                        not store). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="OCXO">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "OCXO"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="OCXO">
    <Param name="iCalValue">2048</Param>
    <Param name="iCalYear">2012</Param>
    <Param name="iCalMonth">5</Param>
    <Param name="iCalDay">24</Param>
    <Param name="bStore">false</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "OCXO",
        "param": [
          {
            "name": "iCalValue",
            "#text": "2048"
          },
          {
            "name": "iCalYear",
            "#text": "2012"
          },
          {
            "name": "iCalMonth",
            "#text": "5"
          },
          {
            "name": "iCalDay",
            "#text": "24"
          },
          {
            "name": "bStore",
            "#text": "false"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="OCXO">
    <Param name="iCalValue">2048</Param>
    <Param name="iCalYear">2012</Param>
    <Param name="iCalMonth">5</Param>
    <Param name="iCalDay">24</Param>
    <Param name="bStore">false</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "OCXO",
        "param": [
          {
            "name": "iCalValue",
            "#text": "2048"
          },
          {
            "name": "iCalYear",
            "#text": "2012"
          },
          {
            "name": "iCalMonth",
            "#text": "5"
          },
          {
            "name": "iCalDay",
            "#text": "24"
          },
          {
            "name": "bStore",
            "#text": "false"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="OCXO">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "OCXO"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.17 ReferenceMode  <a id="ReferenceMode"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eREFERENCE_MODE | eReferenceMode | System reference mode. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="ReferenceMode">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ReferenceMode"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="ReferenceMode">
    <Param name="eReferenceMode">REFERENCE_MODE_EXTERNAL</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "ReferenceMode",
        "param": {
          "name": "eReferenceMode",
          "#text": "REFERENCE_MODE_EXTERNAL"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="ReferenceMode">
    <Param name="eReferenceMode">REFERENCE_MODE_EXTERNAL</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ReferenceMode",
        "param": {
          "name": "eReferenceMode",
          "#text": "REFERENCE_MODE_EXTERNAL"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="ReferenceMode">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "ReferenceMode"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.18 Reset  <a id="Reset"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| auto | eRESET_TYPE | eResetType | Type of reset.
                        (default value: 
                        RESET_WARM) |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="Reset">
    <Param name="eResetType">RESET_WARM</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Reset",
        "param": {
          "name": "eResetType",
          "#text": "RESET_WARM"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="Reset">
    <Param name="eResetType">RESET_WARM</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "Reset",
        "param": {
          "name": "eResetType",
          "#text": "RESET_WARM"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.19 RfMode  <a id="RfMode"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eRF_MODE | eRfMode | Preselection mode. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="RfMode">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "RfMode"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="RfMode">
    <Param name="eRfMode">RFMODE_NORMAL</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "RfMode",
        "param": {
          "name": "eRfMode",
          "#text": "RFMODE_NORMAL"
        }
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="RfMode">
    <Param name="eRfMode">RFMODE_NORMAL</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "RfMode",
        "param": {
          "name": "eRfMode",
          "#text": "RFMODE_NORMAL"
        }
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="RfMode">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "RfMode"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.20 TriggerSettings  <a id="TriggerSettings"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eTRIGGER_MODE | eTriggerMode | Triggered measurement mode (disabled, external or Synchronous Scan). |
| i/o | eTRIGGER_SOURCE | eTriggerSource | Trigger source (external connector etc.). |
| i/o | eTRIGGER_MEASUREMODE | eMeasureMode | Measurement mode (single or continuous). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="TriggerSettings">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "TriggerSettings"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="TriggerSettings">
    <Param name="eTriggerMode">DFTRIGGERMODE_DISABLED</Param>
    <Param name="eTriggerSource">DFTRIGGERSOURCE_TRIGGER</Param>
    <Param name="eMeasureMode">DFTRIGGERMEASMODE_SINGLE</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "TriggerSettings",
        "param": [
          {
            "name": "eTriggerMode",
            "#text": "DFTRIGGERMODE_DISABLED"
          },
          {
            "name": "eTriggerSource",
            "#text": "DFTRIGGERSOURCE_TRIGGER"
          },
          {
            "name": "eMeasureMode",
            "#text": "DFTRIGGERMEASMODE_SINGLE"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="TriggerSettings">
    <Param name="eTriggerMode">DFTRIGGERMODE_DISABLED</Param>
    <Param name="eTriggerSource">DFTRIGGERSOURCE_TRIGGER</Param>
    <Param name="eMeasureMode">DFTRIGGERMEASMODE_SINGLE</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TriggerSettings",
        "param": [
          {
            "name": "eTriggerMode",
            "#text": "DFTRIGGERMODE_DISABLED"
          },
          {
            "name": "eTriggerSource",
            "#text": "DFTRIGGERSOURCE_TRIGGER"
          },
          {
            "name": "eMeasureMode",
            "#text": "DFTRIGGERMEASMODE_SINGLE"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="TriggerSettings">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TriggerSettings"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.21 UartSettings  <a id="UartSettings"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | eUART | eUart | UART (connector) to address. |
| i/o | eSTATE | eState | Enable or disable selected UART. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="UartSettings">
    <Param name="eUart">UART_X15</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "UartSettings",
        "param": {
          "name": "eUart",
          "#text": "UART_X15"
        }
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="UartSettings">
    <Param name="eUart">UART_X15</Param>
    <Param name="eState">STATE_ON</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "UartSettings",
        "param": [
          {
            "name": "eUart",
            "#text": "UART_X15"
          },
          {
            "name": "eState",
            "#text": "STATE_ON"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="UartSettings">
    <Param name="eUart">UART_X15</Param>
    <Param name="eState">STATE_ON</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "UartSettings",
        "param": [
          {
            "name": "eUart",
            "#text": "UART_X15"
          },
          {
            "name": "eState",
            "#text": "STATE_ON"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="UartSettings">
    <Param name="eUart">UART_X15</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "UartSettings",
        "param": {
          "name": "eUart",
          "#text": "UART_X15"
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.10.22 VideoPan  <a id="VideoPan"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| i/o | eVIDEO_MODE | eVideoMode | VIDEO data trace state (on or off) and data format (16 bit or 32 bit per data value). |
| i/o | eDISPLAY_VARIANTS | eDisplayVariants | VDPan data trace state (on or off) and display variants. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="VideoPan">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "VideoPan"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="VideoPan">
    <Param name="eVideoMode">VIDEO_OFF</Param>
    <Param name="eDisplayVariants">DISPLAY_VARIANTS_OFF</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "VideoPan",
        "param": [
          {
            "name": "eVideoMode",
            "#text": "VIDEO_OFF"
          },
          {
            "name": "eDisplayVariants",
            "#text": "DISPLAY_VARIANTS_OFF"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="VideoPan">
    <Param name="eVideoMode">VIDEO_OFF</Param>
    <Param name="eDisplayVariants">DISPLAY_VARIANTS_OFF</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "VideoPan",
        "param": [
          {
            "name": "eVideoMode",
            "#text": "VIDEO_OFF"
          },
          {
            "name": "eDisplayVariants",
            "#text": "DISPLAY_VARIANTS_OFF"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="VideoPan">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "VideoPan"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

## 1.11 Trace-Commands

### 1.11.1 TraceDelete  <a id="TraceDelete"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | string | zIP | IP address (24 chars max.) of client host. |
| id | int32 | iPort | Local port on client host. |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="TraceDelete">
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceDelete",
        "param": [
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="TraceDelete">
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceDelete",
        "param": [
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.11.2 TraceDeleteInactive  <a id="TraceDeleteInactive"></a>

_No members (command takes no parameters)._

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="TraceDeleteInactive">
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceDeleteInactive"
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="TraceDeleteInactive">
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceDeleteInactive"
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.11.3 TraceDisable  <a id="TraceDisable"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | eTRACETAG | eTraceTag | Set of data that output is to be disabled for. |
| id | string | zIP | IP address (24 chars max.) of client host. |
| id | int32 | iPort | Local port on client host. |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="TraceDisable">
    <Param name="eTraceTag">TRACETAG_AUDIO</Param>
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceDisable",
        "param": [
          {
            "name": "eTraceTag",
            "#text": "TRACETAG_AUDIO"
          },
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="TraceDisable">
    <Param name="eTraceTag">TRACETAG_AUDIO</Param>
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceDisable",
        "param": [
          {
            "name": "eTraceTag",
            "#text": "TRACETAG_AUDIO"
          },
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.11.4 TraceEnable  <a id="TraceEnable"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | eTRACETAG | eTraceTag | Set of data that output is to be enabled for. |
| id | string | zIP | IP address (24 chars max.) of client host. |
| id | int32 | iPort | Local port on client host. |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="TraceEnable">
    <Param name="eTraceTag">TRACETAG_AUDIO</Param>
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceEnable",
        "param": [
          {
            "name": "eTraceTag",
            "#text": "TRACETAG_AUDIO"
          },
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="TraceEnable">
    <Param name="eTraceTag">TRACETAG_AUDIO</Param>
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceEnable",
        "param": [
          {
            "name": "eTraceTag",
            "#text": "TRACETAG_AUDIO"
          },
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.11.5 TraceFlagDisable  <a id="TraceFlagDisable"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | eSELECTORFLAG | eSelectorFlag | Selector flag of data subset to be disabled. |
| id | string | zIP | IP address (24 chars max.) of client host. |
| id | int32 | iPort | Local port on client host. |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="TraceFlagDisable">
    <Param name="eSelectorFlag">SELFLAG_AZIMUTH</Param>
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceFlagDisable",
        "param": [
          {
            "name": "eSelectorFlag",
            "#text": "SELFLAG_AZIMUTH"
          },
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="TraceFlagDisable">
    <Param name="eSelectorFlag">SELFLAG_AZIMUTH</Param>
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceFlagDisable",
        "param": [
          {
            "name": "eSelectorFlag",
            "#text": "SELFLAG_AZIMUTH"
          },
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.11.6 TraceFlagEnable  <a id="TraceFlagEnable"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | eSELECTORFLAG | eSelectorFlag | Selector flag of data subset to be enabled. |
| id | string | zIP | IP address (24 chars max.) of client host. |
| id | int32 | iPort | Local port on client host. |

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="TraceFlagEnable">
    <Param name="eSelectorFlag">SELFLAG_AZIMUTH</Param>
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceFlagEnable",
        "param": [
          {
            "name": "eSelectorFlag",
            "#text": "SELFLAG_AZIMUTH"
          },
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="TraceFlagEnable">
    <Param name="eSelectorFlag">SELFLAG_AZIMUTH</Param>
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceFlagEnable",
        "param": [
          {
            "name": "eSelectorFlag",
            "#text": "SELFLAG_AZIMUTH"
          },
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.11.7 TraceInfo  <a id="TraceInfo"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | int32 | iNoOfTraceSlots | Total number of trace slots with R&S DDFx (vacant and used). |
| out | sTRACE_INFO | asTraceInfo[20] | All registered trace slots (20 max.). |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="TraceInfo">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "TraceInfo"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="TraceInfo">
    <Param name="iNoOfTraceSlots">11</Param>
    <Array name="asTraceInfo">
      <Struct name="sTraceInfo">
        <Param name="iSlot">1</Param>
        <Param name="zIP">10.11.12.13</Param>
        <Param name="iPort">12345</Param>
        <Array name="aTraceTags">
          <Param name="eTraceTag">TRACETAG_IFPAN</Param>
          <!-- ... -->
        </Array>
        <Array name="aSelectorFlags">
          <Param name="eSelectorFlag">SELFLAG_LEVEL</Param>
          <!-- ... -->
        </Array>
      </Struct>
      <!-- ... -->
    </Array>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "TraceInfo",
        "param": {
          "name": "iNoOfTraceSlots",
          "#text": "11"
        },
        "array": {
          "name": "asTraceInfo",
          "struct": {
            "name": "sTraceInfo",
            "param": [
              {
                "name": "iSlot",
                "#text": "1"
              },
              {
                "name": "zIP",
                "#text": "10.11.12.13"
              },
              {
                "name": "iPort",
                "#text": "12345"
              }
            ],
            "array": [
              {
                "name": "aTraceTags",
                "param": {
                  "name": "eTraceTag",
                  "#text": "TRACETAG_IFPAN"
                }
              },
              {
                "name": "aSelectorFlags",
                "param": {
                  "name": "eSelectorFlag",
                  "#text": "SELFLAG_LEVEL"
                }
              }
            ]
          }
        }
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.11.8 TraceTcpTxBuffer  <a id="TraceTcpTxBuffer"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| id | string | zIP | IP address (24 chars max.) of client host. |
| id | int32 | iPort | Local port on client host. |
| i/o | int32 | iTxBufferSize | Size [bytes] of pool portion to be used for current TCP connection. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="TraceTcpTxBuffer">
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "TraceTcpTxBuffer",
        "param": [
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          }
        ]
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="TraceTcpTxBuffer">
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
    <Param name="iTxBufferSize">10000000</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "TraceTcpTxBuffer",
        "param": [
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          },
          {
            "name": "iTxBufferSize",
            "#text": "10000000"
          }
        ]
      }
    }
  }
}
```

**Request (SET) XML (from ICD):**
```xml
<Request type="set" id="123">
  <Command name="TraceTcpTxBuffer">
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
    <Param name="iTxBufferSize">10000000</Param>
  </Command>
</Request>
```

**Request (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceTcpTxBuffer",
        "param": [
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          },
          {
            "name": "iTxBufferSize",
            "#text": "10000000"
          }
        ]
      }
    }
  }
}
```

**Reply (SET) XML (from ICD):**
```xml
<Reply type="set" id="123">
  <Command name="TraceTcpTxBuffer">
    <Param name="zIP">10.11.12.13</Param>
    <Param name="iPort">12345</Param>
  </Command>
</Reply>
```

**Reply (SET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "set",
      "id": "123",
      "command": {
        "name": "TraceTcpTxBuffer",
        "param": [
          {
            "name": "zIP",
            "#text": "10.11.12.13"
          },
          {
            "name": "iPort",
            "#text": "12345"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

### 1.11.9 TraceTcpTxBufferPool  <a id="TraceTcpTxBufferPool"></a>

| ioType | Datatype | Member name | Documentation |
|---|---|---|---|
| out | int32 | iMaxPoolSize | Size [bytes] of entire TCP Tx buffer pool. |
| out | int32 | iUsedPoolSize | Size [bytes] of pool portion in use for all TCP connections. |
| out | int32 | iFreePoolSize | Size [bytes] of pool portion available for new TCP connections. |

**Request (GET) XML (from ICD):**
```xml
<Request type="get" id="123">
  <Command name="TraceTcpTxBufferPool">
  </Command>
</Request>
```

**Request (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "request",
  "body": {
    "request": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "TraceTcpTxBufferPool"
      }
    }
  }
}
```

**Reply (GET) XML (from ICD):**
```xml
<Reply type="get" id="123">
  <Command name="TraceTcpTxBufferPool">
    <Param name="iMaxPoolSize">25000000</Param>
    <Param name="iUsedPoolSize">5007360</Param>
    <Param name="iFreePoolSize">19992640</Param>
  </Command>
</Reply>
```

**Reply (GET) JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "control",
  "msg_kind": "reply",
  "body": {
    "reply": {
      "type": "get",
      "id": "123",
      "command": {
        "name": "TraceTcpTxBufferPool",
        "param": [
          {
            "name": "iMaxPoolSize",
            "#text": "25000000"
          },
          {
            "name": "iUsedPoolSize",
            "#text": "5007360"
          },
          {
            "name": "iFreePoolSize",
            "#text": "19992640"
          }
        ]
      }
    }
  }
}
```

[back to top](#ddf-550--ddfx-control-path-tcp-9150---xml-to-json-reference)

---

<a id="part-2-preclassifier-tcp-9153-control--tcp-9154-output"></a>
# Part 2 — Preclassifier (TCP 9153 control / TCP 9154 output)

Source: `DDFSystemControlInterfacePreClassifier.pdf` — R&S DDF-SCIF ("DDF-System
Control Interface") User Manual, doc 3025.2887.02, version 10.

**This is a different ICD describing a different sub-protocol from Part 1.**
Everything in Part 1 is one `Request`/`Reply` command language sent on one port
(9150). The preclassifier feature uses **two separate TCP connections with two
unrelated XML vocabularies**:

| Port | Direction | Root tags | Purpose |
|---|---|---|---|
| **9153** ("DDFCLCmdPort") | bidirectional | `DDFCLRequest` / `DDFCLReply` | get/set the R&S DDF-CL (preclassifier) algorithm's own config parameters (sensitivities, timing, thresholds) |
| **9154** ("preclassifier XML output") | bidirectional | `DFSelect`, `FormatSelect` (client→DDF-CL) / `DFJob`, `DFData` (DDF-CL→client) | select which emitter classes/output format to receive, then continuously receive job definitions and classification results |

**Framing: confirmed raw XML, no binary wrapper, on both ports.** Every
example in the manual (§6.2.1, §6.4.3) shows plain XML text with no magic
bytes or length prefix — unlike port 9150, whose `[MagicStart][Len][XML]
[MagicEnd]` wrapper is documented separately in the main DDF-550 ICD (§3.3.1)
and is the subject of open blocker **D1** (`icd-open-questions.md`). This
matches `DRS_BRIDGE_PORTS.md`'s note for 9153 ("no binary wrapper") and is the
basis for the `format_response` framing bug discussed inline in this
conversation: `format_response` currently wraps its `DDFCLRequest`/
`DDFCLReply` output in the same magic-word envelope as 9150, which this
manual gives no evidence for and `DRS_BRIDGE_PORTS.md` explicitly contradicts.

JSON below is computed the same way as Part 1: the real
`pugixml_generic_mirror.cpp` algorithm (snake_case tags/attributes,
attributes-then-children merge, repeated-tag-becomes-array, `"#text"` for
mixed content), applied to each literal XML example from the manual. Verified
by running a byte-for-byte Python port of that algorithm against every
example below, not hand-derived.

---

## A. Preclassifier control (TCP 9153) — `DDFCLRequest` / `DDFCLReply`

### XML shape (from the manual, §6.4.3)

```xml
<?xml version="1.0" ?>
<DDFCLRequest id="ID" type="TYPE">
  <Command name="COMMAND">
    VALUE
  </Command>
</DDFCLRequest>
```

- `ID` — a continuously increasing integer starting at 1 (chosen by the caller, not the device).
- `TYPE` — `"get"` or `"set"`. If `"get"`, `VALUE` is omitted.
- `COMMAND` — one of the 19 parameter names in the table below.
- `VALUE` — the value for `COMMAND` (only for `"set"`, or present in a `"get"` reply).

Reply is the same shape with root tag `DDFCLReply`, echoing `id`/`type`/`command name` back, `VALUE` always filled in.

**Note on attribute order:** `id` then `type` — matches `ddf550_parser.cpp`'s
`format_response` DDFCL branch (`"<%s id=\"%lld\" type=\"%s\">"`), i.e. this
part of the encoder is already correct.

### Parameter table (Table 6-4/6-5, all `get`+`set`)

| Command | Description | Min/Max/Step | Default |
|---|---|---|---|
| `CyclicAnalysisEnabled` | Enable/disable the preclassifier | bool | `true` |
| `AnalysisIntervalMs` | Length of each analysis period [ms] | 2000/60000/100 | `8000` |
| `AnalysisOverlapTimeMs` | Overlap between successive cyclic analyses [ms] | 0/1000/10 | `800` |
| `DetectionsKeepTimeMs` | How long detected emitters/emissions/bursts are kept [ms] | 0/600000/1000 | `180000` |
| `MaxNumberReportedEmitters` | Hard limit on number of emitters reported | 0/10000/10 | `1000` |
| `ReportedEmittersQualityThreshold` | Quality threshold for reported emitters | 0/100/1 | `40` |
| `ReportedEmittersMaxNumBurstsActive` | Enable/disable the limit on max bursts reported | bool | `false` |
| `ReportedEmittersMaxNumBursts` | Max bursts reported per emitter per analysis | 4/100/1 | `10` |
| `MaxTimeSlotsEmissionInactive` | Min inactivity (in spectra) before a new emission starts | 0/500/1 | `10` |
| `ReportedEmittersQualityThreshold` *(sic — see note below)* | "Quality threshold for azimuth. Results below this threshold are ignored." | 0/100/1 | `60` |
| `MinNumberDataTilesForBurst` | Min data tiles a burst emitter must contain before being reported | 0/10000/1 | `5` |
| `DetectorBurstLengthLimitMs` | Max length of a detected burst before it's split in two [ms] | 0/60000/100 | `5000` |
| `SensitivityAz` | Azimuth sensitivity weight | 0/100/1 | `50` |
| `SensitivityFreq` | Frequency sensitivity weight | 0/100/1 | `50` |
| `SensitivityLvl` | Level sensitivity weight | 0/100/1 | `50` |
| `SensitivityBw` | Bandwidth sensitivity weight | 0/100/1 | `50` |
| `SensitivityLength` | Length sensitivity weight | 0/100/1 | `50` |
| `SensitivityTiming` | Timing sensitivity weight | 0/100/1 | `50` |
| `SensitivityChannelOccupancy` | Channel-occupancy sensitivity weight | 0/100/1 | `50` |

> **Doc defect in the source manual, not mine:** the manual's own Table 6-4
> gives the identical quoted name `"ReportedEmittersQualityThreshold"` to
> *two different rows* (page 24, general quality threshold, default 40; and
> page 25, azimuth-specific threshold, default 60). Since `Command name="..."`
> is how the wire protocol dispatches, two commands sharing one name is a
> real ambiguity — not something to silently rename or guess at. Flagging
> for whoever owns the vendor-doc relationship; don't invent a distinct name
> for the second row without confirming against R&S.

### Worked example 1 — SET (manual's own example, §6.4.3)

**Request XML:**
```xml
<?xml version="1.0" ?>
<DDFCLRequest id="1" type="set">
  <Command name="AnalysisIntervalMs">
    50000
  </Command>
</DDFCLRequest>
```

**Request JSON (as produced by pugixml_generic_mirror):**
```json
{
  "hw": "ddf550",
  "channel": "preclassifier",
  "msg_kind": "request",
  "body": {
    "ddfcl_request": {
      "id": "1",
      "type": "set",
      "command": {
        "name": "AnalysisIntervalMs",
        "#text": "50000"
      }
    }
  }
}
```

**Reply XML:**
```xml
<?xml version="1.0" ?>
<DDFCLReply id="1" type="set">
  <Command name="AnalysisIntervalMs">
    50000
  </Command>
</DDFCLReply>
```

**Reply JSON:**
```json
{
  "hw": "ddf550",
  "channel": "preclassifier",
  "msg_kind": "reply",
  "body": {
    "ddfcl_reply": {
      "id": "1",
      "type": "set",
      "command": {
        "name": "AnalysisIntervalMs",
        "#text": "50000"
      }
    }
  }
}
```

### Worked example 2 — GET (manual's own example, §6.4.3)

**Request XML:**
```xml
<?xml version="1.0" ?>
<DDFCLRequest id="3" type="get">
  <Command name="SensitivityTiming">
  </Command>
</DDFCLRequest>
```

**Request JSON:**
```json
{
  "hw": "ddf550",
  "channel": "preclassifier",
  "msg_kind": "request",
  "body": {
    "ddfcl_request": {
      "id": "3",
      "type": "get",
      "command": {
        "name": "SensitivityTiming"
      }
    }
  }
}
```

**Reply XML:**
```xml
<?xml version="1.0" ?>
<DDFCLReply id="3" type="get">
  <Command name="SensitivityTiming">
    50
  </Command>
</DDFCLReply>
```

**Reply JSON:**
```json
{
  "hw": "ddf550",
  "channel": "preclassifier",
  "msg_kind": "reply",
  "body": {
    "ddfcl_reply": {
      "id": "3",
      "type": "get",
      "command": {
        "name": "SensitivityTiming",
        "#text": "50"
      }
    }
  }
}
```

### Error replies (Table 6-5) — a different shape from every "normal" reply

These two shapes **do not** carry `type`, and one of them doesn't carry a
`Command name` at all — they break the "command always has a name" assumption
that both this document's Part 1 and `build_command_xml` rely on:

**Invalid XML string** (whole request wasn't parseable — id is always `"0"`,
since the id inside the bad request couldn't be read):
```xml
<?xml version="1.0" ?>
<DDFCLReply id="0">
  <Command returnCode="1" returnMessage="Invalid command format">
  </Command>
</DDFCLReply>
```
```json
{
  "hw": "ddf550",
  "channel": "preclassifier",
  "msg_kind": "reply",
  "body": {
    "ddfcl_reply": {
      "id": "0",
      "command": {
        "return_code": "1",
        "return_message": "Invalid command format"
      }
    }
  }
}
```
Note: `command` has **no `"name"` key at all** here. If this shape were ever
fed to `build_command_xml` (the encode side), it would throw
(`"command" object missing "name"`) — correctly, since there's nothing
sensible to re-encode, but worth knowing this is one of the few real shapes
that hits that throw path in practice, not just a theoretical one.

**Invalid command name** (request parsed fine, but `Command name="..."`
wasn't recognized):
```xml
<?xml version="1.0" ?>
<DDFCLReply id="ID">
  <Command name="COMMAND" returnCode="2" returnMessage="Invalid command name">
  </Command>
</DDFCLReply>
```
Concrete instance (`id="2"`, unrecognized command `"Bogus"`):
```json
{
  "hw": "ddf550",
  "channel": "preclassifier",
  "msg_kind": "reply",
  "body": {
    "ddfcl_reply": {
      "id": "2",
      "command": {
        "name": "Bogus",
        "return_code": "2",
        "return_message": "Invalid command name"
      }
    }
  }
}
```
This one *does* have `"name"`, so `build_command_xml` would happily re-encode
it — but silently produces `<Command name="Bogus" returnCode="2" ...>`-shaped
intent without any special handling for `return_code`/`return_message` as
non-`param` attributes; they'd currently be **dropped**, same class of bug as
the `array`/`struct` case fixed earlier in Part 1, just not yet checked here
since `build_command_xml` is Part 1's (9150-only) encoder and has never been
pointed at 9153 traffic.

---

## B. Preclassifier output (TCP 9154) — `DFSelect` / `FormatSelect` / `DFJob` / `DFData`

Unlike 9153's request/reply pattern, this port is a **single connection used
both ways at once**: the client sends `DFSelect`/`FormatSelect` control
commands on it, while the DDF-CL continuously pushes `DFJob` (job definition,
sent on connect and whenever the scan job changes) and `DFData` (classification
results, sent every analysis period) back on the same socket, unprompted.

### `DFSelect` — client selects which emitter classes to receive (§6.2.1)

```xml
<DFSelect>
    <EmitterClass> Burst </EmitterClass>
    <EmitterClass> Hopper </EmitterClass>
</DFSelect>
```
```json
{
  "hw": "ddf550",
  "channel": "preclassifier",
  "msg_kind": "request",
  "body": {
    "df_select": {
      "emitter_class": [
        " Burst ",
        " Hopper "
      ]
    }
  }
}
```
Note the leading/trailing spaces survive verbatim into the JSON strings — the
mirror never trims whitespace (documented Part-1 behavior, §3 rule 6 of the
mirror contract), and the manual's own example XML has that padding around
`Burst`/`Hopper`.

**`classify_xml_root` in `ddf550_parser.cpp` already recognizes `DFSelect`**
(frame_type 1), so this one *is* decodable by the current parser DLL. It's
tagged `channel: "preclassifier"` — the same channel string as 9153's
`DDFCLRequest`/`DDFCLReply` — even though `DFSelect` actually arrives on the
*different* physical port 9154. The JSON alone can't distinguish "came in on
9153" from "came in on 9154" for this tag; that's a pre-existing channel-
taxonomy simplification, not something introduced by this doc.

### `FormatSelect` — client selects output format (§6.2.1)

**As literally printed in the manual:**
```xml
<FormatSelect> FORMAT02<FormatSelect>
```
This is not well-formed XML — the closing tag is missing its `/`
(`<FormatSelect>` instead of `</FormatSelect>`), so it's almost certainly a
typo in the R&S manual, not a real wire example. Parsing it as-is: **fails**
("no element found" — the document never closes). Assuming the obviously
intended form:
```xml
<FormatSelect>FORMAT02</FormatSelect>
```
```json
{
  "hw": "ddf550",
  "channel": "preclassifier_output",
  "msg_kind": "dfdata",
  "body": {
    "format_select": "FORMAT02"
  }
}
```
**But this JSON is illustrative only — `FormatSelect` is not in
`classify_xml_root`'s recognized root-tag table at all** (`DDFCLRequest`,
`DDFCLReply`, `Request`, `Reply`, `DFData`, `Event`, `DFSelect` — no
`FormatSelect`). A real `<FormatSelect>...</FormatSelect>` frame arriving on
9154 today would be rejected by `extract_frame`'s raw-XML path (`root_tag`
comes back null → returns `-1`, i.e. "corrupt"), before `parse_message` is
ever reached. The `channel`/`msg_kind` values shown above are my own
placeholder labels for illustration, not something the current code would
actually compute — there is no real envelope for this tag today. This is a
genuine parser gap: format-selection is a documented, real preclassifier
feature the DLL currently can't parse.

### `DFJob` — job definition, pushed by DDF-CL on connect / job change (§6.2.2)

```xml
<DFJob>
    <Frequency Unit="Hz"> 10000000 </Frequency>
    <Bandwidth Unit="Hz"> 10000 </Bandwidth>
    <Frequency Unit="Hz"> 20000000 </Frequency>
    <Bandwidth Unit="Hz"> 12500 </Bandwidth>
    <FrequencyStart Unit="Hz"> 330000000 </FrequencyStart>
    <FrequencyStop Unit="Hz"> 440000000 </FrequencyStop>
    <FrequencyStep Unit="Hz"> 12500 </FrequencyStep>
</DFJob>
```
```json
{
  "hw": "ddf550",
  "channel": "preclassifier_output",
  "msg_kind": "dfjob",
  "body": {
    "df_job": {
      "frequency": [
        {"unit": "Hz", "#text": " 10000000 "},
        {"unit": "Hz", "#text": " 20000000 "}
      ],
      "bandwidth": [
        {"unit": "Hz", "#text": " 10000 "},
        {"unit": "Hz", "#text": " 12500 "}
      ],
      "frequency_start": {"unit": "Hz", "#text": " 330000000 "},
      "frequency_stop":  {"unit": "Hz", "#text": " 440000000 "},
      "frequency_step":  {"unit": "Hz", "#text": " 12500 "}
    }
  }
}
```
Notable structural case: `Frequency` and `Bandwidth` each appear **twice**,
*interleaved* with each other (`Freq, BW, Freq, BW, ...`) rather than grouped
together in the source XML. The mirror still groups all same-tag children
into one array regardless of interleaving — `"frequency"` and `"bandwidth"`
each become 2-element arrays, in original relative order. Same rule as Part
1, just not exercised by any control-path example.

**Same gap as `FormatSelect`: `DFJob` is not in `classify_xml_root`'s
recognized set either.** The JSON above is what the algorithm *would* produce
if handed a parsed `DFJob` tree — but `extract_frame` currently rejects any
real `<DFJob>` frame as unrecognized before that ever happens. Two of the
four preclassifier-output root tags this manual documents (`FormatSelect`,
`DFJob`) are simply not decodable by the DLL today; only `DFSelect` and
`DFData` are.

### `DFData` — classification results, one of three wire formats

Format is chosen by `FormatSelect` (§6.2.1) — `FORMAT01` (default),
`FORMAT02`, or `FORMAT03`. All three are `<DFData>` root tag, already
recognized by `classify_xml_root` (`channel: "preclassifier_output"`,
`msg_kind: "dfdata"`) and exercised by the existing `test_dfdata_*` tests in
`test_frames_ddf550.cpp` for the FORMAT02-style shape.

#### FORMAT01 — one `<DFData>` per emitter, station data repeated on every message

**Example: fixed frequency (Static emitter)**
```xml
<DFData>
  <EmitterClass> Static </EmitterClass>
  <Frequency Unit="Hz"> 61150000 </Frequency>
  <Bandwidth Unit="Hz"> 165000 </Bandwidth>
  <DFStationData>
    <DFStationName> DDF0xA-Simulation </DFStationName>
    <DFStationLongitude Unit="deg"> 11.61222223 </DFStationLongitude>
    <DFStationLatitude Unit="deg"> 48.12805556 </DFStationLatitude>
    <DDF-CL-ID>16777217</DDF-CL-ID>
    <BearingAvg Unit="deg"> 145.0 </BearingAvg>
    <BearingStdDev Unit="deg">  0.5 </BearingStdDev>
    <Quality> 10 </Quality>
    <LevelAvg Unit="dBuV"> 71.7 </LevelAvg>
    <LevelStdDev Unit="dBuV"> 8.2 </LevelStdDev>
    <FirstDetect> 2005-10-18 13:14:18.500.000 </FirstDetect>
    <LastDetect> 2005-10-18 13:14:22.093.000 </LastDetect>
  </DFStationData>
</DFData>
```
```json
{
  "hw": "ddf550",
  "channel": "preclassifier_output",
  "msg_kind": "dfdata",
  "body": {
    "df_data": {
      "emitter_class": " Static ",
      "frequency": {"unit": "Hz", "#text": " 61150000 "},
      "bandwidth": {"unit": "Hz", "#text": " 165000 "},
      "df_station_data": {
        "df_station_name": " DDF0xA-Simulation ",
        "df_station_longitude": {"unit": "deg", "#text": " 11.61222223 "},
        "df_station_latitude": {"unit": "deg", "#text": " 48.12805556 "},
        "ddf-cl-id": "16777217",
        "bearing_avg": {"unit": "deg", "#text": " 145.0 "},
        "bearing_std_dev": {"unit": "deg", "#text": "  0.5 "},
        "quality": " 10 ",
        "level_avg": {"unit": "dBuV", "#text": " 71.7 "},
        "level_std_dev": {"unit": "dBuV", "#text": " 8.2 "},
        "first_detect": " 2005-10-18 13:14:18.500.000 ",
        "last_detect": " 2005-10-18 13:14:22.093.000 "
      }
    }
  }
}
```
Note `<DDF-CL-ID>` (hyphenated) → `"ddf-cl-id"` — `to_snake_case` only
lowercases, it never touches non-alphanumeric characters, so hyphens survive
as-is (documented Part-1 rule, confirmed again here).

**Example: burst** (same shape as Static — single `Frequency`, not a range):
```xml
<DFData>
  <EmitterClass> Burst </EmitterClass>
  <Frequency Unit="Hz"> 58102500 </Frequency>
  <Bandwidth Unit="Hz"> 140000 </Bandwidth>
  <DFStationData>
    <DFStationName> DDF0xA-Simulation </DFStationName>
    <DFStationLongitude Unit="deg"> 11.61222223 </DFStationLongitude>
    <DFStationLatitude Unit="deg"> 48.12805556 </DFStationLatitude>
    <DDF-CL-ID>16777275</DDF-CL-ID>
    <BearingAvg Unit="deg">  110.0 </BearingAvg>
    <BearingStdDev Unit="deg"> 1.4 </BearingStdDev>
    <Quality> 12 </Quality>
    <LevelAvg Unit="dBuV"> 59.8 </LevelAvg>
    <LevelStdDev Unit="dBuV">  2.9 </LevelStdDev>
    <FirstDetect> 2005-10-18 14:04:48.265.000 </FirstDetect>
    <LastDetect> 2005-10-18 14:04:48.359.000 </LastDetect>
  </DFStationData>
</DFData>
```
JSON: identical shape to the Static example above (same tags), just different values — omitted for brevity.

**Example: hopper** (frequency *range* instead of a single frequency — note
this is the exact "Hopper `StartFrequency`/`StopFrequency` not extracted"
gap tracked as open question **D7** in `icd-open-questions.md`, though D7 is
about the DDF-1GTX Hopper EB200 fields, a different channel than this one):
```xml
<DFData>
  <EmitterClass>Hopper</EmitterClass>
  <FrequencyMin Unit="Hz">55337500</FrequencyMin>
  <FrequencyMax Unit="Hz">59262905</FrequencyMax>
  <FrequencyCount>58</FrequencyCount>
  <FrequencyList>55350000,55625000,55700000,55800000,55950000,55975000,56050000,56125000,...</FrequencyList>
  <FrequencyListReport>
    <FrequencyReport><Frequency Unit="Hz">55350000</Frequency></FrequencyReport>
    <FrequencyReport><Frequency Unit="Hz">55625000</Frequency></FrequencyReport>
    <!-- ... one FrequencyReport per FrequencyList entry ... -->
  </FrequencyListReport>
  <Bandwidth Unit="Hz">0</Bandwidth>
  <DFStationData>
    <DFStationName>Simulation1</DFStationName>
    <DFStationLatitude Unit="deg">48.12790000</DFStationLatitude>
    <DFStationLongitude Unit="deg">11.61290000</DFStationLongitude>
    <DDF-CL-ID>11</DDF-CL-ID>
    <BearingAvg Unit="deg">85.7</BearingAvg>
    <BearingStdDev Unit="deg">2.7</BearingStdDev>
    <Quality>94</Quality>
    <LevelAvg Unit="dBuV">17.7</LevelAvg>
    <LevelStdDev Unit="dBuV">1.3</LevelStdDev>
    <FirstDetect>2013-01-15 12:44:53.970000</FirstDetect>
    <LastDetect>2013-01-15 12:45:03.753000</LastDetect>
    <ChannelSpacing>78947</ChannelSpacing>
    <BurstDuration>22</BurstDuration>
  </DFStationData>
</DFData>
```
```json
{
  "hw": "ddf550",
  "channel": "preclassifier_output",
  "msg_kind": "dfdata",
  "body": {
    "df_data": {
      "emitter_class": "Hopper",
      "frequency_min": {"unit": "Hz", "#text": "55337500"},
      "frequency_max": {"unit": "Hz", "#text": "59262905"},
      "frequency_count": "58",
      "frequency_list": "55350000,55625000,55700000,55800000,55950000,55975000,56050000,56125000,...",
      "frequency_list_report": {
        "frequency_report": [
          {"frequency": {"unit": "Hz", "#text": "55350000"}},
          {"frequency": {"unit": "Hz", "#text": "55625000"}}
        ]
      },
      "bandwidth": {"unit": "Hz", "#text": "0"},
      "df_station_data": {
        "df_station_name": "Simulation1",
        "df_station_latitude": {"unit": "deg", "#text": "48.12790000"},
        "df_station_longitude": {"unit": "deg", "#text": "11.61290000"},
        "ddf-cl-id": "11",
        "bearing_avg": {"unit": "deg", "#text": "85.7"},
        "bearing_std_dev": {"unit": "deg", "#text": "2.7"},
        "quality": "94",
        "level_avg": {"unit": "dBuV", "#text": "17.7"},
        "level_std_dev": {"unit": "dBuV", "#text": "1.3"},
        "first_detect": "2013-01-15 12:44:53.970000",
        "last_detect": "2013-01-15 12:45:03.753000",
        "channel_spacing": "78947",
        "burst_duration": "22"
      }
    }
  }
}
```
Note `frequency_list` is a plain **comma-separated string**, not an array —
the mirror only arrays *repeated XML elements*, and `<FrequencyList>` is a
single element whose text happens to contain commas. Splitting it into
individual frequencies is a consumer-side job, same as `#text` int-casting.

#### FORMAT02 — station data sent once per update, not once per emitter; `DDF-CL-ID` becomes an attribute

```xml
<DFData DDF-CL-ID="16">
  <EmitterClass>Static</EmitterClass>
  <StartFrequency Unit="Hz">59237500</StartFrequency>
  <CenterFrequency Unit="Hz">59250202</CenterFrequency>
  <StopFrequency Unit="Hz">59262905</StopFrequency>
  <BearingAvg Unit="deg">22.3</BearingAvg>
  <BearingStdDev Unit="deg">2.0</BearingStdDev>
  <LevelAvg Unit="dBuV">18.6</LevelAvg>
  <LevelStdDev Unit="dBuV">1.6</LevelStdDev>
  <ElevationAvg Unit="deg">12.0</ElevationAvg>
  <ElevationStdDev Unit="deg">5.4</ElevationStdDev>
  <FirstDetectionTime>2013-01-15 13:00:14.556000</FirstDetectionTime>
  <LastDetectionTime>2013-01-15 13:00:23.919000</LastDetectionTime>
  <Quality>94</Quality>
</DFData>
```
```json
{
  "hw": "ddf550",
  "channel": "preclassifier_output",
  "msg_kind": "dfdata",
  "body": {
    "df_data": {
      "ddf-cl-id": "16",
      "emitter_class": "Static",
      "start_frequency": {"unit": "Hz", "#text": "59237500"},
      "center_frequency": {"unit": "Hz", "#text": "59250202"},
      "stop_frequency": {"unit": "Hz", "#text": "59262905"},
      "bearing_avg": {"unit": "deg", "#text": "22.3"},
      "bearing_std_dev": {"unit": "deg", "#text": "2.0"},
      "level_avg": {"unit": "dBuV", "#text": "18.6"},
      "level_std_dev": {"unit": "dBuV", "#text": "1.6"},
      "elevation_avg": {"unit": "deg", "#text": "12.0"},
      "elevation_std_dev": {"unit": "deg", "#text": "5.4"},
      "first_detection_time": "2013-01-15 13:00:14.556000",
      "last_detection_time": "2013-01-15 13:00:23.919000",
      "quality": "94"
    }
  }
}
```
Here `DDF-CL-ID` is an **XML attribute** on `<DFData>` itself (not a child
element like FORMAT01's `<DDF-CL-ID>`), and note it's the attribute-then-child
merge rule that puts `"ddf-cl-id"` first in the object, ahead of
`"emitter_class"` — matching source order (attributes always come before
children in `node_object_body`).

#### FORMAT03 — same as FORMAT02, plus a `ScanRangeId` element

```xml
<DFData DDF-CL-ID="17">
  <ScanRangeId>1</ScanRangeId>
  <EmitterClass>Hopper</EmitterClass>
  <StartFrequency Unit="Hz">55237500</StartFrequency>
  <CenterFrequency Unit="Hz">57405418</CenterFrequency>
  <StopFrequency Unit="Hz">59912500</StopFrequency>
  <BearingAvg Unit="deg">85.2</BearingAvg>
  <BearingStdDev Unit="deg">2.9</BearingStdDev>
  <LevelAvg Unit="dBuV">18.0</LevelAvg>
  <LevelStdDev Unit="dBuV">1.4</LevelStdDev>
  <ElevationAvg Unit="deg">7.6</ElevationAvg>
  <ElevationStdDev Unit="deg">0.8</ElevationStdDev>
  <FirstDetectionTime>2013-01-15 13:00:14.556000</FirstDetectionTime>
  <LastDetectionTime>2013-01-15 13:00:23.919000</LastDetectionTime>
  <Quality>95</Quality>
  <FrequencyCount>42</FrequencyCount>
  <FrequencyList>55350000,55625000,55700000,55800000,55950000,55975000,56050000,56125000,...</FrequencyList>
  <BurstDuration Unit="ms">44</BurstDuration>
  <BurstBandwidth Unit="Hz">120</BurstBandwidth>
</DFData>
```
```json
{
  "hw": "ddf550",
  "channel": "preclassifier_output",
  "msg_kind": "dfdata",
  "body": {
    "df_data": {
      "ddf-cl-id": "17",
      "scan_range_id": "1",
      "emitter_class": "Hopper",
      "start_frequency": {"unit": "Hz", "#text": "55237500"},
      "center_frequency": {"unit": "Hz", "#text": "57405418"},
      "stop_frequency": {"unit": "Hz", "#text": "59912500"},
      "bearing_avg": {"unit": "deg", "#text": "85.2"},
      "bearing_std_dev": {"unit": "deg", "#text": "2.9"},
      "level_avg": {"unit": "dBuV", "#text": "18.0"},
      "level_std_dev": {"unit": "dBuV", "#text": "1.4"},
      "elevation_avg": {"unit": "deg", "#text": "7.6"},
      "elevation_std_dev": {"unit": "deg", "#text": "0.8"},
      "first_detection_time": "2013-01-15 13:00:14.556000",
      "last_detection_time": "2013-01-15 13:00:23.919000",
      "quality": "95",
      "frequency_count": "42",
      "frequency_list": "55350000,55625000,55700000,55800000,55950000,55975000,56050000,56125000,...",
      "burst_duration": {"unit": "ms", "#text": "44"},
      "burst_bandwidth": {"unit": "Hz", "#text": "120"}
    }
  }
}
```

---

## C. Summary — what's actually usable today vs. documented-but-unparseable

| Root tag | Port | Documented in manual | Recognized by `classify_xml_root` | Notes |
|---|---|---|---|---|
| `DDFCLRequest` / `DDFCLReply` | 9153 | Yes | **Yes** | Framing bug: encoder wraps in magic-word envelope this port doesn't use (see conversation) |
| `DFSelect` | 9154 | Yes | **Yes** | Tagged `channel:"preclassifier"`, same as 9153's tags, despite being a different port |
| `FormatSelect` | 9154 | Yes | **No** | Real gap — any real frame rejected as corrupt before `parse_message` runs |
| `DFJob` | 9154 | Yes | **No** | Same gap as `FormatSelect` |
| `DFData` (FORMAT01/02/03) | 9154 | Yes | Yes | Already exercised by existing `test_dfdata_*` tests |

Two of five preclassifier-output message types (`FormatSelect`, `DFJob`) are
real, ICD-documented traffic this parser DLL cannot currently decode at all —
worth a decision on whether/when to add them to `classify_xml_root`, separate
from the `format_response` framing question raised earlier in this
conversation.
