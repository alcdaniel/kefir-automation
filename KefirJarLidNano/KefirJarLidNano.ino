/**
 * Automatic Kefir Jar Lid · Arduino Nano firmware
 * ===============================================
 *
 * Fermentation timer for a 3D-printed jar lid. When the countdown reaches
 * zero, a micro servo releases the spring-loaded plunger that lifts the kefir
 * grains out of the milk, then returns to its closed position.
 *
 * Hardware
 *   - Classic Arduino Nano (ATmega328P, 5 V, 16 MHz).
 *   - SSD1306 128x64 I2C OLED (address 0x3C or 0x3D).
 *   - 9 g micro servo (SG90, MG90S or similar).
 *   - Three push buttons: up, down and select (optional).
 *
 * Wiring (see README.md)
 *   OLED    VDD -> 5V    GND -> GND    SDA -> A4    SCK/SCL -> A5
 *   Servo   signal -> D9   +5 V -> external 5 V supply (>= 1 A recommended)
 *           GND -> ground shared with the Nano
 *   Buttons up -> D2, down -> D3, select -> D4.
 *           The other leg of each button goes to GND (internal pull-up).
 *
 * Libraries (Arduino IDE Library Manager)
 *   - SSD1306Ascii (Bill Greiman): text on the OLED with very little RAM.
 *   - Servo: servo control.
 *   Wire and EEPROM ship with the Arduino AVR core.
 *
 * Usage
 *   - Home screen: TEST (5 s), QUICK (12 h), NORMAL (24 h) and LONG (36 h).
 *     Up/down change the option and select starts it.
 *   - Hold select for 1.5 s on a preset: edit its duration.
 *   - Hold up or down for 1.5 s: options menu (manual time, servo test and
 *     angle calibration).
 *   - While fermenting: select pauses/resumes; holding it for 1.5 s cancels
 *     without releasing the grains.
 *   - Serial console at 115200 baud: type HELP to list the commands.
 *
 * Persistence
 *   Settings and the countdown are saved to EEPROM when starting, pausing,
 *   resuming or cancelling, and every minute while fermenting. After a power
 *   cut the timer resumes from the last saved minute; time without power is
 *   not counted, so the grains are never released unexpectedly.
 *
 * Screen and console texts are plain ASCII because the OLED fonts and many
 * serial monitors only support ASCII characters.
 */
#include <Wire.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
#include <Servo.h>
#include <EEPROM.h>
#include <ctype.h>

// ===========================================================================
// Arduino Nano pins
// ===========================================================================
const byte PIN_SERVO = 9;          // Servo PWM signal.
const byte PIN_BUTTON_UP = 2;      // "Up" button (to GND).
const byte PIN_BUTTON_DOWN = 3;    // "Down" button (to GND).
const byte PIN_BUTTON_SELECT = 4;  // "Select" button (to GND).
// The Nano's I2C bus is fixed: SDA = A4 and SCL = A5.

// ===========================================================================
// OLED display
// ===========================================================================
// Arduino libraries use 7-bit I2C addresses. Some modules print 0x78 or 0x7A
// on the silkscreen: those are the 8-bit forms of 0x3C and 0x3D. Both
// addresses are probed at start-up.
const byte OLED_PRIMARY_ADDRESS = 0x3C;
const byte OLED_ALTERNATIVE_ADDRESS = 0x3D;
const byte SCREEN_WIDTH = 128;
const byte SCREEN_HEIGHT = 64;

// ===========================================================================
// Timer and presets
// ===========================================================================
const unsigned long SAVE_INTERVAL_MS = 60000UL;  // Periodic EEPROM save.
const unsigned long LONG_PRESS_MS = 1500UL;      // Long-press duration.
const unsigned int MIN_DURATION_MINUTES = 0;
const unsigned int MAX_DURATION_MINUTES = 72 * 60;
const byte PRESET_COUNT = 3;                     // QUICK, NORMAL and LONG.
// Home screen options: 0 = 5-second test, 1..3 = presets.
const byte HOME_TEST_SELECTION = 0;
const byte HOME_FIRST_PRESET_SELECTION = 1;
const byte HOME_OPTION_COUNT = PRESET_COUNT + 1;
const uint16_t DEFAULT_PRESET_DURATION_MINUTES[PRESET_COUNT] = {12 * 60, 24 * 60, 36 * 60};
const uint32_t MECHANISM_TEST_FERMENTATION_MS = 5000UL;  // Quick test of the full cycle.
const byte SERIAL_BUFFER_SIZE = 64;              // Maximum serial command length.

// ===========================================================================
// EEPROM
// ===========================================================================
// The "magic numbers" identify the format of the saved data. If they do not
// match (blank EEPROM or data from another sketch), defaults are loaded.
// Change them whenever PersistentData or PresetData change so that old,
// incompatible data is discarded.
const uint32_t EEPROM_MAGIC = 0x4B465232UL;  // "KFR2"

SSD1306AsciiWire display;
Servo releaseServo;

// Fermentation timer state.
enum RunState : byte { IDLE, RUNNING, PAUSED };

