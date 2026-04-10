// ============================================================
//  Gesture-Controlled Presentation System
//  ESP32 BLE HID Keyboard
//  Team: San, Sam, Reet
// ============================================================
//
//  SENSORS:
//    - HC-SR04 Left  → Previous Slide (LEFT ARROW)
//    - HC-SR04 Right → Next Slide     (RIGHT ARROW)
//    - IR Break-beam → Play/Pause     (SPACEBAR)
//    - LDR           → LED brightness control via PWM
//
//  LIBRARY REQUIRED:
//    Install "ESP32 BLE Keyboard" by T-vK from Arduino Library Manager
//    (Search: "BleKeyboard")
// ============================================================

#include <BleKeyboard.h>

// ─────────────────────────────────────────────
//  PIN DEFINITIONS
// ─────────────────────────────────────────────

// HC-SR04 Ultrasonic — LEFT (Previous Slide)
#define TRIG_LEFT    25
#define ECHO_LEFT    26

// HC-SR04 Ultrasonic — RIGHT (Next Slide)
#define TRIG_RIGHT   27
#define ECHO_RIGHT   14

// IR Break-beam Sensor (Play/Pause)
// Connect OUT pin here; beam broken = LOW
#define IR_SENSOR    34  // Must be INPUT-only GPIO (34-39 on ESP32)

// LDR (Photoresistor) — Analog pin
#define LDR_PIN      35  // Must be INPUT-only GPIO (34-39 on ESP32)

// Status LEDs
#define LED_LEFT     18  // Blinks when left gesture detected
#define LED_RIGHT    19  // Blinks when right gesture detected
#define LED_IR       21  // Blinks when IR gesture detected
#define LED_STATUS   22  // Always-on status LED, brightness controlled by LDR

// PWM channel for status LED brightness
#define PWM_CHANNEL  0
#define PWM_FREQ     5000
#define PWM_RES_BITS 8   // 8-bit = 0–255

// ─────────────────────────────────────────────
//  TUNABLE PARAMETERS
// ─────────────────────────────────────────────

// Distance (cm) at which a hand triggers the sensor
#define GESTURE_THRESHOLD_CM  20

// After one sensor fires, wait this long for a "swipe-through" on the other
#define SWIPE_WINDOW_MS       600

// Minimum time a hand must be present before registering (debounce hold)
#define HOLD_MIN_MS           150

// Ignore all inputs for this long after a gesture fires
#define COOLDOWN_MS           900

// Median filter window size (must be odd for clean median)
#define FILTER_SAMPLES        5

// LDR smoothing window
#define LDR_SAMPLES           8

// How often (ms) sensors are polled — do not go below 30ms for HC-SR04
#define SENSOR_POLL_INTERVAL  50

// ─────────────────────────────────────────────
//  GESTURE STATE MACHINE
// ─────────────────────────────────────────────

enum GestureState {
  STATE_IDLE,       // Waiting for any sensor activation
  STATE_DETECTING,  // One sensor fired, watching for second (or hold)
  STATE_COOLDOWN    // Gesture sent, ignoring inputs
};

GestureState gState = STATE_IDLE;

// ─────────────────────────────────────────────
//  RUNTIME VARIABLES
// ─────────────────────────────────────────────

// BLE Keyboard object (Device name, Manufacturer, Battery %)
BleKeyboard bleKeyboard("GestureController", "ESP32-Team", 100);

// Timing
unsigned long lastPollTime    = 0;
unsigned long cooldownStart   = 0;
unsigned long leftTriggerTime = 0;
unsigned long rightTriggerTime = 0;

// Trigger flags
bool leftActive  = false;
bool rightActive = false;

// Median filter buffers
float leftBuf[FILTER_SAMPLES];
float rightBuf[FILTER_SAMPLES];
int   leftBufIdx  = 0;
int   rightBufIdx = 0;

// LDR smoothing buffer
int ldrBuf[LDR_SAMPLES];
int ldrBufIdx = 0;

// ─────────────────────────────────────────────
//  FUNCTION PROTOTYPES
// ─────────────────────────────────────────────

float   measureDistance(int trigPin, int echoPin);
float   medianFilter(float buf[], int &idx, float newVal);
int     ldrSmoothed();
void    updateStatusLED();
void    blinkLED(int pin, int durationMs);
void    sendKey(uint8_t key, const char* label);
void    runStateMachine(float distLeft, float distRight, bool irBroken);

