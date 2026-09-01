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
 *   * Flame sensor monitored continuously for up to 10 seconds.
 * 
 * - Step 3 (Flame Detection Outcome):
 *   * FLAME DETECTED: Relay 3 (Ignitor) OFF, Relays 2, 4, 5 OFF. Relay 1 (Gas Valve) stays ON (RUNNING state).
 *   * NO FLAME DETECTED (after 10s): Relay 1 (Gas Valve) OFF, Relay 3 (Ignitor) OFF, Relays 2, 4, 5 OFF.
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
#define PIN_RELAY_1_GAS       25  // Relay 1: Propane Gas Valve Relay
#define PIN_RELAY_2_BEEPER    27  // Relay 2: Piezo Beeper Warning Relay
#define PIN_RELAY_3_IGNITOR   26  // Relay 3: Spark Ignitor High-Voltage Relay
#define PIN_RELAY_4_SPA_LIGHT 12  // Relay 4: Spa Light Control Relay
#define PIN_RELAY_5_POOL_LIGHT 14 // Relay 5: Pool Light Control Relay
#define PIN_STATUS_LED        2   // Onboard Status Indicator LED

// Input Sensor Pins
#define PIN_FLAME_SENSOR      33  // Infrared (IR) Flame Sensor Digital Output

// Flame Sensor Signal Polarity
// Set to HIGH if sensor outputs HIGH when flame is present.
#define FLAME_DETECTED_STATE  HIGH

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
#define FLAME_DEBOUNCE_MS        150     // Continuous flame detection debounce filter (ms)

// ============================================================================
// SYSTEM STATES
// ============================================================================
enum IgnitionState {
    STATE_PREFLOW_BEEP,      // Relay 1 ON, Relays 2/4/5 pulse 1Hz for 10s (10 beeps)
    STATE_IGNITION_ATTEMPT,  // Relay 1 & 3 ON, Relays 2/4/5 pulse 2Hz, check flame 10s max
    STATE_RUNNING,           // Flame detected! Relay 1 ON, all other relays OFF
    STATE_RETRY_WAIT,        // Attempt failed: All relays OFF, wait 30s before retry
    STATE_LOCKOUT            // 5 attempts failed: All relays OFF, halt until reboot
};

// Global State Variables
IgnitionState currentState = STATE_PREFLOW_BEEP;
uint8_t currentAttempt = 1;

uint32_t stateStartTime = 0;
uint32_t flameDetectStartTime = 0;
bool flameDetecting = false;

