/*
 * ESP32 32E Wroom Relay Board - Firebasket Ignition System Firmware
 * 
 * Hardware Sequence & Operation:
 * - Boot: Power applied -> ESP32 boots up automatically and starts ignition sequence.
 * 
 * - Step 1 (Gas Pre-Flow & Initial Beeping):
 *   * Relay 1 (Propane Gas Valve) closes (ON) to allow propane gas to flow to the burner.
 *   * Simultaneously, Relay 2 (Piezo Beeper) pulses ON/OFF once per second (1 Hz) 10 times (10 seconds total).
 *   * Synchronous Light Interruption: For every ON pulse of Relay 2, Relay 4 (Spa Light) and 
 *     Relay 5 (Pool Light) also pulse ON/OFF simultaneously.
 * 
 * - Step 2 (Spark Ignition & Rapid Beeping / Light Interruption):
 *   * After 10 beeps (10s), Relay 3 (Spark Ignitor) closes (ON) to turn on high-voltage spark ignition.
 *   * Simultaneously, Relay 2 (Beeper) pulses ON/OFF twice per second (2 Hz).
 *   * Relays 4 & 5 (Spa & Pool Lights) pulse ON/OFF in sync with Relay 2 (2 Hz).
 *   * AD8495 K-Type Thermocouple Flame Sensor monitored continuously for up to 10 seconds.
 * 
 * - Step 3 (Flame Detection Outcome):
 *   * FLAME DETECTED: Relay 3 (Ignitor) OFF, Relays 2, 4, 5 OFF. Relay 1 (Gas Valve) stays ON (RUNNING state).
 *     Relay 6 (Flame Detected Relay) closes (ON) whenever the thermocouple detects flame.
 *   * NO FLAME DETECTED (after 10s): Relay 1 (Gas Valve) OFF, Relay 3 (Ignitor) OFF, Relays 2, 4, 5 OFF, Relay 6 OFF.
 * 
 * - Step 4 (Retry & Lockout Logic):
 *   * If ignition fails, system waits 30 seconds (Safety Purge Delay) with all relays OFF.
 *   * The sequence repeats after 30 seconds and repeats 4 more times (5 total attempts: 1 initial + 4 retries).
 *   * If all 5 attempts fail, the system enters LOCKOUT state and does not repeat anymore until reboot 
 *     (power cycling the ESP32).
 */

#include <Arduino.h>

// ============================================================================
// HARDWARE PIN DEFINITIONS & CONFIGURATION
// ============================================================================

// Relay Output Pins (Active HIGH positive trigger logic)
#define PIN_RELAY_1_GAS       32  // Relay 1: Propane Gas Valve Relay (GPIO 32)
#define PIN_RELAY_2_BEEPER    33  // Relay 2: Piezo Beeper Warning Relay (GPIO 33)
#define PIN_RELAY_3_IGNITOR   25  // Relay 3: Spark Ignitor High-Voltage Relay (GPIO 25)
#define PIN_RELAY_4_SPA_LIGHT 26  // Relay 4: Spa Light Control Relay (GPIO 26)
#define PIN_RELAY_5_POOL_LIGHT 27 // Relay 5: Pool Light Control Relay (GPIO 27)
#define PIN_RELAY_6_FLAME     12  // Relay 6: Dedicated Flame Detection Relay (GPIO 12)

// Unused Relays on eMariete ESP32-WROOM-32e 8-Relay Board (Explicitly held OFF for safety)
#define PIN_RELAY_7_UNUSED    13  // Relay 7: Unused (GPIO 13)
#define PIN_RELAY_8_UNUSED    23  // Relay 8: Unused (GPIO 23)
#define PIN_STATUS_LED        2   // Onboard Status Indicator LED

// Input Sensor Pins
#define PIN_FLAME_SENSOR      34  // AD8495 Analog K-Type Thermocouple Input (GPIO 34 / ADC1_CH6)

// ============================================================================
// AD8495 THERMOCOUPLE & FLAME SENSING PARAMETERS
// ============================================================================
#define AD8495_REF_VOLTAGE_V      0.0     // Reference Voltage (0.0V with REF pin tied to GND)
#define AD8495_GAIN_V_PER_C       0.005   // Transfer function gain: 5 mV/°C (0.005 V/°C)
#define ADC_VREF                  3.3     // ESP32 ADC Reference Voltage (3.3V)
#define ADC_RESOLUTION            4095.0  // ESP32 12-bit ADC Max Value (0-4095)
#define ADC_SAMPLE_COUNT          16      // Multi-sample averaging count to filter spark EMI noise