// User-interface screens. drawScreen() draws each one and handleButtons()
// decides which one comes next on every button press.
enum Screen : byte {
  HOME, EDIT_PRESET_DURATION, ADVANCED_MENU, DURATION, SETTINGS, EDIT_HOME_ANGLE,
  EDIT_RELEASE_ANGLE, EDIT_RELEASE_TIME, RUNNING_SCREEN, PAUSED_SCREEN, DONE
};

// Settings and timer state stored in EEPROM (address 0).
// EEPROM.put() only rewrites the cells that change, which reduces wear.
struct PersistentData {
  uint32_t magic;
  uint16_t durationMinutes;
  byte homeAngle;
  byte releaseAngle;
  byte releaseSeconds;
  byte runState;
  uint32_t remainingMs;
  byte check;
};

// Duration of the three presets, stored right after PersistentData.
struct PresetData {
  uint32_t magic;
  uint16_t minutes[PRESET_COUNT];
  byte check;
};

const uint32_t PRESET_EEPROM_MAGIC = 0x4B505233UL;  // "KPR3"

// ===========================================================================
// Program state
// ===========================================================================
// First-run defaults; afterwards they are read from EEPROM.
// The angles depend on your build: calibrate them from the SERVO menu.
uint16_t durationMinutes = DEFAULT_PRESET_DURATION_MINUTES[1];
uint16_t presetDurationMinutes[PRESET_COUNT] = {12 * 60, 24 * 60, 36 * 60};
byte homeAngle = 0;
byte releaseAngle = 60;
byte releaseSeconds = 3;
RunState runState = IDLE;
Screen screen = HOME;
byte homeSelection = 2;  // Test, quick, normal, long: starts on NORMAL.
byte advancedSelection = 0;
byte settingsSelection = 0;
uint32_t remainingMs = 0;
uint32_t lastTickMs = 0;
uint32_t lastSaveMs = 0;
uint32_t releaseStartedMs = 0;
bool releaseInProgress = false;
bool releaseIsTest = false;
bool shortTestInProgress = false;
bool displayAvailable = false;
bool ignoreNextHomeSelectRelease = false;
char serialBuffer[SERIAL_BUFFER_SIZE];
byte serialBufferLength = 0;

// SSD1306Ascii writes straight to the display without a RAM frame buffer. The
// last drawn state is remembered so the screen is only redrawn when something
// changes, avoiding flicker on every loop() iteration.
Screen lastRenderedScreen = static_cast<Screen>(255);
byte lastRenderedHomeSelection = 255;
byte lastRenderedAdvancedSelection = 255;
byte lastRenderedSettingsSelection = 255;
uint16_t lastRenderedDuration = 0;
byte lastRenderedHomeAngle = 255;
byte lastRenderedReleaseAngle = 255;
byte lastRenderedReleaseSeconds = 255;
bool lastRenderedReleaseInProgress = false;
uint32_t lastRenderedTimerSecond = UINT32_MAX;

// ===========================================================================
// Buttons
// ===========================================================================
// Push button wired between a pin and GND with 30 ms debouncing (INPUT_PULLUP:
// LOW = pressed). Each update() call reports, at most once:
//   pressed()        -> the button has just gone down.
//   longPressed()    -> it has been held for LONG_PRESS_MS.
//   shortReleased()  -> it was released before becoming a long press.
class Button {
 public:
  Button(byte pin) : pin_(pin) {}

  void begin() {
    pinMode(pin_, INPUT_PULLUP);
    rawState_ = stableState_ = digitalRead(pin_);
  }

  void update(uint32_t now) {
    pressed_ = false;
    longPressed_ = false;
    shortReleased_ = false;
    bool readState = digitalRead(pin_);
    if (readState != rawState_) {
      rawState_ = readState;
      changedMs_ = now;
    }
    if (rawState_ != stableState_ && now - changedMs_ > 30) {
      stableState_ = rawState_;
      if (stableState_ == LOW) {
        downMs_ = now;
        longSent_ = false;
        pressed_ = true;
      } else if (!longSent_) {
        shortReleased_ = true;
      }
    }
    if (stableState_ == LOW && !longSent_ && now - downMs_ >= LONG_PRESS_MS) {
      longSent_ = true;
      longPressed_ = true;
    }
  }

  bool pressed() const { return pressed_; }
  bool longPressed() const { return longPressed_; }
  bool shortReleased() const { return shortReleased_; }

 private:
  byte pin_;
  bool rawState_ = HIGH;
  bool stableState_ = HIGH;
  bool pressed_ = false;
  bool longPressed_ = false;
  bool shortReleased_ = false;
  bool longSent_ = false;
  uint32_t changedMs_ = 0;
  uint32_t downMs_ = 0;
};

Button buttonUp(PIN_BUTTON_UP);
Button buttonDown(PIN_BUTTON_DOWN);
Button buttonSelect(PIN_BUTTON_SELECT);

// ===========================================================================
// EEPROM persistence
// ===========================================================================
// Simple XOR checksum to detect corrupt or incomplete data.
byte checksum(const PersistentData &data) {
  const byte *bytes = reinterpret_cast<const byte *>(&data);
  byte value = 0;
  for (unsigned int i = 0; i < offsetof(PersistentData, check); ++i) value ^= bytes[i];
  return value;
}

