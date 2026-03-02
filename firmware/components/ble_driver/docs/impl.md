## Battery Level Status Characteristic (0x2BEA)

Source: [GATT Specification Supplement](https://btprodspecificationrefs.blob.core.windows.net/gatt-specification-supplement/GATT_Specification_Supplement.pdf) §3.31

### Structure

| Field             | Data Type   | Size (octets) | Description                              |
| ----------------- | ----------- | ------------- | ---------------------------------------- |
| Flags             | boolean[8]  | 1             | See Flags field table                    |
| Power State       | boolean[16] | 2             | See Power State field table              |
| Identifier        | uint16      | 0 or 2        | Present if bit 0 of Flags is set         |
| Battery Level     | uint8       | 0 or 1        | 0–100%. Present if bit 1 of Flags is set |
| Additional Status | boolean[8]  | 0 or 1        | Present if bit 2 of Flags is set         |

### Flags Field (§3.31.1)

| Bit | Definition                |
| --- | ------------------------- |
| 0   | Identifier Present        |
| 1   | Battery Level Present     |
| 2   | Additional Status Present |
| 3–7 | RFU                       |

### Power State Field (§3.31.2)

| Bits | Definition                                                                                                 |
| ---- | ---------------------------------------------------------------------------------------------------------- |
| 0    | Battery Present: 0=No, 1=Yes                                                                               |
| 1–2  | Wired External Power Source Connected: 0=No, 1=Yes, 2=Unknown, 3=RFU                                       |
| 3–4  | Wireless External Power Source Connected: 0=No, 1=Yes, 2=Unknown, 3=RFU                                    |
| 5–6  | Battery Charge State: 0=Unknown, 1=Charging, 2=Discharging Active, 3=Discharging Inactive                  |
| 7–8  | Battery Charge Level: 0=Unknown, 1=Good, 2=Low, 3=Critical                                                 |
| 9–11 | Charging Type: 0=Unknown/Not Charging, 1=Constant Current, 2=Constant Voltage, 3=Trickle, 4=Float, 5–7=RFU |
| 12   | Charging Fault Reason: Battery                                                                             |
| 13   | Charging Fault Reason: External Power Source                                                               |
| 14   | Charging Fault Reason: Other                                                                               |
| 15   | RFU                                                                                                        |

### Additional Status Field (§3.31.3)

| Bits | Definition                                          |
| ---- | --------------------------------------------------- |
| 0–1  | Service Required: 0=False, 1=True, 2=Unknown, 3=RFU |
| 2    | Battery Fault: 0=False or Unknown, 1=Yes            |
| 3–7  | RFU                                                 |

### PetPulse Usage

- Flags fixed to `0x06` (Battery Level + Additional Status present, Identifier omitted — single battery device)
- Power State set dynamically based on charge state and wired connection
- Battery Level: 0–100% from BMS
- Additional Status: service required and fault flags from BMS

---

## TX Power

Set TX power before `NimBLEDevice::init()`. Can be set separately for advertising and connected modes to balance discoverability and battery life.

```cpp
NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);  // high for advertising
NimBLEDevice::setPower(ESP_PWR_LVL_P3, ESP_BLE_PWR_TYPE_CONN); // lower once connected
NimBLEDevice::init("PetPulse-A3F2");
```

| Level             | dBm             |
| ----------------- | --------------- |
| `ESP_PWR_LVL_N12` | -12 dBm         |
| `ESP_PWR_LVL_N9`  | -9 dBm          |
| `ESP_PWR_LVL_N6`  | -6 dBm          |
| `ESP_PWR_LVL_N3`  | -3 dBm          |
| `ESP_PWR_LVL_N0`  | 0 dBm (default) |
| `ESP_PWR_LVL_P3`  | +3 dBm          |
| `ESP_PWR_LVL_P6`  | +6 dBm          |
| `ESP_PWR_LVL_P9`  | +9 dBm          |

enum class BNO08xAccuracy : uint8_t
{
UNRELIABLE = 0,
LOW = 1,
MED = 2,
HIGH = 3,
UNDEFINED = 4
};

enum class BNO08xActivity : uint8_t
{
UNKNOWN = 0, // 0 = unknown
IN_VEHICLE = 1, // 1 = in vehicle
ON_BICYCLE = 2, // 2 = on bicycle
ON_FOOT = 3, // 3 = on foot
STILL = 4, // 4 = still
TILTING = 5, // 5 = tilting
WALKING = 6, // 6 = walking
RUNNING = 7, // 7 = running
ON_STAIRS = 8, // 8 = on stairs
UNDEFINED = 9 // used for unit tests
};

```JSON
 Services:  {
  "id": "18ef74ce-c224-d7c1-a0e5-38756a0b967b",
  "services": [
    {
      "uuid": "792c45e0-7b95-4a4d-8bc2-6d04809bb406"
    },
    {
      "uuid": "792c45e1-7b95-4a4d-8bc2-6d04809bb406"
    },
    {
      "uuid": "181a"
    },
    {
      "uuid": "180f"
    },
    {
      "uuid": "1805"
    }
  ],
  "characteristics": [
    {
      "service": "792c45e0-7b95-4a4d-8bc2-6d04809bb406",
      "characteristic": "792c45e2-7b95-4a4d-8bc2-6d04809bb406",
      "properties": {
        "Notify": "Notify"
      },
      "isNotifying": false,
      "descriptors": [
        {
          "uuid": "2902"
        }
      ]
    },
    {
      "properties": {
        "Notify": "Notify"
      },
      "descriptors": [
        {
          "uuid": "2902"
        }
      ],
      "service": "792c45e0-7b95-4a4d-8bc2-6d04809bb406",
      "characteristic": "792c45e3-7b95-4a4d-8bc2-6d04809bb406",
      "isNotifying": false
    },
    {
      "isNotifying": false,
      "descriptors": [
        {
          "uuid": "2902"
        }
      ],
      "service": "792c45e1-7b95-4a4d-8bc2-6d04809bb406",
      "characteristic": "792c45e4-7b95-4a4d-8bc2-6d04809bb406",
      "properties": {
        "Notify": "Notify"
      }
    },
    {
      "descriptors": [
        {
          "uuid": "2902"
        }
      ],
      "isNotifying": false,
      "properties": {
        "Notify": "Notify"
      },
      "service": "792c45e1-7b95-4a4d-8bc2-6d04809bb406",
      "characteristic": "792c45e5-7b95-4a4d-8bc2-6d04809bb406"
    },
    {
      "service": "792c45e1-7b95-4a4d-8bc2-6d04809bb406",
      "descriptors": [
        {
          "uuid": "2902"
        }
      ],
      "properties": {
        "Notify": "Notify"
      },
      "isNotifying": false,
      "characteristic": "792c45e6-7b95-4a4d-8bc2-6d04809bb406"
    },
    {
      "service": "792c45e1-7b95-4a4d-8bc2-6d04809bb406",
      "characteristic": "792c45e7-7b95-4a4d-8bc2-6d04809bb406",
      "descriptors": [
        {
          "uuid": "2902"
        }
      ],
      "properties": {
        "Notify": "Notify"
      },
      "isNotifying": false
    },
    {
      "isNotifying": false,
      "properties": {
        "Notify": "Notify"
      },
      "descriptors": [
        {
          "uuid": "2902"
        }
      ],
      "service": "792c45e1-7b95-4a4d-8bc2-6d04809bb406",
      "characteristic": "792c45e8-7b95-4a4d-8bc2-6d04809bb406"
    },
    {
      "service": "181a",
      "isNotifying": false,
      "characteristic": "2a6e",
      "properties": {
        "Notify": "Notify"
      },
      "descriptors": [
        {
          "uuid": "2902"
        }
      ]
    },
    {
      "properties": {
        "Notify": "Notify"
      },
      "service": "181a",
      "descriptors": [
        {
          "uuid": "2902"
        }
      ],
      "isNotifying": false,
      "characteristic": "2a6f"
    },
    {
      "service": "180f",
      "descriptors": [
        {
          "uuid": "2902"
        }
      ],
      "properties": {
        "Indicate": "Indicate",
        "Notify": "Notify"
      },
      "isNotifying": false,
      "characteristic": "2bed"
    },
    {
      "properties": {
        "Notify": "Notify",
        "Indicate": "Indicate"
      },
      "descriptors": [
        {
          "uuid": "2902"
        }
      ],
      "isNotifying": false,
      "service": "180f",
      "characteristic": "2bf0"
    },
    {
      "properties": {
        "Indicate": "Indicate",
        "Notify": "Notify"
      },
      "service": "180f",
      "characteristic": "2bee",
      "isNotifying": false,
      "descriptors": [
        {
          "uuid": "2902"
        }
      ]
    },
    {
      "isNotifying": false,
      "properties": {
        "Notify": "Notify",
        "Indicate": "Indicate"
      },
      "descriptors": [
        {
          "uuid": "2902"
        }
      ],
      "characteristic": "2bea",
      "service": "180f"
    },
    {
      "properties": {
        "WriteWithoutResponse": "WriteWithoutResponse"
      },
      "service": "1805",
      "isNotifying": false,
      "characteristic": "2a2b"
    }
  ],
  "name": "PetPulse-0001"
```

```json
 LOG  Subscribed to activity notifications
 LOG  sendCurrentTime:  2026-03-02T19:43:38.001Z
 LOG  stepCount:  {"accuracy": 3, "latency": 10614, "steps": 0, "timestamp": 2026-03-02T19:43:43.877Z}
 LOG  stepCount:  {"accuracy": 3, "latency": 35519, "steps": 0, "timestamp": 2026-03-02T19:43:43.972Z}
 LOG  stepCount:  {"accuracy": 3, "latency": 17123, "steps": 0, "timestamp": 2026-03-02T19:43:44.067Z}
 LOG  stepCount:  {"accuracy": 3, "latency": 25738, "steps": 0, "timestamp": 2026-03-02T19:43:44.162Z}
 LOG  stepCount:  {"accuracy": 3, "latency": 7476, "steps": 0, "timestamp": 2026-03-02T19:43:44.257Z}
 LOG  stepCount:  {"accuracy": 3, "latency": 18620, "steps": 0, "timestamp": 2026-03-02T19:43:44.352Z}
 LOG  stepCount:  {"accuracy": 3, "latency": 10015, "steps": 0, "timestamp": 2026-03-02T19:43:44.447Z}
 LOG  stepCount:  {"accuracy": 3, "latency": 10810, "steps": 0, "timestamp": 2026-03-02T19:43:44.541Z}
 LOG  stepCount:  {"accuracy": 3, "latency": 52619, "steps": 0, "timestamp": 2026-03-02T19:43:44.636Z}
 LOG  stepCount:  {"accuracy": 3, "latency": 65500, "steps": 0, "timestamp": 2026-03-02T19:43:44.731Z}
 LOG  activityClass:  {"accuracy": 3, "activityClass": 0, "confidenceArray": [91, 2, 0, 3, 4, 0, 1, 2, 0, 0], "timestamp": 2026-03-02T19:43:44.772Z}
 LOG  activityClass:  {"accuracy": 3, "activityClass": 0, "confidenceArray": [77, 2, 0, 6, 15, 0, 3, 3, 0, 0], "timestamp": 2026-03-02T19:43:45.759Z}
 LOG  activityClass:  {"accuracy": 3, "activityClass": 0, "confidenceArray": [55, 4, 0, 6, 35, 0, 3, 3, 0, 0], "timestamp": 2026-03-02T19:43:46.747Z}
 LOG  activityClass:  {"accuracy": 3, "activityClass": 4, "confidenceArray": [30, 4, 0, 6, 60, 0, 3, 3, 0, 0], "timestamp": 2026-03-02T19:43:47.735Z}
 LOG  activityClass:  {"accuracy": 3, "activityClass": 4, "confidenceArray": [14, 3, 0, 4, 79, 0, 2, 2, 0, 0], "timestamp": 2026-03-02T19:43:48.723Z}
 LOG  activityClass:  {"accuracy": 3, "activityClass": 4, "confidenceArray": [6, 2, 0, 2, 90, 0, 1, 1, 0, 0], "timestamp": 2026-03-02T19:43:49.711Z}
 LOG  activityClass:  {"accuracy": 3, "activityClass": 4, "confidenceArray": [3, 1, 0, 2, 94, 0, 1, 1, 0, 0], "timestamp": 2026-03-02T19:43:50.699Z}
 LOG  activityClass:  {"accuracy": 3, "activityClass": 4, "confidenceArray": [1, 1, 0, 2, 96, 0, 1, 1, 0, 0], "timestamp": 2026-03-02T19:43:51.687Z}
 LOG  activityClass:  {"accuracy": 3, "activityClass": 4, "confidenceArray": [1, 1, 0, 2, 96, 0, 1, 1, 0, 0], "timestamp": 2026-03-02T19:43:52.675Z}
 LOG  activityClass:  {"accuracy": 3, "activityClass": 4, "confidenceArray": [1, 1, 0, 2, 96, 0, 1, 1, 0, 0], "timestamp": 2026-03-02T19:43:53.663Z}
```