// Flame Temperature Thresholds & Hysteresis (°C)
#define FLAME_TEMP_THRESHOLD_C    150.0   // Temperature required to confirm flame presence (°C)
#define FLAME_TEMP_HYSTERESIS_C   20.0    // Temperature hysteresis for flame loss (Extinguish < 130°C)

// Relay Logic (Positive Control Logic: HIGH = Closed / Energized, LOW = Open / De-energized)
#define RELAY_ON   HIGH
#define RELAY_OFF  LOW

// Timing Parameters (in milliseconds)
#define PREFLOW_BEEP_COUNT       10      // 10 beeps during gas pre-flow
#define PREFLOW_BEEP_PERIOD_MS   1000    // 1 beep per second (500ms ON / 500ms OFF = 10s total)
#define IGNITION_TIMEOUT_MS      10000   // 10-second max spark ignition attempt window
#define IGNITION_BEEP_PERIOD_MS  500     // 2 beeps per second (250ms ON / 250ms OFF)
#define PURGE_WAIT_MS            30000   // 30-second delay between ignition attempts
#define MAX_ATTEMPTS             5       // 1 initial attempt + 4 retries = 5 total attempts

// ============================================================================
// SYSTEM STATES
// ============================================================================
enum IgnitionState {
    STATE_PREFLOW_BEEP,      // Relay 1 ON, Relays 2/4/5 pulse 1Hz for 10s (10 beeps)
    STATE_IGNITION_ATTEMPT,  // Relay 1 & 3 ON, Relays 2/4/5 pulse 2Hz, check flame 10s max
    STATE_RUNNING,           // Flame detected! Relay 1 & 6 ON, all other relays OFF
    STATE_RETRY_WAIT,        // Attempt failed: All relays OFF, wait 30s before retry
    STATE_LOCKOUT            // 5 attempts failed: All relays OFF, halt until reboot
};

// Global State Variables
IgnitionState currentState = STATE_PREFLOW_BEEP;
uint8_t currentAttempt = 1;

uint32_t stateStartTime = 0;
bool isFlameDetected = false;

// Function Declarations
void transitionToState(IgnitionState newState);
void handlePreflowBeepState();
void handleIgnitionAttemptState();
void handleRunningState();
void handleRetryWaitState();
void handleLockoutState();
bool readDebouncedFlameSensor();
float getThermocoupleTemperatureC(uint16_t *outRawAdc = NULL, float *outVoltage = NULL);
void setBeeperAndLights(bool state);
void setAllRelaysOff();
const char* getStateName(IgnitionState state);