// ============================================================
//  SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Gesture-Controlled Presentation System ===");
  Serial.println("Initializing hardware...");

  // Ultrasonic sensor pins
  pinMode(TRIG_LEFT,  OUTPUT);
  pinMode(ECHO_LEFT,  INPUT);
  pinMode(TRIG_RIGHT, OUTPUT);
  pinMode(ECHO_RIGHT, INPUT);

  // IR Break-beam (active LOW when beam broken)
  pinMode(IR_SENSOR, INPUT_PULLUP);

  // Status LEDs
  pinMode(LED_LEFT,  OUTPUT);
  pinMode(LED_RIGHT, OUTPUT);
  pinMode(LED_IR,    OUTPUT);

  // PWM for status LED (LDR-controlled brightness)
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES_BITS);
  ledcAttachPin(LED_STATUS, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 128);   // Start at 50% brightness

  // Initialize filter buffers to far-away distance
  for (int i = 0; i < FILTER_SAMPLES; i++) {
    leftBuf[i]  = 400.0f;
    rightBuf[i] = 400.0f;
  }
  for (int i = 0; i < LDR_SAMPLES; i++) {
    ldrBuf[i] = 2048;
  }

  // Brief startup LED test
  digitalWrite(LED_LEFT,  HIGH);
  digitalWrite(LED_RIGHT, HIGH);
  digitalWrite(LED_IR,    HIGH);
  delay(500);
  digitalWrite(LED_LEFT,  LOW);
  digitalWrite(LED_RIGHT, LOW);
  digitalWrite(LED_IR,    LOW);

  // Start BLE
  Serial.println("Starting BLE Keyboard...");
  bleKeyboard.begin();
  Serial.println("Waiting for Bluetooth connection...");
}

// ============================================================
//  MAIN LOOP
// ============================================================

void loop() {
  unsigned long now = millis();

  // Update status LED brightness from LDR (runs every loop)
  updateStatusLED();

  // Poll sensors at the fixed interval only
  if (now - lastPollTime < SENSOR_POLL_INTERVAL) return;
  lastPollTime = now;

  // Read all sensors
  float rawLeft  = measureDistance(TRIG_LEFT,  ECHO_LEFT);
  float rawRight = measureDistance(TRIG_RIGHT, ECHO_RIGHT);
  bool  irBroken = (digitalRead(IR_SENSOR) == LOW);

  // Apply median filter for noise rejection
  float distLeft  = medianFilter(leftBuf,  leftBufIdx,  rawLeft);
  float distRight = medianFilter(rightBuf, rightBufIdx, rawRight);

  // Debug output to Serial Monitor
  Serial.printf("[L: %5.1fcm | R: %5.1fcm | IR: %s | State: %d | BLE: %s]\n",
    distLeft, distRight,
    irBroken ? "BREAK" : "  OK ",
    (int)gState,
    bleKeyboard.isConnected() ? "Connected" : "Waiting...");

  // Run the gesture FSM
  runStateMachine(distLeft, distRight, irBroken);
}

// ============================================================
//  GESTURE STATE MACHINE
// ============================================================

void runStateMachine(float distLeft, float distRight, bool irBroken) {
  unsigned long now = millis();

  switch (gState) {

    // ── IDLE: Wait for first sensor trigger ──────────────────
    case STATE_IDLE:

      // IR gesture has priority (immediate — no swipe window needed)
      if (irBroken) {
        Serial.println(">> IR BEAM BROKEN → SPACEBAR (Play/Pause)");
        sendKey(' ', "SPACE");
        blinkLED(LED_IR, 250);
        cooldownStart = now;
        gState = STATE_COOLDOWN;
        return;
      }

      // Left sensor triggered first
      if (distLeft < GESTURE_THRESHOLD_CM) {
        leftActive      = true;
        leftTriggerTime = now;
        rightActive     = false;
        gState = STATE_DETECTING;
        Serial.println(">> LEFT sensor active — watching for swipe or hold...");
      }
      // Right sensor triggered first
      else if (distRight < GESTURE_THRESHOLD_CM) {
        rightActive      = true;
        rightTriggerTime = now;
        leftActive       = false;
        gState = STATE_DETECTING;
        Serial.println(">> RIGHT sensor active — watching for swipe or hold...");
      }
      break;

    // ── DETECTING: First sensor fired, watching for resolution ──
    case STATE_DETECTING:

      if (leftActive) {
        // Swipe-through: hand moves from Left → Right → NEXT slide
        if (distRight < GESTURE_THRESHOLD_CM &&
            (now - leftTriggerTime) < SWIPE_WINDOW_MS) {
          Serial.println(">> SWIPE RIGHT (L→R) → NEXT SLIDE (→)");
          sendKey(KEY_RIGHT_ARROW, "RIGHT ARROW");
          blinkLED(LED_RIGHT, 300);
          blinkLED(LED_LEFT,  150);
          gState = STATE_COOLDOWN;
          cooldownStart = now;
          leftActive = false;
        }
        // Simple left tap: hand held & withdrawn from left sensor → PREVIOUS slide
        else if (distLeft >= GESTURE_THRESHOLD_CM &&
                 (now - leftTriggerTime) >= HOLD_MIN_MS) {
          Serial.println(">> LEFT TAP → PREVIOUS SLIDE (←)");
          sendKey(KEY_LEFT_ARROW, "LEFT ARROW");
          blinkLED(LED_LEFT, 300);
          gState = STATE_COOLDOWN;
          cooldownStart = now;
          leftActive = false;
        }
        // Timeout: gesture window expired
        else if ((now - leftTriggerTime) > SWIPE_WINDOW_MS) {
          Serial.println(">> LEFT timeout — resetting.");
          leftActive = false;
          gState = STATE_IDLE;
        }
      }

      else if (rightActive) {
        // Swipe-through: hand moves from Right → Left → PREVIOUS slide
        if (distLeft < GESTURE_THRESHOLD_CM &&
            (now - rightTriggerTime) < SWIPE_WINDOW_MS) {
          Serial.println(">> SWIPE LEFT (R→L) → PREVIOUS SLIDE (←)");
          sendKey(KEY_LEFT_ARROW, "LEFT ARROW");
          blinkLED(LED_LEFT,  300);
          blinkLED(LED_RIGHT, 150);
          gState = STATE_COOLDOWN;
          cooldownStart = now;
          rightActive = false;
        }
        // Simple right tap: hand held & withdrawn from right sensor → NEXT slide
        else if (distRight >= GESTURE_THRESHOLD_CM &&
                 (now - rightTriggerTime) >= HOLD_MIN_MS) {
          Serial.println(">> RIGHT TAP → NEXT SLIDE (→)");
          sendKey(KEY_RIGHT_ARROW, "RIGHT ARROW");
          blinkLED(LED_RIGHT, 300);
          gState = STATE_COOLDOWN;
          cooldownStart = now;
          rightActive = false;
        }
        // Timeout
        else if ((now - rightTriggerTime) > SWIPE_WINDOW_MS) {
          Serial.println(">> RIGHT timeout — resetting.");
          rightActive = false;
          gState = STATE_IDLE;
        }
      }

      else {
        // No active sensor — fallback reset
        gState = STATE_IDLE;
      }
      break;

    // ── COOLDOWN: Ignore all inputs ───────────────────────────
    case STATE_COOLDOWN:
      if ((now - cooldownStart) >= COOLDOWN_MS) {
        gState = STATE_IDLE;
        leftActive  = false;
        rightActive = false;
        Serial.println(">> Cooldown done. Ready for next gesture.\n");
      }
      break;
  }
}

