/**
 * Automatic Kefir Jar Lid · Arduino Nano firmware
 * ===============================================
 *
 * Fermentation timer for a 3D-printed jar lid. When the countdown reaches
 * zero, a micro servo releases the spring-loaded plunger that lifts the kefir
 * grains out of the milk, then returns to its closed position.
 *
 * Copyright (c) 2026 Daniel Álvarez Conde
 * Licence: CC BY-NC-SA 4.0 (https://creativecommons.org/licenses/by-nc-sa/4.0/)
 * Project: https://www.alcdaniel.com/store/product/?slug=kefir-automation-jar-lid
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
 *   - SSD1306Ascii (Bill Greiman): text and raw bitmaps on the OLED.
 *   - Servo: servo control.
 *   Wire and EEPROM ship with the Arduino AVR core.
 *
 * Usage
 *   - First start: choose the language (English or Spanish).
 *   - Home screen: TEST (5 s), QUICK (12 h), NORMAL (24 h), LONG (36 h) and
 *     MENU. Up/down change the option; select starts it or opens the menu.
 *     The last preset you started is the one shown when the lid starts.
 *   - Hold select for 1.5 s on a preset: edit its duration.
 *   - Hold up or down for 1.5 s: the menu from anywhere on the home screen.
 *   - Menu: TIME (edit the QUICK, NORMAL and LONG times: OK selects a preset,
 *     OK again edits it while the time blinks, OK saves; nothing starts), SERVO (angles and hold time), LANGUAGE (English /
 *     Spanish) and EXIT.
 *   - While fermenting: up adds 30 minutes; holding select for 1.5 s cancels,
 *     which releases the grains like the timer does at zero.
 *   - Buttons act when released (a hold is a separate action), so pressing
 *     one button never triggers a second action on the next screen.
 *   - Power saving: after 30 s without buttons the screen dims (fermenting:
 *     only the time left; finished: DONE) or turns off. A press wakes it;
 *     after DONE, a press goes back to the home screen.
 *   - Serial console at 115200 baud (English only): type HELP for the commands.
 *
 * Screen
 *   The character is a kefir grain called KEFI. It says hello at start-up,
 *   animates when a fermentation starts, every minute, while releasing and
 *   when it is done. Its face shows the state: wink = ready, happy =
 *   fermenting, star eyes = done, open mouth = grains being released, worried
 *   = no time set. Menus are not animated, so they respond straight away. The time is HH:MM while more than a minute is left and
 *   00:SS below that; a bar at the bottom shows progress.
 *
 * Persistence
 *   Settings and the countdown are saved to EEPROM when starting, cancelling,
 *   adding time, and every 5 minutes while fermenting (EEPROM cells wear out
 *   after about 100,000 writes). After a power cut the timer resumes from the
 *   last saved point, so up to 5 minutes can be lost; time without power is
 *   not counted, so the grains are never released unexpectedly.
 *   The language and the last preset are stored separately.
 *
 * Screen texts are plain ASCII (no accents) because the OLED fonts and many
 * serial monitors only support ASCII characters. The serial console is English.
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

// Layout in 8-pixel rows (pages). Rows 0-1: title. Rows 2-5: face on the left,
// big time on the right. Row 7: progress bar or footer hint.
const byte FACE_COL = 8;
const byte FACE_ROW = 2;
const byte TIME_COL = 48;
const byte TIME_WIDTH = 80;
const byte TIME_ROW = 3;
const byte BAR_COL = 4;
const byte BAR_WIDTH = 120;
const byte BAR_ROW = 7;
const byte FOOTER_COL = 4;

// ===========================================================================
// Timer and presets
// ===========================================================================
const unsigned long SAVE_INTERVAL_MS = 300000UL;  // Periodic EEPROM save (5 min).
const unsigned long LONG_PRESS_MS = 1500UL;       // Long-press duration.
const unsigned long NO_TIME_MESSAGE_MS = 2500UL;  // "NO TIME" stays this long.
const uint32_t ONE_MINUTE_MS = 60000UL;           // Above this: HH:MM; below: 00:SS.
const unsigned int MIN_DURATION_MINUTES = 0;
const unsigned int MAX_DURATION_MINUTES = 72 * 60;
const uint16_t ADD_TIME_MINUTES = 30;             // Added with UP during a fermentation.
const byte PRESET_COUNT = 3;                      // QUICK, NORMAL and LONG.
// Home screen options: 0 = 5-second test, 1..3 = presets, 4 = menu.
const byte HOME_TEST_SELECTION = 0;
const byte HOME_FIRST_PRESET_SELECTION = 1;
const byte HOME_MENU_SELECTION = 4;
const byte HOME_OPTION_COUNT = PRESET_COUNT + 2;
const byte HOME_DEFAULT_SELECTION = 2;            // NORMAL, until a preset has been started.
const uint16_t DEFAULT_PRESET_DURATION_MINUTES[PRESET_COUNT] = {12 * 60, 24 * 60, 36 * 60};
const uint32_t MECHANISM_TEST_FERMENTATION_MS = 5000UL;  // Quick test of the full cycle.
const byte SERIAL_BUFFER_SIZE = 64;               // Maximum serial command length.
const byte TEST_FLAG = 0x80;                      // Bit in the saved runState: 5-second test.
const byte LEGACY_PAUSED_STATE = 2;               // Runs paused by older firmware resume.

// Power saving. After IDLE_TIMEOUT_MS without buttons or serial commands the
// display dims (fermenting: only the time left) or turns off.
const unsigned long IDLE_TIMEOUT_MS = 30000UL;
const byte CONTRAST_FULL = 0xCF;
const byte CONTRAST_DIM = 0x08;
const byte CMD_DISPLAY_OFF = 0xAE;
const byte CMD_DISPLAY_ON = 0xAF;

// Options menu items.
const byte MENU_ITEM_COUNT = 4;
const byte MENU_TIME = 0;
const byte MENU_SERVO = 1;
const byte MENU_LANGUAGE = 2;
const byte MENU_EXIT = 3;

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

// Interface language. ENGLISH is the default until the first choice is saved.
enum Language : byte { ENGLISH, SPANISH };

// Fermentation timer state.
enum RunState : byte { IDLE, RUNNING };

// Display power state: full screen, dimmed time, or off.
enum DisplayMode : byte { DISPLAY_AWAKE, DISPLAY_DIM, DISPLAY_OFF };

// User-interface screens. drawScreen() draws each one and handleButtons()
// decides which one comes next on every button press.
enum Screen : byte {
  HOME, EDIT_PRESET_DURATION, ADVANCED_MENU, TIME_PICK, TIME_EDIT, SETTINGS, EDIT_HOME_ANGLE,
  EDIT_RELEASE_ANGLE, EDIT_RELEASE_TIME, RUNNING_SCREEN, DONE, NO_TIME, LANGUAGE_PICK
};

// Settings and timer state stored in EEPROM (address 0).
// EEPROM.put() only rewrites the cells that change, which reduces wear.
struct PersistentData {
  uint32_t magic;
  uint16_t durationMinutes;
  byte homeAngle;
  byte releaseAngle;
  byte releaseSeconds;
  byte runState;      // RunState, plus TEST_FLAG during the 5-second test.
  uint32_t remainingMs;
  byte check;
};

// Duration of the three presets, stored right after PersistentData.
struct PresetData {
  uint32_t magic;
  uint16_t minutes[PRESET_COUNT];
  byte check;
};

// Language, stored after the presets.
struct LanguageData {
  uint32_t magic;
  byte language;
  byte check;
};

// Last preset started (home screen selection), stored after the language.
struct LastPresetData {
  uint32_t magic;
  byte selection;
  byte check;
};

const uint32_t PRESET_EEPROM_MAGIC = 0x4B505233UL;     // "KPR3"
const uint32_t LANGUAGE_EEPROM_MAGIC = 0x4B4C4E31UL;   // "KLN1"
const uint32_t LAST_PRESET_EEPROM_MAGIC = 0x4B4C5034UL;  // "KLP4"
const int PRESET_EEPROM_ADDRESS = sizeof(PersistentData);
const int LANGUAGE_EEPROM_ADDRESS = PRESET_EEPROM_ADDRESS + sizeof(PresetData);
const int LAST_PRESET_EEPROM_ADDRESS = LANGUAGE_EEPROM_ADDRESS + sizeof(LanguageData);

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
Language language = ENGLISH;
bool languageSaved = false;     // False until the language has been chosen.
byte languageChoice = ENGLISH;  // Highlighted option on the language screen.
byte homeSelection = HOME_DEFAULT_SELECTION;
byte presetSelection = HOME_DEFAULT_SELECTION;  // Last preset on the home screen.
byte timeChoice = 0;                  // TIME menu: 0-2 = presets, 3 = back.
bool blinkVisible = true;             // Blinking time while editing (TIME_EDIT).
uint32_t lastBlinkMs = 0;
const uint32_t BLINK_MS = 500;

// Character animations (see "Animations" below).
enum Animation : byte { ANIM_NONE, ANIM_SPLASH, ANIM_START, ANIM_TICK, ANIM_BLINK, ANIM_SHAKE, ANIM_DONE_FX };
Animation animKind = ANIM_NONE;
Screen animScreen = HOME;     // The animation stops when the screen changes.
byte animStep = 0;
uint32_t animNextMs = 0;
uint32_t nextBlinkMs = 0;
byte advancedSelection = 0;
byte settingsSelection = 0;
uint32_t remainingMs = 0;
uint32_t totalMs = 0;           // Length of the current run, for the progress bar.
uint32_t lastTickMs = 0;
uint32_t lastSaveMs = 0;
uint32_t releaseStartedMs = 0;
uint32_t noTimeStartedMs = 0;
bool releaseInProgress = false;
bool releaseIsTest = false;
bool shortTestInProgress = false;
bool displayAvailable = false;
DisplayMode displayMode = DISPLAY_AWAKE;
Screen sleepScreen = HOME;      // Screen that was showing when the display dimmed or turned off.
uint32_t lastActivityMs = 0;    // Last button press or serial command.
uint32_t lastDimTick = UINT32_MAX;
bool wakeHeld = false;          // The press that woke the display is ignored until released.
char serialBuffer[SERIAL_BUFFER_SIZE];
byte serialBufferLength = 0;

// SSD1306Ascii writes straight to the display without a RAM frame buffer. The
// last drawn state is remembered so the screen is only redrawn when something
// changes, avoiding flicker on every loop() iteration.
Screen lastRenderedScreen = static_cast<Screen>(255);
byte lastRenderedHomeSelection = 255;
byte lastRenderedAdvancedSelection = 255;
byte lastRenderedSettingsSelection = 255;
byte lastRenderedLanguage = 255;
byte lastRenderedLanguageChoice = 255;
uint16_t lastRenderedDuration = 0;
uint16_t lastRenderedPreset = 0;
byte lastRenderedTimeChoice = 255;
uint16_t lastRenderedTimeValue = 0;
byte lastRenderedHomeAngle = 255;
byte lastRenderedReleaseAngle = 255;
byte lastRenderedReleaseSeconds = 255;
bool lastRenderedReleaseInProgress = false;
uint32_t lastRenderedTick = UINT32_MAX;

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
  bool isDown() const { return stableState_ == LOW; }

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
  data.runState = static_cast<byte>(runState) | (shortTestInProgress ? TEST_FLAG : 0);
  data.remainingMs = remainingMs;
  data.check = checksum(data);
  EEPROM.put(0, data);
}

void savePresetData() {
  PresetData data = {};
  data.magic = PRESET_EEPROM_MAGIC;
  for (byte i = 0; i < PRESET_COUNT; ++i) data.minutes[i] = presetDurationMinutes[i];
  data.check = presetChecksum(data);
  EEPROM.put(PRESET_EEPROM_ADDRESS, data);
}

void saveLanguage() {
  LanguageData data = {};
  data.magic = LANGUAGE_EEPROM_MAGIC;
  data.language = language;
  data.check = language ^ 0x5A;
  EEPROM.put(LANGUAGE_EEPROM_ADDRESS, data);
  languageSaved = true;
}

void saveLastPreset() {
  LastPresetData data = {};
  data.magic = LAST_PRESET_EEPROM_MAGIC;
  data.selection = homeSelection;
  data.check = homeSelection ^ 0x3C;
  EEPROM.put(LAST_PRESET_EEPROM_ADDRESS, data);
}

// Loads the settings; if the stored data is invalid, the defaults are saved
// for next time.
void loadPersistentData() {
  PersistentData data;
  EEPROM.get(0, data);
  byte savedState = data.runState & ~TEST_FLAG;
  if (savedState == LEGACY_PAUSED_STATE) savedState = RUNNING;
  if (data.magic != EEPROM_MAGIC || data.check != checksum(data) ||
      data.durationMinutes < MIN_DURATION_MINUTES || data.durationMinutes > MAX_DURATION_MINUTES ||
      data.homeAngle > 180 || data.releaseAngle > 180 ||
      data.releaseSeconds < 1 || data.releaseSeconds > 10 || savedState > RUNNING) {
    savePersistentData();
    return;
  }
  durationMinutes = data.durationMinutes;
  homeAngle = data.homeAngle;
  releaseAngle = data.releaseAngle;
  releaseSeconds = data.releaseSeconds;
  runState = static_cast<RunState>(savedState);
  shortTestInProgress = (data.runState & TEST_FLAG) != 0;
  remainingMs = data.remainingMs;
}

void loadPresetData() {
  PresetData data;
  EEPROM.get(PRESET_EEPROM_ADDRESS, data);
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

// Without a valid record the language stays unchosen and the language screen
// is shown at start-up.
void loadLanguage() {
  LanguageData data;
  EEPROM.get(LANGUAGE_EEPROM_ADDRESS, data);
  if (data.magic == LANGUAGE_EEPROM_MAGIC && data.check == (data.language ^ 0x5A) &&
      data.language <= SPANISH) {
    language = static_cast<Language>(data.language);
    languageSaved = true;
  }
  languageChoice = language;
}

// The home screen opens on the last preset that was started.
void loadLastPreset() {
  LastPresetData data;
  EEPROM.get(LAST_PRESET_EEPROM_ADDRESS, data);
  if (data.magic == LAST_PRESET_EEPROM_MAGIC && data.check == (data.selection ^ 0x3C) &&
      data.selection >= HOME_FIRST_PRESET_SELECTION && data.selection < HOME_MENU_SELECTION) {
    homeSelection = data.selection;
    presetSelection = data.selection;
  }
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

// HH:MM rounded up: 23 h 59 min 59 s is still shown as 24:00.
void formatMinuteDuration(uint32_t milliseconds, char *out, byte length) {
  const uint32_t totalMinutes = (milliseconds + 59999UL) / 60000UL;
  const uint16_t hours = totalMinutes / 60UL;
  const byte minutes = totalMinutes % 60UL;
  snprintf(out, length, "%02u:%02u", hours, minutes);
}

// Countdown text for the big number: HH:MM while a minute or more is left,
// 00:SS in the last minute. Rounded up, so it never shows 00:00 while time is
// left.
void formatClock(uint32_t milliseconds, char *out, byte length) {
  if (milliseconds >= ONE_MINUTE_MS) {
    formatMinuteDuration(milliseconds, out, length);
    return;
  }
  byte seconds = (milliseconds + 999UL) / 1000UL;
  if (seconds > 59) seconds = 59;
  snprintf(out, length, "00:%02u", seconds);
}

// Value that changes when the countdown text changes. The display is only
// redrawn when this changes: once a minute above one minute, once a second
// below.
uint32_t displayTickOf(uint32_t milliseconds) {
  return milliseconds >= ONE_MINUTE_MS ? (milliseconds + 59999UL) / 60000UL
                                       : (milliseconds + 999UL) / 1000UL;
}

// ===========================================================================
// OLED drawing
// ===========================================================================
// Faces are 32x32 monochrome bitmaps stored in flash: four pages of 32 columns,
// each byte holding 8 vertical pixels (bit 0 = top pixel of the page).
// The character is a kefir grain: a cauliflower-shaped outline with a face.
const uint8_t FACE_READY[128] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0xC0, 0x60, 0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x18, 0x0C, 0x02, 0x02,
  0x06, 0x0C, 0x18, 0x30, 0x20, 0x20, 0x10, 0x10, 0x10, 0x30, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x80, 0xC0, 0x60, 0x33, 0x1C, 0x00, 0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xE0, 0xE0, 0xE0, 0x00, 0x00, 0x00, 0x0F, 0x18, 0x30, 0x20, 0xC0, 0x00,
  0x00, 0x03, 0x04, 0x0C, 0x18, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0xCC, 0x06, 0x03, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x08, 0x04, 0x04, 0x0C, 0x18, 0x30, 0x60,
  0x40, 0x40, 0x30, 0x18, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00,
};

const uint8_t FACE_HAPPY[128] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0xC0, 0x60, 0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x18, 0x0C, 0x02, 0x02,
  0x06, 0x0C, 0x18, 0x30, 0x20, 0x20, 0x10, 0x10, 0x10, 0x30, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x80, 0xC0, 0x60, 0x33, 0x1C, 0x00, 0x00, 0x00, 0x10, 0x08, 0x08, 0x08, 0x10, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x10, 0x08, 0x08, 0x08, 0x10, 0x00, 0x00, 0x0F, 0x18, 0x30, 0x20, 0xC0, 0x00,
  0x00, 0x03, 0x04, 0x0C, 0x18, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xE0, 0xE0, 0xE0,
  0xE0, 0xE0, 0xE0, 0xE0, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0xCC, 0x06, 0x03, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x08, 0x04, 0x04, 0x0C, 0x18, 0x30, 0x61,
  0x41, 0x41, 0x30, 0x18, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00,
};

const uint8_t FACE_DONE[128] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0xC0, 0x60, 0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x18, 0x0C, 0x02, 0x02,
  0x06, 0x0C, 0x18, 0x30, 0x20, 0x20, 0x10, 0x10, 0x10, 0x30, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x80, 0xC0, 0x60, 0x33, 0x1C, 0x00, 0x00, 0x40, 0x40, 0x40, 0xF8, 0x40, 0x40, 0x40, 0x00,
  0x00, 0x00, 0x40, 0x40, 0x40, 0xF8, 0x40, 0x40, 0x40, 0x00, 0x0F, 0x18, 0x30, 0x20, 0xC0, 0x00,
  0x00, 0x03, 0x04, 0x0C, 0x18, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x60, 0xE0, 0xE0, 0xE0,
  0xE0, 0xE0, 0xE0, 0xE0, 0x60, 0x03, 0x00, 0x00, 0x00, 0x00, 0x38, 0xCC, 0x06, 0x03, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x08, 0x04, 0x04, 0x0C, 0x18, 0x30, 0x61,
  0x41, 0x41, 0x30, 0x18, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00,
};

const uint8_t FACE_SURPRISED[128] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0xC0, 0x60, 0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x18, 0x0C, 0x02, 0x02,
  0x06, 0x0C, 0x18, 0x30, 0x20, 0x20, 0x10, 0x10, 0x10, 0x30, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x80, 0xC0, 0x60, 0x33, 0x1C, 0x00, 0x00, 0xE0, 0x10, 0x08, 0xE8, 0x08, 0x10, 0xE0, 0x00,
  0x00, 0x00, 0xE0, 0x10, 0x08, 0xE8, 0x08, 0x10, 0xE0, 0x00, 0x0F, 0x18, 0x30, 0x20, 0xC0, 0x00,
  0x00, 0x03, 0x04, 0x0C, 0x18, 0xF0, 0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0x02, 0x01, 0x38, 0x44,
  0x44, 0x44, 0x38, 0x01, 0x02, 0x02, 0x02, 0x01, 0x00, 0x00, 0x38, 0xCC, 0x06, 0x03, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x08, 0x04, 0x04, 0x0C, 0x18, 0x30, 0x60,
  0x40, 0x40, 0x30, 0x18, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00,
};

const uint8_t FACE_WORRIED[128] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0xC0, 0x60, 0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x18, 0x0C, 0x02, 0x02,
  0x06, 0x0C, 0x18, 0x30, 0x20, 0x20, 0x10, 0x10, 0x10, 0x30, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x80, 0xC0, 0x60, 0x33, 0x1C, 0x00, 0x02, 0x02, 0x04, 0xE4, 0xE8, 0xE8, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xE8, 0xE8, 0xE4, 0x04, 0x02, 0x02, 0x0F, 0x18, 0x30, 0x20, 0xC0, 0x00,
  0x00, 0x03, 0x04, 0x0C, 0x18, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x08, 0x08,
  0x08, 0x08, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0xCC, 0x06, 0x03, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x08, 0x04, 0x04, 0x0C, 0x18, 0x30, 0x60,
  0x40, 0x40, 0x30, 0x18, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00,
};

const uint8_t FACE_FOCUS[128] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0xC0, 0x60, 0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x18, 0x0C, 0x02, 0x02,
  0x06, 0x0C, 0x18, 0x30, 0x20, 0x20, 0x10, 0x10, 0x10, 0x30, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x80, 0xC0, 0x60, 0x33, 0x1C, 0x00, 0x00, 0x00, 0x00, 0xE0, 0xE0, 0xE0, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xE0, 0xE0, 0xE0, 0x00, 0x00, 0x00, 0x0F, 0x18, 0x30, 0x20, 0xC0, 0x00,
  0x00, 0x03, 0x04, 0x0C, 0x18, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0xCC, 0x06, 0x03, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x08, 0x04, 0x04, 0x0C, 0x18, 0x30, 0x60,
  0x40, 0x40, 0x30, 0x18, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00,
};

const uint8_t FACE_BLINK[128] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0xC0, 0x60, 0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x18, 0x0C, 0x02, 0x02,
  0x06, 0x0C, 0x18, 0x30, 0x20, 0x20, 0x10, 0x10, 0x10, 0x30, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x80, 0xC0, 0x60, 0x33, 0x1C, 0x00, 0x00, 0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00,
  0x00, 0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x0F, 0x18, 0x30, 0x20, 0xC0, 0x00,
  0x00, 0x03, 0x04, 0x0C, 0x18, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0xCC, 0x06, 0x03, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x08, 0x04, 0x04, 0x0C, 0x18, 0x30, 0x60,
  0x40, 0x40, 0x30, 0x18, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00,
};

// Big word centred on a row (2X font, 12 px per character). The text is picked
// by the current language, so the centring follows its length.
void drawWord(byte row, const __FlashStringHelper *english, const __FlashStringHelper *spanish) {
  const __FlashStringHelper *text = language == SPANISH ? spanish : english;
  const byte length = strlen_P(reinterpret_cast<PGM_P>(text));
  display.setFont(Adafruit5x7);
  display.set2X();
  display.setCursor((SCREEN_WIDTH - 12 * length) / 2, row);
  display.print(text);
  display.set1X();
}

// Title: clears the screen and writes the word on the top rows (pages 0-1).
void drawTitle(const __FlashStringHelper *english, const __FlashStringHelper *spanish) {
  display.clear();
  drawWord(0, english, spanish);
}

// Text in the current language. Used for the small footer lines.
void printText(const __FlashStringHelper *english, const __FlashStringHelper *spanish) {
  display.print(language == SPANISH ? spanish : english);
}

// Preset names.
void drawPresetTitle(byte presetIndex) {
  if (presetIndex == 0) drawTitle(F("QUICK"), F("RAPIDO"));
  else if (presetIndex == 1) drawTitle(F("NORMAL"), F("NORMAL"));
  else drawTitle(F("LONG"), F("LARGO"));
}

// Arrows in the top corners: more options to the left or right.
void drawArrows(bool showLeft, bool showRight) {
  display.setFont(Adafruit5x7);
  display.set2X();
  if (showLeft) { display.setCursor(4, 0); display.print(F("<")); }
  if (showRight) { display.setCursor(116, 0); display.print(F(">")); }
  display.set1X();
}

// Face on the left, pages 2-5. Sprites are written straight to the display.
void drawFace(const uint8_t *sprite) {
  for (byte row = 0; row < 4; ++row) {
    display.setCursor(FACE_COL, FACE_ROW + row);
    for (byte col = 0; col < 32; ++col) {
      display.ssd1306WriteRam(pgm_read_byte(&sprite[row * 32 + col]));
    }
  }
}

// Big number centred in the right-hand area (Verdana digits, pages 3-5).
// Only that area is cleared, so the face and title stay in place.
void drawBigText(const char *text) {
  display.clear(TIME_COL, SCREEN_WIDTH - 1, TIME_ROW, TIME_ROW + 2);
  display.setFont(Verdana_digits_24);
  display.set1X();
  const byte width = display.strWidth(text);
  display.setCursor(TIME_COL + (TIME_WIDTH - width) / 2, TIME_ROW);
  display.print(text);
  display.setFont(Adafruit5x7);
}

// Whole hours as "24 H": large number and a small H, centred in the time area.
void drawHoursText(byte hours) {
  char digits[4];
  snprintf(digits, sizeof(digits), "%u", hours);
  display.clear(TIME_COL, SCREEN_WIDTH - 1, TIME_ROW, TIME_ROW + 2);
  display.setFont(Verdana_digits_24);
  display.set1X();
  const byte digitsWidth = display.strWidth(digits);
  display.setFont(Adafruit5x7);
  display.set2X();
  const byte hourWidth = display.strWidth("H");
  const byte start = TIME_COL + (TIME_WIDTH - digitsWidth - hourWidth - 4) / 2;

  display.setFont(Verdana_digits_24);
  display.set1X();
  display.setCursor(start, TIME_ROW);
  display.print(digits);
  display.setFont(Adafruit5x7);
  display.set2X();
  display.setCursor(start + digitsWidth + 4, TIME_ROW + 1);
  display.print(F("H"));
  display.set1X();
}

// A duration in minutes, as "24 H" for whole hours or HH:MM otherwise.
void drawMinutesText(uint16_t minutes) {
  if (minutes % 60 == 0) {
    drawHoursText(minutes / 60);
    return;
  }
  char text[8];
  formatMinuteDuration(static_cast<uint32_t>(minutes) * 60000UL, text, sizeof(text));
  drawBigText(text);
}

// Progress bar on the bottom row: filled as the run goes on. The bar is an
// outline with a solid fill, drawn in one row of pages.
void drawProgress() {
  byte filled = 0;
  if (totalMs > 0 && remainingMs < totalMs) {
    const uint32_t elapsed = totalMs - remainingMs;
    const uint32_t step = totalMs / BAR_WIDTH + 1;  // Avoids 32-bit overflow.
    filled = elapsed / step;
  }
  display.setCursor(BAR_COL, BAR_ROW);
  for (byte i = 0; i < BAR_WIDTH; ++i) {
    display.ssd1306WriteRam(i < filled ? 0x3C : 0x24);
  }
}

// Short hint along the bottom row, in the current language.
void drawFooter(const __FlashStringHelper *english, const __FlashStringHelper *spanish) {
  display.setFont(Adafruit5x7);
  display.set1X();
  display.setCursor(FOOTER_COL, BAR_ROW);
  printText(english, spanish);
}

// Language picker, shown until a language has been chosen. Both names are
// shown in their own language so the choice is clear to either reader.
void drawLanguagePicker() {
  drawTitle(F("LANGUAGE"), F("LANGUAGE"));
  display.setFont(Adafruit5x7);
  display.set2X();
  if (languageChoice == ENGLISH) { display.setCursor(22, 3); display.print(F("ENGLISH")); }
  else { display.setCursor(30, 3); display.print(F("ESPANOL")); }
  display.set1X();
  drawArrows(true, true);
  display.setCursor(FOOTER_COL, BAR_ROW);
  display.print(F("UP/DN  OK = SELECT"));
}

// Declared here because drawScreen() and the timer use them before their
// definitions below.
void drawSpriteAt(int x, int y, const uint8_t *sprite);
void drawClock();
void startAnimation(Animation kind);
void stopAnimation();

// Draws the full screen for the current `screen`.
void drawScreen() {
  if (!displayAvailable) return;
  char text[10];
  switch (screen) {
    case LANGUAGE_PICK:
      drawLanguagePicker();
      break;
    case HOME:
      if (homeSelection == HOME_TEST_SELECTION) {
        drawTitle(F("TEST"), F("PRUEBA"));
        drawFace(FACE_READY);
        formatClock(MECHANISM_TEST_FERMENTATION_MS, text, sizeof(text));
        drawBigText(text);
        drawFooter(F("OK = START"), F("OK = INICIAR"));
      } else if (homeSelection == HOME_MENU_SELECTION) {
        drawTitle(F("MENU"), F("MENU"));
        drawFace(FACE_FOCUS);
        drawFooter(F("OK = OPEN"), F("OK = ABRIR"));
      } else {
        const byte presetIndex = homeSelection - HOME_FIRST_PRESET_SELECTION;
        drawPresetTitle(presetIndex);
        drawFace(FACE_READY);
        drawMinutesText(presetDurationMinutes[presetIndex]);
        drawFooter(F("OK START  HOLD EDIT"), F("OK INICIA  MANT EDIT"));
      }
      drawArrows(homeSelection > HOME_TEST_SELECTION, homeSelection < HOME_OPTION_COUNT - 1);
      break;
    case EDIT_PRESET_DURATION:
      drawPresetTitle(homeSelection - HOME_FIRST_PRESET_SELECTION);
      drawFace(FACE_FOCUS);
      drawMinutesText(presetDurationMinutes[homeSelection - HOME_FIRST_PRESET_SELECTION]);
      drawFooter(F("UP/DN 1H  OK SAVE"), F("+/- 1H  OK GUARDAR"));
      break;
    case ADVANCED_MENU:
      drawTitle(F("OPTIONS"), F("OPCIONES"));
      if (advancedSelection == MENU_TIME) drawWord(3, F("TIME"), F("HORA"));
      else if (advancedSelection == MENU_SERVO) drawWord(3, F("SERVO"), F("SERVO"));
      else if (advancedSelection == MENU_LANGUAGE) drawWord(3, F("LANGUAGE"), F("IDIOMA"));
      else drawWord(3, F("EXIT"), F("SALIR"));
      drawArrows(true, true);
      if (advancedSelection == MENU_LANGUAGE) {
        // The footer names the language that OK switches to.
        drawFooter(F("OK = ESPANOL"), F("OK = ENGLISH"));
      } else {
        drawFooter(F("UP/DN  OK = CHOOSE"), F("+/-  OK = ELEGIR"));
      }
      break;
    case TIME_PICK:
      if (timeChoice < PRESET_COUNT) {
        drawPresetTitle(timeChoice);
        drawFace(FACE_FOCUS);
        drawMinutesText(presetDurationMinutes[timeChoice]);
        drawFooter(F("OK = EDIT TIME"), F("OK = EDITAR"));
      } else {
        drawTitle(F("EXIT"), F("SALIR"));
        drawFooter(F("OK = BACK"), F("OK = VOLVER"));
      }
      drawArrows(timeChoice > 0, timeChoice < PRESET_COUNT);
      break;
    case TIME_EDIT:
      drawPresetTitle(timeChoice);
      drawFace(FACE_FOCUS);
      blinkVisible = true;
      lastBlinkMs = millis();
      drawMinutesText(presetDurationMinutes[timeChoice]);
      drawFooter(F("UP/DN 30M  OK SAVE"), F("+/- 30M  OK GUARDAR"));
      break;
    case SETTINGS:
      drawTitle(F("SERVO"), F("SERVO"));
      if (settingsSelection == 0) drawWord(3, F("CLOSED"), F("CERRADO"));
      else if (settingsSelection == 1) drawWord(3, F("OPEN"), F("ABIERTO"));
      else if (settingsSelection == 2) drawWord(3, F("HOLD"), F("MANTENER"));
      else drawWord(3, F("EXIT"), F("SALIR"));
      drawArrows(true, true);
      break;
    case EDIT_HOME_ANGLE:
    case EDIT_RELEASE_ANGLE:
    case EDIT_RELEASE_TIME:
      if (screen == EDIT_HOME_ANGLE) { drawTitle(F("CLOSED"), F("CERRADO")); snprintf(text, sizeof(text), "%u", homeAngle); }
      else if (screen == EDIT_RELEASE_ANGLE) { drawTitle(F("OPEN"), F("ABIERTO")); snprintf(text, sizeof(text), "%u", releaseAngle); }
      else { drawTitle(F("HOLD S"), F("PAUSA S")); snprintf(text, sizeof(text), "%u", releaseSeconds); }
      drawFace(FACE_FOCUS);
      drawBigText(text);
      drawFooter(F("UP/DN  OK SAVE"), F("+/-  OK GUARDAR"));
      break;
    case RUNNING_SCREEN:
      if (shortTestInProgress) drawTitle(F("TEST"), F("PRUEBA"));
      else drawTitle(F("ACTIVE"), F("ACTIVO"));
      drawFace(FACE_HAPPY);
      formatClock(remainingMs, text, sizeof(text));
      drawBigText(text);
      drawProgress();
      break;
    case DONE:
      if (releaseInProgress) {
        drawTitle(F("RELEASING"), F("LIBERANDO"));
        drawFace(FACE_SURPRISED);
        drawFooter(F("LIFTING THE GRAINS"), F("LEVANTANDO GRANOS"));
      } else {
        // Only the character, no text.
        display.clear();
        drawSpriteAt(48, 16, FACE_DONE);
      }
      break;
    case NO_TIME:
      drawTitle(F("NO TIME"), F("SIN TIEMPO"));
      drawFace(FACE_WORRIED);
      drawFooter(F("SET A TIME FIRST"), F("PON UN TIEMPO"));
      break;
  }
}

// Returns true when something changed that requires a full redraw, and
// remembers the current state as "already drawn".
bool needsFullRedraw() {
  const uint16_t presetNow = presetDurationMinutes[presetSelection - HOME_FIRST_PRESET_SELECTION];
  const uint16_t timeValueNow = timeChoice < PRESET_COUNT ? presetDurationMinutes[timeChoice] : 0;
  const bool changed =
      screen != lastRenderedScreen ||
      homeSelection != lastRenderedHomeSelection ||
      advancedSelection != lastRenderedAdvancedSelection ||
      settingsSelection != lastRenderedSettingsSelection ||
      language != lastRenderedLanguage ||
      languageChoice != lastRenderedLanguageChoice ||
      durationMinutes != lastRenderedDuration ||
      presetNow != lastRenderedPreset ||
      timeChoice != lastRenderedTimeChoice ||
      timeValueNow != lastRenderedTimeValue ||
      homeAngle != lastRenderedHomeAngle ||
      releaseAngle != lastRenderedReleaseAngle ||
      releaseSeconds != lastRenderedReleaseSeconds ||
      releaseInProgress != lastRenderedReleaseInProgress;
  if (changed) {
    lastRenderedScreen = screen;
    lastRenderedHomeSelection = homeSelection;
    lastRenderedAdvancedSelection = advancedSelection;
    lastRenderedSettingsSelection = settingsSelection;
    lastRenderedLanguage = language;
    lastRenderedLanguageChoice = languageChoice;
    lastRenderedDuration = durationMinutes;
    lastRenderedPreset = presetNow;
    lastRenderedTimeChoice = timeChoice;
    lastRenderedTimeValue = timeValueNow;
    lastRenderedHomeAngle = homeAngle;
    lastRenderedReleaseAngle = releaseAngle;
    lastRenderedReleaseSeconds = releaseSeconds;
    lastRenderedReleaseInProgress = releaseInProgress;
    lastRenderedTick = displayTickOf(remainingMs);
  }
  return changed;
}

// The countdown text and the progress bar.
void drawClock() {
  char text[10];
  formatClock(remainingMs, text, sizeof(text));
  drawBigText(text);
  drawProgress();
}

// During the countdown only the time and the progress bar are refreshed, when
// the displayed value changes. Each whole minute plays a short animation first.
void updateTimerText() {
  if (!displayAvailable || screen != RUNNING_SCREEN) return;
  const uint32_t tick = displayTickOf(remainingMs);
  if (tick == lastRenderedTick) return;
  lastRenderedTick = tick;
  if (animKind == ANIM_NONE && remainingMs >= ONE_MINUTE_MS) {
    startAnimation(ANIM_TICK);
    return;
  }
  drawClock();
}

// Blinks the time while editing, like a clock in setting mode. Only the time
// area is redrawn, so the rest of the screen stays still.
void updateBlink(uint32_t now) {
  if (!displayAvailable || displayMode != DISPLAY_AWAKE || screen != TIME_EDIT) return;
  if (now - lastBlinkMs < BLINK_MS) return;
  lastBlinkMs = now;
  blinkVisible = !blinkVisible;
  if (blinkVisible) drawMinutesText(presetDurationMinutes[timeChoice]);
  else display.clear(TIME_COL, SCREEN_WIDTH - 1, TIME_ROW, TIME_ROW + 2);
}

// Dimmed view while fermenting: only the time left, centred.
void drawDimTime() {
  char text[10];
  formatClock(remainingMs, text, sizeof(text));
  display.clear();
  display.setFont(Verdana_digits_24);
  display.set1X();
  const byte width = display.strWidth(text);
  display.setCursor((SCREEN_WIDTH - width) / 2, 2);
  display.print(text);
  display.setFont(Adafruit5x7);
  lastDimTick = displayTickOf(remainingMs);
}

// Dimmed "finished" view: the word DONE / LISTO, nothing else.
void drawDimDone() {
  display.clear();
  drawSpriteAt(48, 16, FACE_DONE);
}

// Redraws the dimmed time when the displayed value changes.
void updateDimTime() {
  if (screen == DONE) return;
  if (displayTickOf(remainingMs) != lastDimTick) {
    drawDimTime();
  }
}

// Dims to the lowest contrast: the time left while fermenting, or DONE after a
// finished cycle.
void enterDim() {
  stopAnimation();
  displayMode = DISPLAY_DIM;
  sleepScreen = screen;
  display.setContrast(CONTRAST_DIM);
  if (screen == DONE) drawDimDone();
  else drawDimTime();
}

// Turns the panel off (idle screens have nothing worth showing in the dark).
void sleepDisplay() {
  stopAnimation();
  displayMode = DISPLAY_OFF;
  sleepScreen = screen;
  display.ssd1306WriteCmd(CMD_DISPLAY_OFF);
}

// Full brightness and full screen. The redraw is forced on the next loop().
void wakeDisplay() {
  displayMode = DISPLAY_AWAKE;
  lastActivityMs = millis();
  display.setContrast(CONTRAST_FULL);
  display.ssd1306WriteCmd(CMD_DISPLAY_ON);
  lastRenderedScreen = static_cast<Screen>(255);
}


// ===========================================================================
// Animations
// ===========================================================================
// The character is a kefir grain called KEFI. Each animation is a short list
// of frames. runAnimation() draws one frame when its delay has passed, so the
// buttons keep working between frames. Frames only redraw the face area (or
// the whole screen when the character moves across it), which keeps them fast.

// Draws a 32x32 sprite at any pixel position, also partly off-screen. Each
// page of the display gets the bits of the sprite that fall inside it, so the
// sprite can move one pixel at a time.
void drawSpriteAt(int x, int y, const uint8_t *sprite) {
  const int firstPage = y >= 0 ? y / 8 : 0;
  const int lastPage = (y + 31) / 8 > 7 ? 7 : (y + 31) / 8;
  const int firstCol = x < 0 ? 0 : x;
  const int lastCol = x + 31 > 127 ? 127 : x + 31;
  if (firstCol > lastCol) return;
  for (int page = firstPage; page <= lastPage; ++page) {
    display.setCursor(firstCol, page);
    for (int col = firstCol; col <= lastCol; ++col) {
      const int spriteCol = col - x;
      byte value = 0;
      for (byte bit = 0; bit < 8; ++bit) {
        const int row = page * 8 + bit - y;
        if (row < 0 || row > 31) continue;
        if ((pgm_read_byte(&sprite[(row >> 3) * 32 + spriteCol]) >> (row & 7)) & 1) value |= 1 << bit;
      }
      display.ssd1306WriteRam(value);
    }
  }
}

// Clears the face area (left side, below the title) and draws the face there.
void clearFaceArea() { display.clear(0, 47, FACE_ROW, FACE_ROW + 4); }

// Clears the band below the title (the time area included).
void clearStage() { display.clear(0, SCREEN_WIDTH - 1, FACE_ROW, FACE_ROW + 4); }

// Small sparkle at column x, on the given page.
void drawSparkle(byte x, byte page) {
  display.setCursor(x - 2, page); display.ssd1306WriteRam(0x10);
  display.setCursor(x, page); display.ssd1306WriteRam(0x38);
  display.setCursor(x + 2, page); display.ssd1306WriteRam(0x10);
}

void startAnimation(Animation kind) {
  animKind = kind;
  animScreen = screen;
  animStep = 0;
  animNextMs = millis();
}

void stopAnimation() { animKind = ANIM_NONE; }

// Frame functions return the delay before the next frame, or 0 when finished.
uint16_t frameSplash() {
  static const int8_t bounce[] = {64, 52, 42, 34, 26, 20, 16};
  if (animStep < 7) {
    display.clear();
    drawSpriteAt(48, bounce[animStep], FACE_READY);
    return 90;
  }
  if (animStep < 9) {
    display.clear();
    drawSpriteAt(48, animStep == 7 ? 18 : 16, FACE_HAPPY);
    return 120;
  }
  if (animStep == 9) {
    display.clear();
    drawSpriteAt(48, 16, FACE_HAPPY);
    display.setFont(Adafruit5x7);
    display.set2X();
    display.setCursor(40, 6);
    display.print(F("KEFI"));
    display.set1X();
    return 1200;
  }
  animKind = ANIM_NONE;
  drawScreen();
  needsFullRedraw();
  return 0;
}

// The grain grows out of the middle of the screen, hops, winks and slides to
// its place on the left, where the time then appears.
uint16_t frameStart() {
  switch (animStep) {
    case 0: display.clear(); drawSpriteAt(48, 16, FACE_READY); return 130;
    case 1: clearStage(); drawSpriteAt(48, 19, FACE_HAPPY); return 130;
    case 2: clearStage(); drawSpriteAt(48, 16, FACE_HAPPY); return 130;
    case 3: clearStage(); drawSpriteAt(40, 16, FACE_HAPPY); return 130;
    case 4: clearStage(); drawSpriteAt(24, 16, FACE_HAPPY); return 130;
    case 5: clearStage(); drawSpriteAt(8, 16, FACE_HAPPY); return 130;
    case 6: clearStage(); drawSpriteAt(8, 16, FACE_READY); return 150;
    default:
      animKind = ANIM_NONE;
      drawScreen();
      needsFullRedraw();
      return 0;
  }
}

// One minute has passed: the grain winks and hops, then the new time shows.
uint16_t frameTick() {
  switch (animStep) {
    case 0: clearFaceArea(); drawSpriteAt(8, 16, FACE_READY); return 150;
    case 1: clearFaceArea(); drawSpriteAt(8, 18, FACE_HAPPY); return 150;
    default:
      clearFaceArea();
      drawSpriteAt(8, 16, FACE_HAPPY);
      drawClock();
      animKind = ANIM_NONE;
      return 0;
  }
}

// A quick blink while the grain is idle.
uint16_t frameBlink() {
  if (animStep == 0) {
    clearFaceArea();
    drawSpriteAt(8, 16, FACE_BLINK);
    return 140;
  }
  clearFaceArea();
  drawSpriteAt(8, 16, screen == RUNNING_SCREEN ? FACE_HAPPY : FACE_READY);
  animKind = ANIM_NONE;
  return 0;
}

// Shakes while the grains are released, or once when there is no time set.
uint16_t frameShake() {
  static const int8_t shake[] = {-2, 2, -2, 2, 0};
  if (screen == NO_TIME && animStep >= 8) {
    animKind = ANIM_NONE;
    drawScreen();
    needsFullRedraw();
    return 0;
  }
  const bool centred = screen == DONE && !releaseInProgress;
  const int baseX = centred ? 48 : 8;
  const uint8_t *sprite = releaseInProgress ? FACE_SURPRISED
                        : (screen == NO_TIME ? FACE_WORRIED : FACE_DONE);
  clearStage();
  drawSpriteAt(baseX + shake[animStep % 5], 16, sprite);
  return 90;
}

// After a finished cycle: the grain bobs in the middle and sparkles.
uint16_t frameDone() {
  clearStage();
  const int y = (animStep & 1) ? 18 : 16;
  drawSpriteAt(48, y, FACE_DONE);
  if (animStep & 1) {
    drawSparkle(28, 3);
    drawSparkle(100, 2);
    drawSparkle(96, 5);
  }
  return 180;
}

// Runs the current animation, one frame at a time.
void runAnimation(uint32_t now) {
  if (animKind == ANIM_NONE || now < animNextMs) return;
  if (screen != animScreen) {
    animKind = ANIM_NONE;
    return;
  }
  uint16_t delayMs = 0;
  switch (animKind) {
    case ANIM_SPLASH: delayMs = frameSplash(); break;
    case ANIM_START: delayMs = frameStart(); break;
    case ANIM_TICK: delayMs = frameTick(); break;
    case ANIM_BLINK: delayMs = frameBlink(); break;
    case ANIM_SHAKE: delayMs = frameShake(); break;
    case ANIM_DONE_FX: delayMs = frameDone(); break;
    default: break;
  }
  ++animStep;
  if (delayMs == 0) animKind = ANIM_NONE;
  else animNextMs = millis() + delayMs;
}

// Starts a blink every few seconds while the grain is on screen.
void scheduleBlink(uint32_t now) {
  if (animKind != ANIM_NONE || displayMode != DISPLAY_AWAKE || now < nextBlinkMs) return;
  const bool grainOnScreen = screen == RUNNING_SCREEN ||
      (screen == HOME && homeSelection != HOME_MENU_SELECTION);
  if (!grainOnScreen) return;
  nextBlinkMs = now + 3000UL + now % 2000UL;
  startAnimation(ANIM_BLINK);
}

// ===========================================================================
// Servo and fermentation cycle
// ===========================================================================
// The servo only draws power while it is attached. At the closed position it
// holds nothing, so it is detached after it has settled there. It is attached
// again for the release, the only time it has to hold force (the plunger).
const uint16_t SERVO_SETTLE_MS = 800;
bool servoAttached = false;
uint32_t servoDetachMs = 0;  // 0 = nothing scheduled.

void servoAttach() {
  if (servoAttached) return;
  releaseServo.attach(PIN_SERVO);
  servoAttached = true;
}

void servoDetach() {
  if (servoAttached) releaseServo.detach();
  servoAttached = false;
  servoDetachMs = 0;
}

void moveServoHome() {
  servoAttach();
  releaseServo.write(homeAngle);
  servoDetachMs = millis() + SERVO_SETTLE_MS;
}

void moveServoRelease() {
  servoAttach();
  releaseServo.write(releaseAngle);
  servoDetachMs = 0;  // Keep the force on for the whole release.
}

// Moves the servo to the release position. loop() returns it to the closed
// position after `releaseSeconds` seconds (finishRelease()).
void beginRelease(bool isTest) {
  // One release at a time: a second request while the servo is already
  // moving must not move it again.
  if (releaseInProgress) return;
  releaseIsTest = isTest;
  releaseInProgress = true;
  releaseStartedMs = millis();
  moveServoRelease();
  screen = DONE;
  startAnimation(ANIM_SHAKE);
  Serial.println(isTest ? F("Release test started.") : F("Releasing the grains."));
}

void finishRelease() {
  moveServoHome();
  releaseInProgress = false;
  lastActivityMs = millis();
  animKind = ANIM_NONE;
  if (releaseIsTest) {
    screen = HOME;
    Serial.println(F("Test finished: servo in closed position."));
  } else {
    runState = IDLE;
    remainingMs = 0;
    savePersistentData();
    screen = DONE;
    startAnimation(ANIM_DONE_FX);
    Serial.println(F("Release finished: servo in closed position."));
  }
}

// Shown instead of starting when there is no time set (0 minutes).
void showNoTime() {
  screen = NO_TIME;
  noTimeStartedMs = millis();
  startAnimation(ANIM_SHAKE);
  Serial.println(F("No time set: choose a duration before starting."));
}

void startFermentation() {
  if (durationMinutes == 0) {
    showNoTime();
    return;
  }
  remainingMs = static_cast<uint32_t>(durationMinutes) * 60000UL;
  totalMs = remainingMs;
  shortTestInProgress = false;
  runState = RUNNING;
  lastTickMs = lastSaveMs = millis();
  savePersistentData();
  screen = RUNNING_SCREEN;
  startAnimation(ANIM_START);
  Serial.println(F("Fermentation started."));
}

void startMechanismTest() {
  remainingMs = MECHANISM_TEST_FERMENTATION_MS;
  totalMs = remainingMs;
  shortTestInProgress = true;
  runState = RUNNING;
  lastTickMs = lastSaveMs = millis();
  savePersistentData();
  screen = RUNNING_SCREEN;
  startAnimation(ANIM_START);
  Serial.println(F("5-second test started."));
}

// Cancelling stops the fermentation and releases the grains now, exactly like
// the timer does when it reaches zero.
void cancelFermentation() {
  runState = IDLE;
  remainingMs = 0;
  shortTestInProgress = false;
  savePersistentData();
  beginRelease(false);
}

// Adds time to the running fermentation. The 72-hour limit still applies.
void addFermentationTime(uint16_t minutes) {
  const uint32_t extraMs = static_cast<uint32_t>(minutes) * 60000UL;
  const uint32_t limitMs = static_cast<uint32_t>(MAX_DURATION_MINUTES) * 60000UL;
  if (totalMs + extraMs > limitMs) {
    Serial.println(F("Cannot add time: the limit is 72 hours."));
    return;
  }
  remainingMs += extraMs;
  totalMs += extraMs;
  savePersistentData();
  Serial.println(F("30 minutes added."));
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
  Serial.println(F("ADD                      Add 30 minutes to the running fermentation"));
  Serial.println(F("CANCEL                   Cancel and release the grains now"));
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
    if (runState != IDLE) Serial.println(F("A fermentation is already running."));
    else if (arg1 == NULL || setDurationFromArguments(arg1, arg2)) startFermentation();
    return;
  }
  if (isCommand(command, "ADD", "AGREGAR")) {
    if (runState == RUNNING && !shortTestInProgress) addFermentationTime(ADD_TIME_MINUTES);
    else Serial.println(F("There is no fermentation running to add time to."));
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
// Select acts on release for the menus and the timer, so the release of the
// press that opened a screen can never trigger an action on the next screen.
void startPresetFermentation() {
  presetSelection = homeSelection;
  saveLastPreset();
  durationMinutes = presetDurationMinutes[homeSelection - HOME_FIRST_PRESET_SELECTION];
  startFermentation();
}

void handleButtons() {
  if (releaseInProgress) return;
  if (screen == LANGUAGE_PICK) {
    if (buttonUp.pressed() || buttonDown.pressed()) languageChoice = !languageChoice;
    if (buttonSelect.shortReleased()) {
      language = static_cast<Language>(languageChoice);
      saveLanguage();
      screen = HOME;
    }
    return;
  }
  if (screen == NO_TIME) {
    if (millis() - noTimeStartedMs >= NO_TIME_MESSAGE_MS || buttonSelect.shortReleased() ||
        buttonUp.pressed() || buttonDown.pressed()) {
      screen = HOME;
    }
    return;
  }
  if (screen == RUNNING_SCREEN) {
    // A 1.5 s hold cancels (and releases the grains). UP adds 30 minutes,
    // except during the 5-second test.
    if (buttonSelect.longPressed()) cancelFermentation();
    else if (buttonUp.pressed() && !shortTestInProgress) addFermentationTime(ADD_TIME_MINUTES);
    return;
  }
  // The finished screen dims on its own; handled in loop(), where any press
  // returns to the home screen.
  if (screen == DONE) return;
  if (screen == HOME) {
    if (buttonUp.longPressed() || buttonDown.longPressed()) {
      advancedSelection = MENU_TIME;
      screen = ADVANCED_MENU;
      return;
    }
    if (buttonUp.shortReleased() && homeSelection > HOME_TEST_SELECTION) --homeSelection;
    if (buttonDown.shortReleased() && homeSelection < HOME_OPTION_COUNT - 1) ++homeSelection;
    if (homeSelection >= HOME_FIRST_PRESET_SELECTION && homeSelection < HOME_MENU_SELECTION) {
      presetSelection = homeSelection;
    }
    // Holding select on a preset opens its editor; a short press starts it.
    if (buttonSelect.longPressed() && homeSelection >= HOME_FIRST_PRESET_SELECTION &&
        homeSelection < HOME_MENU_SELECTION) {
      screen = EDIT_PRESET_DURATION;
    } else if (buttonSelect.shortReleased()) {
      if (homeSelection == HOME_TEST_SELECTION) startMechanismTest();
      else if (homeSelection == HOME_MENU_SELECTION) {
        advancedSelection = MENU_TIME;
        screen = ADVANCED_MENU;
      } else startPresetFermentation();
    }
    return;
  }
  if (screen == EDIT_PRESET_DURATION) {
    uint16_t &selectedDuration = presetDurationMinutes[homeSelection - HOME_FIRST_PRESET_SELECTION];
    if (buttonUp.pressed() && selectedDuration + 60 <= MAX_DURATION_MINUTES) selectedDuration += 60;
    if (buttonDown.pressed() && selectedDuration > MIN_DURATION_MINUTES) {
      selectedDuration = selectedDuration >= 60 ? selectedDuration - 60 : 0;
    }
    if (buttonSelect.shortReleased()) {
      savePresetData();
      screen = HOME;
    }
    return;
  }
  if (screen == ADVANCED_MENU) {
    if (buttonUp.pressed()) advancedSelection = (advancedSelection + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
    if (buttonDown.pressed()) advancedSelection = (advancedSelection + 1) % MENU_ITEM_COUNT;
    if (buttonSelect.shortReleased()) {
      if (advancedSelection == MENU_TIME) {
        timeChoice = presetSelection - HOME_FIRST_PRESET_SELECTION;
        screen = TIME_PICK;
      }
      else if (advancedSelection == MENU_SERVO) { settingsSelection = 0; screen = SETTINGS; }
      else if (advancedSelection == MENU_LANGUAGE) {
        language = language == ENGLISH ? SPANISH : ENGLISH;
        languageChoice = language;
        saveLanguage();
      } else screen = HOME;
    }
    return;
  }
  if (screen == TIME_PICK) {
    // Choose QUICK, NORMAL or LONG; OK starts editing it. EXIT goes back.
    if (buttonUp.pressed() && timeChoice > 0) --timeChoice;
    if (buttonDown.pressed() && timeChoice < PRESET_COUNT) ++timeChoice;
    if (buttonSelect.shortReleased()) {
      if (timeChoice < PRESET_COUNT) screen = TIME_EDIT;
      else screen = ADVANCED_MENU;
    }
    return;
  }
  if (screen == TIME_EDIT) {
    // The time blinks. Up/down change it; OK saves it and returns to the list.
    // Nothing starts.
    uint16_t &minutes = presetDurationMinutes[timeChoice];
    if (buttonUp.pressed() && minutes + 30 <= MAX_DURATION_MINUTES) minutes += 30;
    if (buttonDown.pressed() && minutes > MIN_DURATION_MINUTES) {
      minutes = minutes >= 30 ? minutes - 30 : 0;
    }
    if (buttonSelect.shortReleased()) {
      savePresetData();
      screen = TIME_PICK;
    }
    return;
  }
  if (screen == SETTINGS) {
    if (buttonUp.pressed()) settingsSelection = (settingsSelection + 3) % 4;
    if (buttonDown.pressed()) settingsSelection = (settingsSelection + 1) % 4;
    if (buttonSelect.shortReleased()) {
      if (settingsSelection == 0) screen = EDIT_HOME_ANGLE;
      else if (settingsSelection == 1) screen = EDIT_RELEASE_ANGLE;
      else if (settingsSelection == 2) screen = EDIT_RELEASE_TIME;
      else screen = ADVANCED_MENU;
    }
    return;
  }
  if (screen == EDIT_HOME_ANGLE) {
    // The servo follows the value, but only when it changes.
    if (buttonUp.pressed() && homeAngle < 180) { ++homeAngle; moveServoHome(); }
    if (buttonDown.pressed() && homeAngle > 0) { --homeAngle; moveServoHome(); }
  } else if (screen == EDIT_RELEASE_ANGLE) {
    if (buttonUp.pressed() && releaseAngle < 180) ++releaseAngle;
    if (buttonDown.pressed() && releaseAngle > 0) --releaseAngle;
  } else if (screen == EDIT_RELEASE_TIME) {
    if (buttonUp.pressed() && releaseSeconds < 10) ++releaseSeconds;
    if (buttonDown.pressed() && releaseSeconds > 1) --releaseSeconds;
  }
  if (buttonSelect.shortReleased()) { savePersistentData(); screen = SETTINGS; }
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
  loadLanguage();
  loadLastPreset();
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
  moveServoHome();
  totalMs = shortTestInProgress ? MECHANISM_TEST_FERMENTATION_MS
                                : static_cast<uint32_t>(durationMinutes) * 60000UL;
  // If power was lost during a fermentation, resume from the last saved
  // point.
  if (runState == RUNNING && remainingMs > 0) {
    screen = RUNNING_SCREEN;
    lastTickMs = lastSaveMs = millis();
  } else {
    runState = IDLE;
    shortTestInProgress = false;
    screen = HOME;
  }
  // First start: ask for the language before anything else.
  if (!languageSaved) screen = LANGUAGE_PICK;
  lastActivityMs = millis();
  drawScreen();
  needsFullRedraw();
  nextBlinkMs = millis() + 3000UL;
  if (displayAvailable) startAnimation(ANIM_SPLASH);  // KEFI says hello
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

  // Any button press or serial command counts as activity. A press while the
  // display is dimmed or off only wakes it: that press is not acted on.
  const bool anyPressed = buttonUp.pressed() || buttonDown.pressed() || buttonSelect.pressed();
  if (anyPressed || Serial.available()) {
    lastActivityMs = now;
    if (displayMode != DISPLAY_AWAKE) {
      wakeDisplay();
      wakeHeld = anyPressed;
    }
  }
  // After a finished cycle, any press goes back to the home screen. The press
  // is then ignored until it is released, so select cannot start a preset.
  if (anyPressed && screen == DONE && !releaseInProgress) {
    screen = HOME;
    wakeHeld = true;
  }
  const bool ignoreButtons = wakeHeld;
  if (wakeHeld && !buttonUp.isDown() && !buttonDown.isDown() && !buttonSelect.isDown()) {
    wakeHeld = false;
  }
  readSerialCommands();
  updateTimer(now);
  // updateTimer() may start the release in this same iteration, so millis()
  // is read again: `now` is older than releaseStartedMs, and the unsigned
  // subtraction would wrap around and close the servo immediately.
  const uint32_t releaseNow = millis();
  if (releaseInProgress && releaseNow - releaseStartedMs >= static_cast<uint32_t>(releaseSeconds) * 1000UL) {
    finishRelease();
  }
  // Something happened while the display was dimmed or off (the timer ended,
  // a serial command changed the screen): show it.
  if (displayMode != DISPLAY_AWAKE && (releaseInProgress || screen != sleepScreen)) {
    wakeDisplay();
  }
  if (!ignoreButtons) handleButtons();
  // Disconnect the servo once it has settled at the closed position. While
  // calibrating the angles it stays connected so that it follows the value.
  const bool calibrating = screen == SETTINGS || screen == EDIT_HOME_ANGLE ||
                           screen == EDIT_RELEASE_ANGLE || screen == EDIT_RELEASE_TIME;
  if (servoAttached && servoDetachMs != 0 && !releaseInProgress && !calibrating &&
      now >= servoDetachMs) {
    servoDetach();
  }
  if (displayAvailable && displayMode == DISPLAY_AWAKE && !releaseInProgress &&
      releaseNow - lastActivityMs >= IDLE_TIMEOUT_MS) {
    if (screen == RUNNING_SCREEN || screen == DONE) enterDim();
    else sleepDisplay();
  }
  if (displayMode == DISPLAY_AWAKE) {
    if (needsFullRedraw()) {
      drawScreen();
    } else {
      updateTimerText();
      updateBlink(now);
    }
    scheduleBlink(now);
    runAnimation(now);
  } else if (displayMode == DISPLAY_DIM) {
    updateDimTime();
  }
}