// ============================================================================
// SETUP & HARDWARE INITIALIZATION
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(500); // Short delay for serial monitor initialization

    Serial.println();
    Serial.println(F("=========================================================="));
    Serial.println(F("   ESP32 32E RELAY BOARD - FIREBASKET IGNITION FIRMWARE   "));
    Serial.println(F("   (AD8495 K-Type Thermocouple & Flame Relay Enabled)    "));
    Serial.println(F("=========================================================="));

    // Safely set all relay pins LOW before configuring pin modes
    digitalWrite(PIN_RELAY_1_GAS, RELAY_OFF);
    digitalWrite(PIN_RELAY_2_BEEPER, RELAY_OFF);
    digitalWrite(PIN_RELAY_3_IGNITOR, RELAY_OFF);
    digitalWrite(PIN_RELAY_4_SPA_LIGHT, RELAY_OFF);
    digitalWrite(PIN_RELAY_5_POOL_LIGHT, RELAY_OFF);
    digitalWrite(PIN_RELAY_6_FLAME, RELAY_OFF);
    digitalWrite(PIN_RELAY_7_UNUSED, RELAY_OFF);
    digitalWrite(PIN_RELAY_8_UNUSED, RELAY_OFF);
    digitalWrite(PIN_STATUS_LED, LOW);

    // Configure Pin Modes
    pinMode(PIN_RELAY_1_GAS, OUTPUT);
    pinMode(PIN_RELAY_2_BEEPER, OUTPUT);
    pinMode(PIN_RELAY_3_IGNITOR, OUTPUT);
    pinMode(PIN_RELAY_4_SPA_LIGHT, OUTPUT);
    pinMode(PIN_RELAY_5_POOL_LIGHT, OUTPUT);
    pinMode(PIN_RELAY_6_FLAME, OUTPUT);
    pinMode(PIN_RELAY_7_UNUSED, OUTPUT);
    pinMode(PIN_RELAY_8_UNUSED, OUTPUT);
    pinMode(PIN_STATUS_LED, OUTPUT);
    pinMode(PIN_FLAME_SENSOR, INPUT);

    Serial.printf("Pin Configuration (eMariete ESP32-WROOM-32e 8-Relay Board):\n");
    Serial.printf(" - Relay 1 (Gas Valve): GPIO %d\n", PIN_RELAY_1_GAS);
    Serial.printf(" - Relay 2 (Beeper):    GPIO %d\n", PIN_RELAY_2_BEEPER);
    Serial.printf(" - Relay 3 (Ignitor):   GPIO %d\n", PIN_RELAY_3_IGNITOR);
    Serial.printf(" - Relay 4 (Spa Light): GPIO %d\n", PIN_RELAY_4_SPA_LIGHT);
    Serial.printf(" - Relay 5 (Pool Light):GPIO %d\n", PIN_RELAY_5_POOL_LIGHT);
    Serial.printf(" - Relay 6 (Flame Relay):GPIO %d (Energized on Flame Detect)\n", PIN_RELAY_6_FLAME);
    Serial.printf(" - Relays 7 & 8:         GPIO %d, %d (Disabled/Unused)\n", 
                  PIN_RELAY_7_UNUSED, PIN_RELAY_8_UNUSED);
    Serial.printf(" - AD8495 Thermocouple: GPIO %d (ADC1_CH6 Analog Input)\n", PIN_FLAME_SENSOR);
    Serial.println(F("----------------------------------------------------------"));
    Serial.printf("Thermocouple Flame Sensing Parameters:\n");
    Serial.printf(" - Flame Temp Threshold:  %.1f °C\n", FLAME_TEMP_THRESHOLD_C);
    Serial.printf(" - Flame Loss Hysteresis: %.1f °C (Off below %.1f °C)\n", 
                  FLAME_TEMP_HYSTERESIS_C, FLAME_TEMP_THRESHOLD_C - FLAME_TEMP_HYSTERESIS_C);
    Serial.printf(" - AD8495 Transfer Gain:  5.0 mV / °C (VREF: %.1f V)\n", AD8495_REF_VOLTAGE_V);
    Serial.println(F("----------------------------------------------------------"));
    Serial.printf("Timing Parameters:\n");
    Serial.printf(" - Max Attempts: %d (1 initial + 4 retries)\n", MAX_ATTEMPTS);
    Serial.printf(" - Pre-Flow Duration: 10 seconds (10 beeps @ 1 Hz)\n");
    Serial.printf(" - Ignition Timeout: 10 seconds (@ 2 Hz beeps/light pulses)\n");
    Serial.printf(" - Retry Purge Wait: 30 seconds\n");
    Serial.println(F("=========================================================="));

    // Begin sequence on power boot (Attempt 1 of 5)
    currentAttempt = 1;
    transitionToState(STATE_PREFLOW_BEEP);
}

// ============================================================================
// MAIN EVENT LOOP
// ============================================================================
void loop() {
    switch (currentState) {
        case STATE_PREFLOW_BEEP:
            handlePreflowBeepState();
            break;

        case STATE_IGNITION_ATTEMPT:
            handleIgnitionAttemptState();
            break;

        case STATE_RUNNING:
            handleRunningState();
            break;

        case STATE_RETRY_WAIT:
            handleRetryWaitState();
            break;

        case STATE_LOCKOUT:
            handleLockoutState();
            break;
    }
}

// ============================================================================
// STATE MACHINE TRANSITIONS & HANDLING
// ============================================================================

/**
 * Handles state transition setup, timer resets, and initial relay states.
 */