// Function Declarations
void transitionToState(IgnitionState newState);
void handlePreflowBeepState();
void handleIgnitionAttemptState();
void handleRunningState();
void handleRetryWaitState();
void handleLockoutState();
bool readDebouncedFlameSensor();
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
    Serial.println(F("=========================================================="));

    // Safely set all relay pins LOW before configuring pin modes
    digitalWrite(PIN_RELAY_1_GAS, RELAY_OFF);
    digitalWrite(PIN_RELAY_2_BEEPER, RELAY_OFF);
    digitalWrite(PIN_RELAY_3_IGNITOR, RELAY_OFF);
    digitalWrite(PIN_RELAY_4_SPA_LIGHT, RELAY_OFF);
    digitalWrite(PIN_RELAY_5_POOL_LIGHT, RELAY_OFF);
    digitalWrite(PIN_STATUS_LED, LOW);

    // Configure Pin Modes
    pinMode(PIN_RELAY_1_GAS, OUTPUT);
    pinMode(PIN_RELAY_2_BEEPER, OUTPUT);
    pinMode(PIN_RELAY_3_IGNITOR, OUTPUT);
    pinMode(PIN_RELAY_4_SPA_LIGHT, OUTPUT);
    pinMode(PIN_RELAY_5_POOL_LIGHT, OUTPUT);
    pinMode(PIN_STATUS_LED, OUTPUT);
    pinMode(PIN_FLAME_SENSOR, INPUT);

    Serial.printf("Pin Configuration:\n");
    Serial.printf(" - Relay 1 (Gas Valve): GPIO %d\n", PIN_RELAY_1_GAS);
    Serial.printf(" - Relay 2 (Beeper):    GPIO %d\n", PIN_RELAY_2_BEEPER);
    Serial.printf(" - Relay 3 (Ignitor):   GPIO %d\n", PIN_RELAY_3_IGNITOR);
    Serial.printf(" - Relay 4 (Spa Light): GPIO %d\n", PIN_RELAY_4_SPA_LIGHT);
    Serial.printf(" - Relay 5 (Pool Light):GPIO %d\n", PIN_RELAY_5_POOL_LIGHT);
    Serial.printf(" - IR Flame Sensor:     GPIO %d\n", PIN_FLAME_SENSOR);
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
    flameDetecting = false;

    Serial.printf("\n[STATE CHANGE] Attempt %d of %d -> Entering %s\n", 
                  currentAttempt, MAX_ATTEMPTS, getStateName(newState));

    switch (newState) {
        case STATE_PREFLOW_BEEP:
            // Relay 1 ON (Gas Valve open), all other relays initialized OFF
            digitalWrite(PIN_RELAY_1_GAS, RELAY_ON);
            digitalWrite(PIN_RELAY_3_IGNITOR, RELAY_OFF);
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
            // Relay 1 stays ON (Gas Valve), Ignitor, Beeper, & Light relays OFF
            digitalWrite(PIN_RELAY_1_GAS, RELAY_ON);
            digitalWrite(PIN_RELAY_3_IGNITOR, RELAY_OFF);
            setBeeperAndLights(false);
            digitalWrite(PIN_STATUS_LED, HIGH);
            Serial.println(F(">>> FLAME DETECTED AND STABLE! Ignitor OFF, Gas Valve ON. System in RUNNING state. <<<"));
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
 * Monitor IR Flame Sensor for up to 10 seconds.
 */
void handleIgnitionAttemptState() {
    uint32_t elapsed = millis() - stateStartTime;

    // Pulse Beeper (Relay 2) and Lights (Relays 4 & 5) twice per second (2 Hz)
    uint32_t cycleTime = elapsed % IGNITION_BEEP_PERIOD_MS;
    bool pulseState = (cycleTime < (IGNITION_BEEP_PERIOD_MS / 2));
    setBeeperAndLights(pulseState);

    // Continuous IR Flame Sensor monitoring
    bool isFlame = readDebouncedFlameSensor();

    if (isFlame) {
        Serial.println(F("Flame signal verified by sensor during ignition window!"));
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
 * Maintains Relay 1 (Gas Valve) ON and all other relays OFF.
 * Continuously monitors IR Flame sensor. If flame is lost, immediately shuts off gas valve for safety.
 */
void handleRunningState() {
    bool isFlame = readDebouncedFlameSensor();

    if (!isFlame) {
        Serial.println(F("WARNING: FLAME LOST DURING BURNER OPERATION!"));
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
// HELPER FUNCTIONS
// ============================================================================

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
 * De-energizes all 5 relays to a safe OFF (open) state.
 */
void setAllRelaysOff() {
    digitalWrite(PIN_RELAY_1_GAS, RELAY_OFF);
    digitalWrite(PIN_RELAY_2_BEEPER, RELAY_OFF);
    digitalWrite(PIN_RELAY_3_IGNITOR, RELAY_OFF);
    digitalWrite(PIN_RELAY_4_SPA_LIGHT, RELAY_OFF);
    digitalWrite(PIN_RELAY_5_POOL_LIGHT, RELAY_OFF);
}

/**
 * Reads IR Flame Sensor with software debouncing to prevent false triggers from spark EMI noise.
 */
bool readDebouncedFlameSensor() {
    bool rawState = (digitalRead(PIN_FLAME_SENSOR) == FLAME_DETECTED_STATE);

    if (rawState) {
        if (!flameDetecting) {
            flameDetecting = true;
            flameDetectStartTime = millis();
        } else if ((millis() - flameDetectStartTime) >= FLAME_DEBOUNCE_MS) {
            return true; // Flame presence confirmed
        }
    } else {
        flameDetecting = false;
    }
    return false;
}

/**
 * Returns human-readable state strings for serial monitor diagnostics output.
 */
const char* getStateName(IgnitionState state) {
    switch (state) {
        case STATE_PREFLOW_BEEP:     return "PREFLOW_BEEP (Step 1: Gas ON, 10 Beeps @ 1Hz, Lights Pulse)";
        case STATE_IGNITION_ATTEMPT: return "IGNITION_ATTEMPT (Step 2: Spark ON, Beeps/Lights @ 2Hz, 10s Flame Check)";
        case STATE_RUNNING:          return "RUNNING (Step 3: Flame Detected, Gas ON, Ignitor/Beeper OFF)";
        case STATE_RETRY_WAIT:       return "RETRY_WAIT (Step 4: All Relays OFF, 30s Safety Purge Delay)";
        case STATE_LOCKOUT:          return "LOCKOUT (5 Attempts Failed - System Halted, Reboot Required)";
        default:                     return "UNKNOWN";
    }
}