byte presetChecksum(const PresetData &data) {
  const byte *bytes = reinterpret_cast<const byte *>(&data);
  byte value = 0;
  for (unsigned int i = 0; i < offsetof(PresetData, check); ++i) value ^= bytes[i];
  return value;
}

void savePersistentData() {
  PersistentData data = {};
  data.magic = EEPROM_MAGIC;
  data.durationMinutes = durationMinutes;
  data.homeAngle = homeAngle;
  data.releaseAngle = releaseAngle;
  data.releaseSeconds = releaseSeconds;
  data.runState = runState;
  data.remainingMs = remainingMs;
  data.check = checksum(data);
  EEPROM.put(0, data);
}

void savePresetData() {
  PresetData data = {};
  data.magic = PRESET_EEPROM_MAGIC;
  for (byte i = 0; i < PRESET_COUNT; ++i) data.minutes[i] = presetDurationMinutes[i];
  data.check = presetChecksum(data);
  EEPROM.put(sizeof(PersistentData), data);
}

// Loads the settings; if the stored data is invalid, the defaults are saved
// for next time.
void loadPersistentData() {
  PersistentData data;
  EEPROM.get(0, data);
  if (data.magic != EEPROM_MAGIC || data.check != checksum(data) ||
      data.durationMinutes < MIN_DURATION_MINUTES || data.durationMinutes > MAX_DURATION_MINUTES ||
      data.homeAngle > 180 || data.releaseAngle > 180 ||
      data.releaseSeconds < 1 || data.releaseSeconds > 10 || data.runState > PAUSED) {
    savePersistentData();
    return;
  }
  durationMinutes = data.durationMinutes;
  homeAngle = data.homeAngle;
  releaseAngle = data.releaseAngle;
  releaseSeconds = data.releaseSeconds;
  runState = static_cast<RunState>(data.runState);
  remainingMs = data.remainingMs;
}

void loadPresetData() {
  PresetData data;
  EEPROM.get(sizeof(PersistentData), data);
  bool valid = data.magic == PRESET_EEPROM_MAGIC && data.check == presetChecksum(data);
  for (byte i = 0; i < PRESET_COUNT && valid; ++i) {
    valid = data.minutes[i] >= MIN_DURATION_MINUTES && data.minutes[i] <= MAX_DURATION_MINUTES;
  }
  if (!valid) {
    for (byte i = 0; i < PRESET_COUNT; ++i) presetDurationMinutes[i] = DEFAULT_PRESET_DURATION_MINUTES[i];
    savePresetData();
    return;
  }
  for (byte i = 0; i < PRESET_COUNT; ++i) presetDurationMinutes[i] = data.minutes[i];
}

// ===========================================================================
// Time formatting
// ===========================================================================
// Truncated HH:MM:SS, for the serial console.
void formatDuration(uint32_t milliseconds, char *out, byte length) {
  uint32_t seconds = milliseconds / 1000UL;
  uint16_t hours = seconds / 3600UL;
  byte minutes = (seconds % 3600UL) / 60UL;
  byte secs = seconds % 60UL;
  snprintf(out, length, "%02u:%02u:%02u", hours, minutes, secs);
}

// HH:MM:SS rounded up, for the 5-second test.
void formatSecondDuration(uint32_t milliseconds, char *out, byte length) {
  const uint32_t seconds = (milliseconds + 999UL) / 1000UL;
  const uint16_t hours = seconds / 3600UL;
  const byte minutes = (seconds % 3600UL) / 60UL;
  const byte secs = seconds % 60UL;
  snprintf(out, length, "%02u:%02u:%02u", hours, minutes, secs);
}

// HH:MM rounded up: 23 h 59 min 59 s is still shown as 24:00.
void formatMinuteDuration(uint32_t milliseconds, char *out, byte length) {
  const uint32_t totalMinutes = (milliseconds + 59999UL) / 60000UL;
  const uint16_t hours = totalMinutes / 60UL;
  const byte minutes = totalMinutes % 60UL;
  snprintf(out, length, "%02u:%02u", hours, minutes);
}

// ===========================================================================
// OLED drawing
// ===========================================================================
// Many two-colour 0.96" OLEDs have a 16 px yellow band at the top and a blue
// area below. Titles stay in the top band and the numbers go underneath; it
// looks just as good on a single-colour display.
void drawTitle(const __FlashStringHelper *title, byte column) {
  display.clear();
  display.setFont(Adafruit5x7);
  display.set2X();
  display.setCursor(column, 0);
  display.print(title);
  display.set1X();
}

// Side arrows showing that there are more options to the left or right.
void drawArrows(bool showLeft, bool showRight) {
  display.setFont(Adafruit5x7);
  display.set2X();
  if (showLeft) { display.setCursor(8, 6); display.print(F("<")); }
  if (showRight) { display.setCursor(108, 6); display.print(F(">")); }
  display.set1X();
}