void transitionToState(IgnitionState newState) {
    currentState = newState;
    stateStartTime = millis();

    Serial.printf("\n[STATE CHANGE] Attempt %d of %d -> Entering %s\n", 
                  currentAttempt, MAX_ATTEMPTS, getStateName(newState));

    switch (newState) {
        case STATE_PREFLOW_BEEP:
            // Relay 1 ON (Gas Valve open), all other relays initialized OFF
            digitalWrite(PIN_RELAY_1_GAS, RELAY_ON);
            digitalWrite(PIN_RELAY_3_IGNITOR, RELAY_OFF);
            digitalWrite(PIN_RELAY_6_FLAME, RELAY_OFF);
            setBeeperAndLights(false);
            digitalWrite(PIN_STATUS_LED, HIGH);
            Serial.println(F("Relay 1 (Gas Valve) CLOSED -> Propane flowing. Starting 10s pre-flow beeps..."));
            break;

        case STATE_IGNITION_ATTEMPT:
            // Relay 1 stays ON (Gas Valve), Relay 3 ON (Spark Ignitor)
            digitalWrite(PIN_RELAY_1_GAS, RELAY_ON);
            digitalWrite(PIN_RELAY_3_IGNITOR, RELAY_ON);
            setBeeperAndLights(false);
            Serial.println(F("Relay 3 (Spark Ignitor) CLOSED -> High-Voltage Spark ACTIVE (10s max timeout)."));
            break;

        case STATE_RUNNING:
            // Relay 1 stays ON (Gas Valve), Relay 6 ON (Flame Relay), Ignitor, Beeper, & Light relays OFF
            digitalWrite(PIN_RELAY_1_GAS, RELAY_ON);
            digitalWrite(PIN_RELAY_3_IGNITOR, RELAY_OFF);
            digitalWrite(PIN_RELAY_6_FLAME, RELAY_ON);
            setBeeperAndLights(false);
            digitalWrite(PIN_STATUS_LED, HIGH);
            Serial.println(F(">>> FLAME DETECTED! Relay 6 (Flame Relay) ON. Ignitor OFF, Gas Valve ON. RUNNING state. <<<"));
            break;

        case STATE_RETRY_WAIT:
            // Turn off all relays during 30s purge wait
            setAllRelaysOff();
            digitalWrite(PIN_STATUS_LED, LOW);
            Serial.printf("Ignition attempt failed. All relays OFF. Waiting 30-second purge cycle before retry...\n");
            break;

        case STATE_LOCKOUT:
            // Turn off all relays permanently until power reboot
            setAllRelaysOff();
            digitalWrite(PIN_STATUS_LED, LOW);
            Serial.println(F("=========================================================================="));
            Serial.println(F("!!! CRITICAL SAFETY LOCKOUT: 5 FAILED ATTEMPTS (1 INITIAL + 4 RETRIES).  !!!"));
            Serial.println(F("!!! SYSTEM HALTED. PLEASE REMOVE AND RESTORE POWER TO REBOOT ESP32.    !!!"));
            Serial.println(F("=========================================================================="));
            break;
    }
}

/**
 * STEP 1: Relay 1 ON (Propane Gas Valve Open).
 * Relay 2 (Beeper) pulses 10 times at 1 pulse per second (1 Hz: 500ms ON / 500ms OFF).
 * Relays 4 (Spa Light) and 5 (Pool Light) pulse ON/OFF in sync with Relay 2.
 * Total duration: 10 seconds.
 */
void handlePreflowBeepState() {
    uint32_t elapsed = millis() - stateStartTime;
    uint32_t totalBeepDuration = PREFLOW_BEEP_COUNT * PREFLOW_BEEP_PERIOD_MS; // 10,000 ms

    // Keep Relay 1 (Gas Valve) ON
    digitalWrite(PIN_RELAY_1_GAS, RELAY_ON);

    // Read Thermocouple continuously during pre-flow
    readDebouncedFlameSensor();

    if (elapsed < totalBeepDuration) {
        uint32_t cycleTime = elapsed % PREFLOW_BEEP_PERIOD_MS;
        // Pulse 500ms ON / 500ms OFF (1 Hz)
        bool pulseState = (cycleTime < (PREFLOW_BEEP_PERIOD_MS / 2));
        setBeeperAndLights(pulseState);
    } else {
        // 10 beeps complete -> turn off beeper & lights, transition to ignition attempt
        setBeeperAndLights(false);
        transitionToState(STATE_IGNITION_ATTEMPT);
    }
}