// ============================================================
//  HC-SR04: MEASURE DISTANCE  (returns cm)
// ============================================================

float measureDistance(int trigPin, int echoPin) {
  // Send 10µs pulse
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Wait for echo; 30ms timeout = ~5m max range (beyond HC-SR04 spec)
  long duration = pulseIn(echoPin, HIGH, 30000UL);

  if (duration == 0) return 400.0f;          // Timeout → treat as out-of-range
  return (duration * 0.0343f) / 2.0f;        // µs → cm (speed of sound = 343 m/s)
}

// ============================================================
//  MEDIAN FILTER  (noise rejection for ultrasonic)
// ============================================================

float medianFilter(float buf[], int &idx, float newVal) {
  buf[idx] = newVal;
  idx = (idx + 1) % FILTER_SAMPLES;

  // Copy buffer and sort
  float sorted[FILTER_SAMPLES];
  for (int i = 0; i < FILTER_SAMPLES; i++) sorted[i] = buf[i];

  // Insertion sort (fast enough for small N)
  for (int i = 1; i < FILTER_SAMPLES; i++) {
    float key = sorted[i];
    int j = i - 1;
    while (j >= 0 && sorted[j] > key) {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = key;
  }

  return sorted[FILTER_SAMPLES / 2];   // Middle value = median
}

// ============================================================
//  LDR: SMOOTHED AMBIENT LIGHT READING
// ============================================================

int ldrSmoothed() {
  ldrBuf[ldrBufIdx] = analogRead(LDR_PIN);
  ldrBufIdx = (ldrBufIdx + 1) % LDR_SAMPLES;

  long sum = 0;
  for (int i = 0; i < LDR_SAMPLES; i++) sum += ldrBuf[i];
  return (int)(sum / LDR_SAMPLES);
}

// ============================================================
//  LDR → STATUS LED BRIGHTNESS (PWM)
// ============================================================

void updateStatusLED() {
  int avg = ldrSmoothed();

  // ESP32 ADC: 0 (dark) to 4095 (bright)
  // In a bright room → LEDs can be brighter
  // In a dark room   → LEDs should be dimmer (not distracting)
  // Map: low LDR → low brightness; high LDR → high brightness
  int brightness = map(avg, 0, 4095, 15, 255);
  brightness = constrain(brightness, 15, 255);
  ledcWrite(PWM_CHANNEL, brightness);
}

// ============================================================
//  BLINK A LED for visual feedback
// ============================================================

void blinkLED(int pin, int durationMs) {
  digitalWrite(pin, HIGH);
  delay(durationMs);
  digitalWrite(pin, LOW);
}

// ============================================================
//  SEND BLE KEYSTROKE
// ============================================================

void sendKey(uint8_t key, const char* label) {
  if (bleKeyboard.isConnected()) {
    bleKeyboard.write(key);
    Serial.printf("   [BLE] Sent key: %s\n", label);
  } else {
    Serial.println("   [BLE] NOT CONNECTED — key not sent!");
  }
}