// Large centred number followed by "H" (for example "24 H").
void drawBigHours(byte hours) {
  char value[4];
  snprintf(value, sizeof(value), "%u", hours);

  display.setFont(Verdana_digits_24);
  display.set1X();
  const byte digitsWidth = display.strWidth(value);
  display.setFont(Adafruit5x7);
  display.set2X();
  const byte hourWidth = display.strWidth("H");
  const byte start = (SCREEN_WIDTH - digitsWidth - hourWidth - 4) / 2;

  display.setFont(Verdana_digits_24);
  display.set1X();
  display.setCursor(start, 3);
  display.print(value);
  display.setFont(Adafruit5x7);
  display.set2X();
  display.setCursor(start + digitsWidth + 4, 4);
  display.print(F("H"));
  display.set1X();
}

// Large centred countdown: HH:MM or, with showSeconds, HH:MM:SS.
void drawBigTimer(uint32_t milliseconds, bool showSeconds = false) {
  char value[16];
  if (showSeconds) formatSecondDuration(milliseconds, value, sizeof(value));
  else formatMinuteDuration(milliseconds, value, sizeof(value));
  display.setFont(Verdana_digits_24);
  display.set1X();
  const byte valueWidth = display.strWidth(value);
  display.setCursor((SCREEN_WIDTH - valueWidth) / 2, 3);
  display.print(value);
  display.setFont(Adafruit5x7);
  display.set1X();
}

// Large centred value (servo angles and seconds).
void drawBigValue(byte value) {
  char text[4];
  snprintf(text, sizeof(text), "%u", value);
  display.setFont(Verdana_digits_24);
  display.set1X();
  const byte textWidth = display.strWidth(text);
  display.setCursor((SCREEN_WIDTH - textWidth) / 2, 3);
  display.print(text);
  display.setFont(Adafruit5x7);
  display.set1X();
}

// Draws the full screen for the current `screen`. Title columns centre the
// text: column = (128 - 12 * characters) / 2 with the 2x font.
void drawScreen() {
  if (!displayAvailable) return;
  switch (screen) {
    case HOME:
      if (homeSelection == HOME_TEST_SELECTION) {
        drawTitle(F("TEST"), 40);
        drawBigTimer(MECHANISM_TEST_FERMENTATION_MS, true);
      } else {
        const byte presetIndex = homeSelection - HOME_FIRST_PRESET_SELECTION;
        if (presetIndex == 0) drawTitle(F("QUICK"), 34);
        else if (presetIndex == 1) drawTitle(F("NORMAL"), 28);
        else drawTitle(F("LONG"), 40);
        if (presetDurationMinutes[presetIndex] % 60 == 0) {
          drawBigHours(presetDurationMinutes[presetIndex] / 60);
        } else {
          drawBigTimer(static_cast<uint32_t>(presetDurationMinutes[presetIndex]) * 60000UL);
        }
      }
      drawArrows(homeSelection > HOME_TEST_SELECTION, homeSelection < HOME_OPTION_COUNT - 1);
      break;
    case EDIT_PRESET_DURATION:
      if (homeSelection == 1) drawTitle(F("QUICK"), 34);
      else if (homeSelection == 2) drawTitle(F("NORMAL"), 28);
      else drawTitle(F("LONG"), 40);
      drawBigTimer(static_cast<uint32_t>(presetDurationMinutes[homeSelection - HOME_FIRST_PRESET_SELECTION]) * 60000UL);
      drawArrows(true, true);
      break;
    case ADVANCED_MENU:
      drawTitle(F("OPTIONS"), 22);
      display.set2X();
      if (advancedSelection == 0) { display.setCursor(40, 3); display.print(F("TIME")); }
      else if (advancedSelection == 1) { display.setCursor(40, 3); display.print(F("TEST")); }
      else if (advancedSelection == 2) { display.setCursor(34, 3); display.print(F("SERVO")); }
      else { display.setCursor(40, 3); display.print(F("EXIT")); }
      display.set1X();
      drawArrows(true, true);
      break;
    case DURATION:
      drawTitle(F("TIME"), 40);
      drawBigTimer(static_cast<uint32_t>(durationMinutes) * 60000UL);
      drawArrows(true, true);
      break;
    case SETTINGS:
      drawTitle(F("SERVO"), 34);
      display.set2X();
      if (settingsSelection == 0) { display.setCursor(28, 3); display.print(F("CLOSED")); }
      else if (settingsSelection == 1) { display.setCursor(40, 3); display.print(F("OPEN")); }
      else if (settingsSelection == 2) { display.setCursor(40, 3); display.print(F("HOLD")); }
      else { display.setCursor(40, 3); display.print(F("EXIT")); }
      display.set1X();
      drawArrows(true, true);
      break;
    case EDIT_HOME_ANGLE:
    case EDIT_RELEASE_ANGLE:
    case EDIT_RELEASE_TIME:
      if (screen == EDIT_HOME_ANGLE) { drawTitle(F("CLOSED"), 28); drawBigValue(homeAngle); }
      else if (screen == EDIT_RELEASE_ANGLE) { drawTitle(F("OPEN"), 40); drawBigValue(releaseAngle); }
      else { drawTitle(F("HOLD S"), 28); drawBigValue(releaseSeconds); }
      drawArrows(true, true);
      break;
    case RUNNING_SCREEN:
      if (shortTestInProgress) drawTitle(F("TEST"), 40);
      else drawTitle(F("ACTIVE"), 28);
      drawBigTimer(remainingMs, shortTestInProgress);
      break;
    case PAUSED_SCREEN:
      drawTitle(F("PAUSED"), 28);
      drawBigTimer(remainingMs, shortTestInProgress);
      break;
    case DONE:
      if (releaseInProgress) drawTitle(F("RELEASING"), 10);
      else drawTitle(F("DONE"), 40);
      break;
  }
}