/**
 * STEP 2 & 3: Relay 3 ON (Spark Ignitor). Relay 1 stays ON (Gas Valve).
 * Relay 2 (Beeper) pulses ON/OFF twice per second (2 Hz: 250ms ON / 250ms OFF).
 * Relays 4 & 5 (Spa & Pool Lights) pulse ON/OFF in sync with Relay 2.
 * Monitor AD8495 Thermocouple for up to 10 seconds.
 */
void handleIgnitionAttemptState() {
    uint32_t elapsed = millis() - stateStartTime;

    // Pulse Beeper (Relay 2) and Lights (Relays 4 & 5) twice per second (2 Hz)
    uint32_t cycleTime = elapsed % IGNITION_BEEP_PERIOD_MS;
    bool pulseState = (cycleTime < (IGNITION_BEEP_PERIOD_MS / 2));
    setBeeperAndLights(pulseState);

    // Continuous AD8495 Thermocouple flame monitoring
    bool isFlame = readDebouncedFlameSensor();

    if (isFlame) {
        Serial.println(F("Flame signal verified by thermocouple during ignition window!"));
        transitionToState(STATE_RUNNING);
        return;
    }

    // Check for 10-second ignition timeout
    if (elapsed >= IGNITION_TIMEOUT_MS) {
        Serial.println(F("!!! 10-Second Ignition Timeout Reached - No Flame Detected !!!"));
        
        if (currentAttempt < MAX_ATTEMPTS) {
            transitionToState(STATE_RETRY_WAIT);
        } else {
            transitionToState(STATE_LOCKOUT);
        }
        return;
    }
}

/**
 * STEP 3 (Cont.): Flame detected state.
 * Maintains Relay 1 (Gas Valve) ON, Relay 6 (Flame Relay) ON, and all other relays OFF.
 * Continuously monitors AD8495 Thermocouple. If flame is lost, immediately shuts off gas valve & flame relay for safety.
 */
void handleRunningState() {
    bool isFlame = readDebouncedFlameSensor();

    if (!isFlame) {
        Serial.println(F("WARNING: FLAME LOST OR TEMPERATURE DROPPED BELOW THRESHOLD!"));
        setAllRelaysOff();
        
        if (currentAttempt < MAX_ATTEMPTS) {
            transitionToState(STATE_RETRY_WAIT);
        } else {
            transitionToState(STATE_LOCKOUT);
        }
    }
}

/**
 * STEP 4: 30-second Purge Wait delay between failed ignition attempts.
 * All relays remain OFF. After 30s, increments attempt counter and restarts sequence.
 */
void handleRetryWaitState() {
    uint32_t elapsed = millis() - stateStartTime;

    // Periodic countdown logging every 5 seconds
    static uint32_t lastLogTime = 0;
    if (millis() - lastLogTime >= 5000) {
        lastLogTime = millis();
        uint32_t remainingSec = (PURGE_WAIT_MS - elapsed) / 1000;
        Serial.printf("Purge delay in progress... %u seconds remaining before attempt %d\n", 
                      remainingSec, currentAttempt + 1);
    }

    if (elapsed >= PURGE_WAIT_MS) {
        currentAttempt++;
        Serial.printf("\nPurge delay complete. Starting Retry Attempt %d of %d...\n", currentAttempt, MAX_ATTEMPTS);
        transitionToState(STATE_PREFLOW_BEEP);
    }
}

/**
 * STEP 4 (Lockout): System locked out after 5 total failed attempts.
 * All relays OFF. System halts here until power is disconnected and re-applied to ESP32.
 */
void handleLockoutState() {
    // Pulse status LED slowly (100ms every 3 seconds) as visual indicator of lockout state
    uint32_t alarmCycle = millis() % 3000;
    digitalWrite(PIN_STATUS_LED, (alarmCycle < 100) ? HIGH : LOW);
}

// ============================================================================
// HELPER FUNCTIONS & THERMOCOUPLE READINGS
// ============================================================================

/**
 * Reads multi-sampled raw ADC from AD8495 Thermocouple on GPIO 34, converts to °C.
 */
float getThermocoupleTemperatureC(uint16_t *outRawAdc, float *outVoltage) {
    uint32_t adcSum = 0;
    for (int i = 0; i < ADC_SAMPLE_COUNT; i++) {
        adcSum += analogRead(PIN_FLAME_SENSOR);
        delayMicroseconds(50); // Short delay between samples
    }
    uint16_t avgAdc = adcSum / ADC_SAMPLE_COUNT;
    float voltage = (avgAdc / ADC_RESOLUTION) * ADC_VREF;
    float tempC = (voltage - AD8495_REF_VOLTAGE_V) / AD8495_GAIN_V_PER_C;

    if (outRawAdc) *outRawAdc = avgAdc;
    if (outVoltage) *outVoltage = voltage;

    return tempC;
}

