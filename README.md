# ESP32 32E Relay Board - Firebasket Ignition System Firmware

Automatic firebasket ignition firmware designed for ESP32 32E Wroom Relay Boards. Operates on system power-up, executes a safety-checked ignition sequence with propane gas pre-flow, high-voltage spark control, synchronized spa/pool light interruption pulses, infrared flame sensing, 30-second purge delays, and an automatic 5-attempt (1 initial + 4 retries) safety lockout.

---

## ⚡ Hardware Wiring & Pinout Guide

| ESP32 Pin | Connected Component | Relay # | Logic Level | Description |
|---|---|---|---|---|
| **GPIO 25** | Gas Solenoid Valve Relay | **Relay 1** | Active HIGH (`+3.3V`) | Closes/opens spring-loaded ball valve for propane gas flow |
| **GPIO 27** | Piezo Beeper Relay | **Relay 2** | Active HIGH (`+3.3V`) | Warning beeper audio output relay |
| **GPIO 26** | HV Spark Ignitor Relay | **Relay 3** | Active HIGH (`+3.3V`) | High-voltage spark ignition driver relay |
| **GPIO 12** | Spa Light Relay | **Relay 4** | Active HIGH (`+3.3V`) | Spa light control relay (pulsed in sync with Relay 2) |
| **GPIO 14** | Pool Light Relay | **Relay 5** | Active HIGH (`+3.3V`) | Pool light control relay (pulsed in sync with Relay 2) |
| **GPIO 33** | IR Flame Sensor Data Pin | - | Active HIGH / LOW | Digital output pin from Infrared Flame Sensor module |
| **GPIO 2** | Onboard Status LED | - | Active HIGH | Visual system running state indicator |
| **GND** | System Ground | - | Ground (`0V`) | Common ground across relay module & sensors |
| **5V / VIN** | System Power Input | - | `5V DC` | Power supply input (System boots & runs on power-up) |

> 📌 **Note on Relay Modules**: Relay pins use positive logic (`HIGH = Closed / Energized`). If your specific ESP32 relay board uses different GPIO numbers, easily update the `#define PIN_RELAY_...` definitions at the top of [`src/main.cpp`](file:///c:/Projects/Firebasket%20Ignition/src/main.cpp).

---

## ⏱️ Ignition Sequence Breakdown & Timing

```
                       Power Applied (Boot)
                                │
                                ▼
         ┌──────────────────────────────────────────────┐
         │ 1. Gas Pre-Flow & Initial Beeping (10 Sec)   │
         │  • Relay 1 (Gas Valve) CLOSED -> Propane ON  │
         │  • Relay 2 (Beeper) pulses 1x/sec (10 times) │
         │  • Relays 4 & 5 (Spa/Pool Lights) pulse in   │
         │    sync with Relay 2 to interrupt lights    │
         └──────────────────────┬───────────────────────┘
                                │
                                ▼
         ┌──────────────────────────────────────────────┐
         │ 2. Spark Ignition & Rapid Pulse (10 Sec Max) │
         │  • Relay 3 (Spark Ignitor) CLOSED -> Spark ON│
         │  • Relay 1 (Gas Valve) stays CLOSED          │
         │  • Relay 2 (Beeper) pulses 2x/sec (2 Hz)     │
         │  • Relays 4 & 5 pulse 2x/sec (2 Hz) in sync  │
         │  • IR Flame Sensor monitored continuously    │
         └──────────────┬───────────────┬───────────────┘
                        │               │
      (Flame Detected)  │               │  (No Flame after 10s)
                        ▼               ▼
     ┌───────────────────────┐    ┌───────────────────────────────────┐
     │ 3. RUNNING STATE      │    │ 4. SAFETY PURGE DELAY (30 Sec)    │
     │  • Relay 3 (Spark) OFF│    │  • Relays 1, 2, 3, 4, 5 ALL OFF   │
     │  • Relays 2,4,5 OFF   │    │  • Wait 30s safety purge delay    │
     │  • Relay 1 (Gas) ON   │    │  • Retry sequence (Up to 5 total) │
     └───────────────────────┘    └─────────────────┬─────────────────┘
                                                    │
                                         (5th Attempt Fails)
                                                    │
                                                    ▼
                                          ┌──────────────────┐
                                          │ 5. LOCKOUT STATE │
                                          │  • All relays OFF│
                                          │  • Halt sequence │
                                          │  • Reboot needed │
                                          └──────────────────┘
```

### 1. Gas Pre-Flow & Initial Beeping (10 Seconds)
- Power applied -> ESP32 boots up.
- **Relay 1 (Gas Valve)** closes (turns ON), opening the spring-loaded ball valve to allow propane gas flow.
- Simultaneously, **Relay 2 (Beeper)** pulses ON/OFF once per second (1 Hz) 10 times (500ms ON / 500ms OFF).
- For every ON pulse of Relay 2, **Relay 4 (Spa Light)** and **Relay 5 (Pool Light)** also pulse ON/OFF simultaneously, interrupting the spa and pool lights.

### 2. Spark Ignition & Rapid Pulse (10 Seconds Max)
- After 10 beeps are complete, **Relay 3 (Spark Ignitor)** closes (turns ON) to energize spark ignition.
- Relay 1 remains CLOSED (gas ON).
- Simultaneously, **Relay 2 (Beeper)** pulses ON/OFF twice per second (2 Hz: 250ms ON / 250ms OFF).
- **Relays 4 & 5 (Spa & Pool Lights)** pulse ON/OFF at 2 Hz in sync with Relay 2.
- Continuous monitoring of Infrared Flame Sensor during this 10-second window.

### 3. Flame Sensor Outcomes
- **Flame Detected**: Relay 3 (Ignitor) turns OFF immediately. Relays 2, 4, 5 turn OFF. Relay 1 (Gas Valve) remains ON in **RUNNING** state. (If flame is lost during operation, gas valve closes instantly).
- **No Flame Detected**: If no flame after 10 seconds, Relay 1 (Gas Valve), Relay 3 (Ignitor), Relay 2 (Beeper), and Relays 4 & 5 (Lights) turn OFF.

### 4. Retries (30-Second Purge) & 5-Attempt Lockout
- System waits **30 seconds** with all relays OFF (Safety Purge Delay).
- The sequence repeats after 30 seconds and repeats 4 more times (**5 total attempts**: 1 initial + 4 retries).
- If all 5 attempts fail to detect a flame, the system enters **LOCKOUT** state (all relays OFF) and will not attempt ignition again until power is removed and restored to reboot the ESP32.

---

## 🛠️ How to Build and Upload

### PlatformIO (Recommended)
1. Open folder in Visual Studio Code with PlatformIO extension.
2. Connect ESP32 board via USB.
3. Click **Build** (Checkmark) and **Upload** (Arrow icon).
4. Open Serial Monitor at **115200 baud** to view real-time diagnostics logs.

### Arduino IDE
1. Open `src/main.cpp` or copy its content into Arduino IDE.
2. Select Board: **ESP32 Dev Module**.
3. Select Port: (Your ESP32 COM port).
4. Click **Upload**.