// Returns true when something changed that requires a full redraw, and
// remembers the current state as "already drawn".
bool needsFullRedraw() {
  const bool changed =
      screen != lastRenderedScreen ||
      homeSelection != lastRenderedHomeSelection ||
      advancedSelection != lastRenderedAdvancedSelection ||
      settingsSelection != lastRenderedSettingsSelection ||
      durationMinutes != lastRenderedDuration ||
      homeAngle != lastRenderedHomeAngle ||
      releaseAngle != lastRenderedReleaseAngle ||
      releaseSeconds != lastRenderedReleaseSeconds ||
      releaseInProgress != lastRenderedReleaseInProgress;
  if (changed) {
    lastRenderedScreen = screen;
    lastRenderedHomeSelection = homeSelection;
    lastRenderedAdvancedSelection = advancedSelection;
    lastRenderedSettingsSelection = settingsSelection;
    lastRenderedDuration = durationMinutes;
    lastRenderedHomeAngle = homeAngle;
    lastRenderedReleaseAngle = releaseAngle;
    lastRenderedReleaseSeconds = releaseSeconds;
    lastRenderedReleaseInProgress = releaseInProgress;
    lastRenderedTimerSecond = shortTestInProgress
        ? (remainingMs + 999UL) / 1000UL
        : (remainingMs + 59999UL) / 60000UL;
  }
  return changed;
}

// During the countdown only the digits are refreshed, once per minute (or once
// per second during the 5-second test).
void updateTimerText() {
  if (!displayAvailable || (screen != RUNNING_SCREEN && screen != PAUSED_SCREEN)) return;
  const uint32_t displayTick = shortTestInProgress
      ? (remainingMs + 999UL) / 1000UL
      : (remainingMs + 59999UL) / 60000UL;
  if (displayTick == lastRenderedTimerSecond) return;

  // Only the numeric area is cleared, so the title band does not flicker.
  display.clear(0, SCREEN_WIDTH - 1, 2, 5);
  drawBigTimer(remainingMs, shortTestInProgress);
  lastRenderedTimerSecond = displayTick;
}

// ===========================================================================
// Servo and fermentation cycle
// ===========================================================================
void moveServoHome() { releaseServo.write(homeAngle); }
void moveServoRelease() { releaseServo.write(releaseAngle); }

// Moves the servo to the release position. loop() returns it to the closed
// position after `releaseSeconds` seconds (finishRelease()).
void beginRelease(bool isTest) {
  releaseIsTest = isTest;
  releaseInProgress = true;
  releaseStartedMs = millis();
  moveServoRelease();
  screen = DONE;
  Serial.println(isTest ? F("Release test started.") : F("Time is up: releasing the grains."));
}

void finishRelease() {
  moveServoHome();
  releaseInProgress = false;
  if (releaseIsTest) {
    screen = HOME;
    Serial.println(F("Test finished: servo in closed position."));
  } else {
    runState = IDLE;
    remainingMs = 0;
    savePersistentData();
    screen = DONE;
    Serial.println(F("Fermentation finished: servo in closed position."));
  }
}

void startFermentation() {
  remainingMs = static_cast<uint32_t>(durationMinutes) * 60000UL;
  shortTestInProgress = false;
  runState = RUNNING;
  lastTickMs = lastSaveMs = millis();
  savePersistentData();
  screen = RUNNING_SCREEN;
  Serial.println(F("Fermentation started."));
}

void startMechanismTest() {
  remainingMs = MECHANISM_TEST_FERMENTATION_MS;
  shortTestInProgress = true;
  runState = RUNNING;
  lastTickMs = lastSaveMs = millis();
  savePersistentData();
  screen = RUNNING_SCREEN;
  Serial.println(F("5-second test started."));
}

void pauseFermentation() {
  runState = PAUSED;
  savePersistentData();
  screen = PAUSED_SCREEN;
  Serial.println(F("Timer paused."));
}

void resumeFermentation() {
  runState = RUNNING;
  lastTickMs = lastSaveMs = millis();
  savePersistentData();
  screen = RUNNING_SCREEN;
  Serial.println(F("Timer resumed."));
}

void cancelFermentation() {
  runState = IDLE;
  remainingMs = 0;
  shortTestInProgress = false;
  savePersistentData();
  moveServoHome();
  screen = HOME;
  Serial.println(F("Fermentation cancelled. Servo in closed position."));
}