/**
 * Reads AD8495 Thermocouple temperature, applies hysteresis filtering,
 * energizes Relay 6 when flame is detected, and returns boolean flame status.
 */
bool readDebouncedFlameSensor() {
    uint16_t rawAdc = 0;
    float voltage = 0.0;
    float tempC = getThermocoupleTemperatureC(&rawAdc, &voltage);

    // Apply Hysteresis
    if (!isFlameDetected) {
        if (tempC >= FLAME_TEMP_THRESHOLD_C) {
            isFlameDetected = true;
        }
    } else {
        if (tempC < (FLAME_TEMP_THRESHOLD_C - FLAME_TEMP_HYSTERESIS_C)) {
            isFlameDetected = false;
        }
    }

    // Energize / De-energize Relay 6 based on flame presence
    digitalWrite(PIN_RELAY_6_FLAME, isFlameDetected ? RELAY_ON : RELAY_OFF);

    // Diagnostics Serial logging (throttled to once per second)
    static uint32_t lastPrintTime = 0;
    if (millis() - lastPrintTime >= 1000) {
        lastPrintTime = millis();
        Serial.printf("[AD8495 SENSOR] Temp: %.1f °C | Volts: %.3f V | ADC: %u | Flame: %s | Relay 6: %s\n",
                      tempC, voltage, rawAdc, 
                      isFlameDetected ? "DETECTED" : "NO FLAME",
                      isFlameDetected ? "ON (Energized)" : "OFF");
    }

    return isFlameDetected;
}

/**
 * Controls Relay 2 (Beeper), Relay 4 (Spa Light), and Relay 5 (Pool Light) together.
 * For every "ON" pulse of Beeper Relay 2, Spa Light Relay 4 and Pool Light Relay 5 also pulse.
 */
void setBeeperAndLights(bool state) {
    uint8_t relayState = state ? RELAY_ON : RELAY_OFF;
    digitalWrite(PIN_RELAY_2_BEEPER, relayState);
    digitalWrite(PIN_RELAY_4_SPA_LIGHT, relayState);
    digitalWrite(PIN_RELAY_5_POOL_LIGHT, relayState);
}

/**
 * De-energizes all relays (including Relay 6 Flame Relay & unused relays 7, 8) to a safe OFF state.
 */
void setAllRelaysOff() {
    digitalWrite(PIN_RELAY_1_GAS, RELAY_OFF);
    digitalWrite(PIN_RELAY_2_BEEPER, RELAY_OFF);
    digitalWrite(PIN_RELAY_3_IGNITOR, RELAY_OFF);
    digitalWrite(PIN_RELAY_4_SPA_LIGHT, RELAY_OFF);
    digitalWrite(PIN_RELAY_5_POOL_LIGHT, RELAY_OFF);
    digitalWrite(PIN_RELAY_6_FLAME, RELAY_OFF);
    digitalWrite(PIN_RELAY_7_UNUSED, RELAY_OFF);
    digitalWrite(PIN_RELAY_8_UNUSED, RELAY_OFF);
    isFlameDetected = false;
}

/**
 * Returns human-readable state strings for serial monitor diagnostics output.
 */
const char* getStateName(IgnitionState state) {
    switch (state) {
        case STATE_PREFLOW_BEEP:     return "PREFLOW_BEEP (Step 1: Gas ON, 10 Beeps @ 1Hz, Lights Pulse)";
        case STATE_IGNITION_ATTEMPT: return "IGNITION_ATTEMPT (Step 2: Spark ON, Beeps/Lights @ 2Hz, 10s Thermocouple Check)";
        case STATE_RUNNING:          return "RUNNING (Step 3: Flame Detected, Gas ON, Relay 6 ON, Ignitor/Beeper OFF)";
        case STATE_RETRY_WAIT:       return "RETRY_WAIT (Step 4: All Relays OFF, 30s Safety Purge Delay)";
        case STATE_LOCKOUT:          return "LOCKOUT (5 Attempts Failed - System Halted, Reboot Required)";
        default:                     return "UNKNOWN";
    }
}