// Subtracts the time elapsed since the previous loop. millis() is subtracted
// as unsigned, so its roll-over every ~49 days does no harm.
void updateTimer(uint32_t now) {
  if (runState != RUNNING) return;
  uint32_t elapsed = now - lastTickMs;
  lastTickMs = now;
  if (elapsed >= remainingMs) {
    remainingMs = 0;
    runState = IDLE;
    shortTestInProgress = false;
    savePersistentData();
    beginRelease(false);
    return;
  }
  remainingMs -= elapsed;
  if (now - lastSaveMs >= SAVE_INTERVAL_MS) {
    savePersistentData();
    lastSaveMs = now;
  }
}

// ===========================================================================
// Serial console (115200 baud, commands end with a newline)
// ===========================================================================
// Parses `value` as an integer within [minimum, maximum].
bool parseNumber(const char *value, long minimum, long maximum, long &result) {
  if (value == NULL || *value == '\0') return false;
  char *end = NULL;
  long parsed = strtol(value, &end, 10);
  if (*end != '\0' || parsed < minimum || parsed > maximum) return false;
  result = parsed;
  return true;
}

void printHelp() {
  Serial.println();
  Serial.println(F("=== KEFIR NANO: COMMANDS ==="));
  Serial.println(F("HELP                     Show this list"));
  Serial.println(F("STATUS                   Show settings and timer"));
  Serial.println(F("DURATION <h> [min]       Save a duration (0 min - 72 h)"));
  Serial.println(F("START [h] [min]          Start; with values, change the duration first"));
  Serial.println(F("PAUSE | RESUME | CANCEL"));
  Serial.println(F("TEST                     Open and close the servo once"));
  Serial.println(F("CLOSE                    Move the servo to the closed position"));
  Serial.println(F("CLOSED <0-180>           Set the closed angle"));
  Serial.println(F("RELEASE <0-180>          Set the release angle"));
  Serial.println(F("HOLD <1-10>              Set the seconds held open"));
  Serial.println(F("Examples: DURATION 14 | START | START 12 30"));
}

void printStatus() {
  char timeText[16];
  formatDuration(remainingMs, timeText, sizeof(timeText));
  Serial.println(F("--- STATUS ---"));
  Serial.print(F("Configured duration: ")); Serial.print(durationMinutes / 60);
  Serial.print(F(" h ")); Serial.print(durationMinutes % 60); Serial.println(F(" min"));
  Serial.print(F("Servo closed: ")); Serial.print(homeAngle);
  Serial.print(F(" | release: ")); Serial.print(releaseAngle);
  Serial.print(F(" | hold: ")); Serial.print(releaseSeconds); Serial.println(F(" s"));
  if (releaseInProgress) Serial.println(F("State: RELEASING GRAINS"));
  else if (runState == RUNNING) { Serial.print(F("State: FERMENTING | remaining: ")); Serial.println(timeText); }
  else if (runState == PAUSED) { Serial.print(F("State: PAUSED | remaining: ")); Serial.println(timeText); }
  else Serial.println(F("State: IDLE"));
}

bool setDurationFromArguments(char *hoursText, char *minutesText) {
  long hours = 0, minutes = 0;
  if (!parseNumber(hoursText, 0, 72, hours) ||
      (minutesText != NULL && !parseNumber(minutesText, 0, 59, minutes))) {
    Serial.println(F("Usage: DURATION <hours 0-72> [minutes 0-59]"));
    return false;
  }
  long total = hours * 60L + minutes;
  if (total < MIN_DURATION_MINUTES || total > MAX_DURATION_MINUTES) {
    Serial.println(F("The duration must be between 0 minutes and 72 hours."));
    return false;
  }
  durationMinutes = total;
  savePersistentData();
  Serial.print(F("Duration saved: ")); Serial.print(hours); Serial.print(F(" h "));
  Serial.print(minutes); Serial.println(F(" min."));
  return true;
}

// Accepts the English command or its Spanish alias, kept so that older notes
// and videos still work.
bool isCommand(const char *command, const char *english, const char *spanish) {
  return !strcmp(command, english) || !strcmp(command, spanish);
}

// Runs one complete line: COMMAND [argument1] [argument2].
void handleSerialCommand(char *line) {
  while (isspace(*line)) ++line;
  if (*line == '\0') return;
  for (char *p = line; *p; ++p) *p = toupper(*p);
  char *command = strtok(line, " \t");
  char *arg1 = strtok(NULL, " \t");
  char *arg2 = strtok(NULL, " \t");

  if (isCommand(command, "HELP", "AYUDA")) { printHelp(); return; }
  if (isCommand(command, "STATUS", "ESTADO")) { printStatus(); return; }
  if (releaseInProgress) { Serial.println(F("The servo is releasing; wait until it finishes.")); return; }
  if (isCommand(command, "DURATION", "DURACION")) {
    if (runState == IDLE) setDurationFromArguments(arg1, arg2);
    else Serial.println(F("The duration cannot be changed during a fermentation."));
    return;
  }
  if (isCommand(command, "START", "INICIAR")) {
    if (runState != IDLE) Serial.println(F("A fermentation is already running or paused."));
    else if (arg1 == NULL || setDurationFromArguments(arg1, arg2)) startFermentation();
    return;
  }
  if (isCommand(command, "PAUSE", "PAUSA")) {
    if (runState == RUNNING) pauseFermentation(); else Serial.println(F("There is no running fermentation to pause."));
    return;
  }
  if (isCommand(command, "RESUME", "CONTINUAR")) {
    if (runState == PAUSED && remainingMs > 0) resumeFermentation(); else Serial.println(F("There is no paused fermentation to resume."));
    return;
  }
  if (isCommand(command, "CANCEL", "CANCELAR")) {
    if (runState != IDLE) cancelFermentation(); else Serial.println(F("There is no fermentation to cancel."));
    return;
  }
  if (isCommand(command, "TEST", "PRUEBA")) {
    if (runState == IDLE) beginRelease(true); else Serial.println(F("Cancel or finish the fermentation before testing the servo."));
    return;
  }
  if (isCommand(command, "CLOSE", "CERRAR")) { moveServoHome(); Serial.println(F("Servo in closed position.")); return; }

  long value = 0;
  if (isCommand(command, "CLOSED", "CERRADO")) {
    if (parseNumber(arg1, 0, 180, value)) {
      homeAngle = value; savePersistentData(); moveServoHome();
      Serial.println(F("Closed angle saved."));
    } else Serial.println(F("Usage: CLOSED <angle 0-180>"));
    return;
  }
  if (isCommand(command, "RELEASE", "LIBERACION")) {
    if (parseNumber(arg1, 0, 180, value)) {
      releaseAngle = value; savePersistentData(); Serial.println(F("Release angle saved."));
    } else Serial.println(F("Usage: RELEASE <angle 0-180>"));
    return;
  }
  if (isCommand(command, "HOLD", "RETENCION")) {
    if (parseNumber(arg1, 1, 10, value)) {
      releaseSeconds = value; savePersistentData(); Serial.println(F("Hold time saved."));
    } else Serial.println(F("Usage: HOLD <seconds 1-10>"));
    return;
  }
  Serial.println(F("Unknown command. Type HELP."));
}

// Collects characters until a newline arrives, then runs the command.
void readSerialCommands() {
  while (Serial.available()) {
    char received = Serial.read();
    if (received == '\r') continue;
    if (received == '\n') {
      serialBuffer[serialBufferLength] = '\0';
      handleSerialCommand(serialBuffer);
      serialBufferLength = 0;
    } else if (serialBufferLength < SERIAL_BUFFER_SIZE - 1) {
      serialBuffer[serialBufferLength++] = received;
    } else {
      serialBufferLength = 0;
      Serial.println(F("Command too long. Type HELP."));
    }
  }
}

// ===========================================================================
// Button interface
// ===========================================================================
void startPresetFermentation() {
  durationMinutes = presetDurationMinutes[homeSelection - HOME_FIRST_PRESET_SELECTION];
  startFermentation();
}

void handleButtons() {
  if (releaseInProgress) return;
  if (screen == RUNNING_SCREEN || screen == PAUSED_SCREEN) {
    if (buttonSelect.longPressed()) cancelFermentation();
    else if (buttonSelect.pressed()) {
      if (screen == RUNNING_SCREEN) pauseFermentation(); else resumeFermentation();
    }
    return;
  }
  if (screen == DONE) { if (buttonSelect.pressed()) screen = HOME; return; }
  if (screen == HOME) {
    if (buttonUp.longPressed() || buttonDown.longPressed()) {
      advancedSelection = 0;
      screen = ADVANCED_MENU;
      return;
    }
    if (buttonUp.pressed() && homeSelection > HOME_TEST_SELECTION) --homeSelection;
    if (buttonDown.pressed() && homeSelection < HOME_OPTION_COUNT - 1) ++homeSelection;
    // On the home screen a short press starts on release; holding select
    // opens the preset editor without starting the timer first.
    if (buttonSelect.longPressed() && homeSelection != HOME_TEST_SELECTION) screen = EDIT_PRESET_DURATION;
    else if (buttonSelect.shortReleased()) {
      if (ignoreNextHomeSelectRelease) ignoreNextHomeSelectRelease = false;
      else if (homeSelection == HOME_TEST_SELECTION) startMechanismTest();
      else startPresetFermentation();
    }
    return;
  }
  if (screen == EDIT_PRESET_DURATION) {
    uint16_t &selectedDuration = presetDurationMinutes[homeSelection - HOME_FIRST_PRESET_SELECTION];
    if (buttonUp.pressed() && selectedDuration < MAX_DURATION_MINUTES) selectedDuration += 60;
    if (buttonDown.pressed() && selectedDuration > MIN_DURATION_MINUTES) {
      selectedDuration = selectedDuration >= 60 ? selectedDuration - 60 : 0;
    }
    if (buttonSelect.pressed()) {
      savePresetData();
      ignoreNextHomeSelectRelease = true;
      screen = HOME;
    }
    return;
  }
  if (screen == ADVANCED_MENU) {
    if (buttonUp.pressed()) advancedSelection = (advancedSelection + 3) % 4;
    if (buttonDown.pressed()) advancedSelection = (advancedSelection + 1) % 4;
    if (buttonSelect.pressed()) {
      if (advancedSelection == 0) screen = DURATION;
      else if (advancedSelection == 1) beginRelease(true);
      else if (advancedSelection == 2) { settingsSelection = 0; screen = SETTINGS; }
      else screen = HOME;
    }
    return;
  }
  if (screen == DURATION) {
    if (buttonUp.pressed() && durationMinutes < MAX_DURATION_MINUTES) durationMinutes += 30;
    if (buttonDown.pressed() && durationMinutes > MIN_DURATION_MINUTES) {
      durationMinutes = durationMinutes >= 30 ? durationMinutes - 30 : 0;
    }
    if (buttonSelect.pressed()) startFermentation();
    return;
  }
  if (screen == SETTINGS) {
    if (buttonUp.pressed()) settingsSelection = (settingsSelection + 3) % 4;
    if (buttonDown.pressed()) settingsSelection = (settingsSelection + 1) % 4;
    if (buttonSelect.pressed()) {
      if (settingsSelection == 0) screen = EDIT_HOME_ANGLE;
      else if (settingsSelection == 1) screen = EDIT_RELEASE_ANGLE;
      else if (settingsSelection == 2) screen = EDIT_RELEASE_TIME;
      else screen = ADVANCED_MENU;
    }
    return;
  }
  if (screen == EDIT_HOME_ANGLE) {
    if (buttonUp.pressed() && homeAngle < 180) ++homeAngle;
    if (buttonDown.pressed() && homeAngle > 0) --homeAngle;
    moveServoHome();
  } else if (screen == EDIT_RELEASE_ANGLE) {
    if (buttonUp.pressed() && releaseAngle < 180) ++releaseAngle;
    if (buttonDown.pressed() && releaseAngle > 0) --releaseAngle;
  } else if (screen == EDIT_RELEASE_TIME) {
    if (buttonUp.pressed() && releaseSeconds < 10) ++releaseSeconds;
    if (buttonDown.pressed() && releaseSeconds > 1) --releaseSeconds;
  }
  if (buttonSelect.pressed()) { savePersistentData(); screen = SETTINGS; }
}

// ===========================================================================
// Start-up and main loop
// ===========================================================================
bool isI2cDeviceAt(byte address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

byte findOledAddress() {
  if (isI2cDeviceAt(OLED_PRIMARY_ADDRESS)) return OLED_PRIMARY_ADDRESS;
  if (isI2cDeviceAt(OLED_ALTERNATIVE_ADDRESS)) return OLED_ALTERNATIVE_ADDRESS;
  return 0;
}

void setup() {
  Serial.begin(115200);
  buttonUp.begin(); buttonDown.begin(); buttonSelect.begin();
  loadPersistentData();
  loadPresetData();
  Wire.begin();                 // Nano: SDA = A4, SCL = A5 (fixed I2C pins).
  Wire.setClock(100000L);       // Standard 100 kHz I2C, the most compatible.
  byte oledAddress = findOledAddress();
  if (oledAddress != 0) {
    display.begin(&Adafruit128x64, oledAddress);
    display.setFont(Adafruit5x7);
    displayAvailable = true;
    Serial.print(F("OLED found at I2C 0x"));
    Serial.println(oledAddress, HEX);
  } else {
    Serial.println(F("No I2C OLED found at 0x3C or 0x3D."));
    Serial.println(F("Classic Nano: SDA=A4, SCL=A5, VCC=3V3/5V and GND=GND."));
    Serial.println(F("Serial control is still available."));
  }
  releaseServo.attach(PIN_SERVO);
  moveServoHome();
  // If power was lost during a fermentation, resume from the last saved
  // minute.
  if (runState == RUNNING && remainingMs > 0) {
    screen = RUNNING_SCREEN;
    lastTickMs = lastSaveMs = millis();
  } else if (runState == PAUSED && remainingMs > 0) {
    screen = PAUSED_SCREEN;
  } else {
    runState = IDLE;
    screen = HOME;
  }
  drawScreen();
  needsFullRedraw();
  Serial.println(F("Kefir Nano ready. Serial port: 115200 baud."));
  printHelp();
}

void loop() {
  uint32_t now = millis();
  buttonUp.update(now); buttonDown.update(now); buttonSelect.update(now);
  // Echo every button press on the console: handy to check the wiring.
  if (buttonUp.pressed()) Serial.println(F("Button pressed: UP (D2)"));
  if (buttonDown.pressed()) Serial.println(F("Button pressed: DOWN (D3)"));
  if (buttonSelect.pressed()) Serial.println(F("Button pressed: SELECT (D4)"));
  readSerialCommands();
  updateTimer(now);
  // updateTimer() may start the release in this same iteration, so millis()
  // is read again: `now` is older than releaseStartedMs, and the unsigned
  // subtraction would wrap around and close the servo immediately.
  const uint32_t releaseNow = millis();
  if (releaseInProgress && releaseNow - releaseStartedMs >= static_cast<uint32_t>(releaseSeconds) * 1000UL) {
    finishRelease();
  }
  handleButtons();
  if (needsFullRedraw()) {
    drawScreen();
  } else updateTimerText();
}
