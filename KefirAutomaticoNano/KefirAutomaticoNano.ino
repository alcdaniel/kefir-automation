/**
 * Sketch Kefir Automatico para Arduino Nano clasico (ATmega328P, 5 V / 16 MHz).
 *
 * OLED SSD1306 128x64 por I2C, servo de 5 V, botones opcionales y consola
 * serie a 115200 baudios. La configuracion y el temporizador se guardan en
 * EEPROM; tras un corte de corriente se reanuda desde el ultimo minuto
 * guardado, sin descontar el tiempo que estuvo apagado.
 */
#include <Wire.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
#include <Servo.h>
#include <EEPROM.h>
#include <ctype.h>

// ----- Cableado Arduino Nano ----------------------------------------------
const byte PIN_SERVO = 9;
const byte PIN_BUTTON_UP = 2;
const byte PIN_BUTTON_DOWN = 3;
const byte PIN_BUTTON_SELECT = 4;
// Las librerias Arduino usan direcciones I2C de 7 bits. 0x7B no es una
// direccion SSD1306 valida: es la forma de 8 bits para lectura de 0x3D.
const byte OLED_PRIMARY_ADDRESS = 0x3C;
const byte OLED_ALTERNATIVE_ADDRESS = 0x3D;
const byte SCREEN_WIDTH = 128;
const byte SCREEN_HEIGHT = 64;

const unsigned long SAVE_INTERVAL_MS = 60000UL;
const unsigned long LONG_PRESS_MS = 1500UL;
const unsigned int MIN_DURATION_MINUTES = 0;
const unsigned int MAX_DURATION_MINUTES = 72 * 60;
const byte PRESET_COUNT = 3;
const byte HOME_TEST_SELECTION = 0;
const byte HOME_FIRST_PRESET_SELECTION = 1;
const byte HOME_OPTION_COUNT = PRESET_COUNT + 1;
const uint16_t DEFAULT_PRESET_DURATION_MINUTES[PRESET_COUNT] = {12 * 60, 24 * 60, 36 * 60};
const uint32_t MECHANISM_TEST_FERMENTATION_MS = 5000UL;
const byte SERIAL_BUFFER_SIZE = 64;
// KFR2 reinicia los antiguos valores almacenados para aplicar los nuevos
// angulos por defecto (cerrado 0 grados, abierto 60 grados).
const uint32_t EEPROM_MAGIC = 0x4B465232UL;  // "KFR2"

SSD1306AsciiWire display;
Servo releaseServo;

enum RunState : byte { IDLE, RUNNING, PAUSED };
enum Screen : byte {
  HOME, EDIT_PRESET_DURATION, ADVANCED_MENU, DURATION, SETTINGS, EDIT_HOME_ANGLE,
  EDIT_RELEASE_ANGLE, EDIT_RELEASE_TIME, RUNNING_SCREEN, PAUSED_SCREEN, DONE
};

// EEPROM.put() escribe solo las celdas que han cambiado, reduciendo desgaste.
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

struct PresetData {
  uint32_t magic;
  uint16_t minutes[PRESET_COUNT];
  byte check;
};

const uint32_t PRESET_EEPROM_MAGIC = 0x4B505233UL;  // "KPR3"

uint16_t durationMinutes = DEFAULT_PRESET_DURATION_MINUTES[1];
uint16_t presetDurationMinutes[PRESET_COUNT] = {12 * 60, 24 * 60, 36 * 60};
byte homeAngle = 0;
byte releaseAngle = 60;
byte releaseSeconds = 3;
RunState runState = IDLE;
Screen screen = HOME;
byte homeSelection = 2;  // Prueba, rapida, normal, larga: normal es la opcion central.
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

// SSD1306Ascii escribe directamente en la pantalla. Guardamos el ultimo
// estado dibujado para no borrar y redibujar todo en cada vuelta de loop().
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

void formatDuration(uint32_t milliseconds, char *out, byte length) {
  uint32_t seconds = milliseconds / 1000UL;
  uint16_t hours = seconds / 3600UL;
  byte minutes = (seconds % 3600UL) / 60UL;
  byte secs = seconds % 60UL;
  snprintf(out, length, "%02u:%02u:%02u", hours, minutes, secs);
}

void formatSecondDuration(uint32_t milliseconds, char *out, byte length) {
  const uint32_t seconds = (milliseconds + 999UL) / 1000UL;
  const uint16_t hours = seconds / 3600UL;
  const byte minutes = (seconds % 3600UL) / 60UL;
  const byte secs = seconds % 60UL;
  snprintf(out, length, "%02u:%02u:%02u", hours, minutes, secs);
}

void formatMinuteDuration(uint32_t milliseconds, char *out, byte length) {
  // Redondeamos hacia arriba: 23 h 59 min 59 s se sigue viendo como 24:00.
  const uint32_t totalMinutes = (milliseconds + 59999UL) / 60000UL;
  const uint16_t hours = totalMinutes / 60UL;
  const byte minutes = totalMinutes % 60UL;
  snprintf(out, length, "%02u:%02u", hours, minutes);
}

// Esta OLED tiene una franja superior amarilla de 16 px y una zona inferior
// azul. Los titulos ocupan solo la franja amarilla; las cifras viven abajo.
void drawTitle(const __FlashStringHelper *title, byte column) {
  display.clear();
  display.setFont(Adafruit5x7);
  display.set2X();
  display.setCursor(column, 0);
  display.print(title);
  display.set1X();
}

void drawArrows(bool showLeft, bool showRight) {
  display.setFont(Adafruit5x7);
  display.set2X();
  if (showLeft) { display.setCursor(8, 6); display.print(F("<")); }
  if (showRight) { display.setCursor(108, 6); display.print(F(">")); }
  display.set1X();
}

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

void drawScreen() {
  if (!displayAvailable) return;
  switch (screen) {
    case HOME:
      if (homeSelection == HOME_TEST_SELECTION) {
        drawTitle(F("PRUEBA"), 28);
        drawBigTimer(MECHANISM_TEST_FERMENTATION_MS, true);
      } else {
        const byte presetIndex = homeSelection - HOME_FIRST_PRESET_SELECTION;
        if (presetIndex == 0) drawTitle(F("RAPIDA"), 28);
        else if (presetIndex == 1) drawTitle(F("NORMAL"), 28);
        else drawTitle(F("LARGA"), 34);
        if (presetDurationMinutes[presetIndex] % 60 == 0) {
          drawBigHours(presetDurationMinutes[presetIndex] / 60);
        } else {
          drawBigTimer(static_cast<uint32_t>(presetDurationMinutes[presetIndex]) * 60000UL);
        }
      }
      drawArrows(homeSelection > HOME_TEST_SELECTION, homeSelection < HOME_OPTION_COUNT - 1);
      break;
    case EDIT_PRESET_DURATION:
      if (homeSelection == 1) drawTitle(F("RAPIDA"), 28);
      else if (homeSelection == 2) drawTitle(F("NORMAL"), 28);
      else drawTitle(F("LARGA"), 34);
      drawBigTimer(static_cast<uint32_t>(presetDurationMinutes[homeSelection - HOME_FIRST_PRESET_SELECTION]) * 60000UL);
      drawArrows(true, true);
      break;
    case ADVANCED_MENU:
      drawTitle(F("OPCIONES"), 16);
      display.set2X();
      if (advancedSelection == 0) { display.setCursor(28, 3); display.print(F("TIEMPO")); }
      else if (advancedSelection == 1) { display.setCursor(28, 3); display.print(F("PRUEBA")); }
      else if (advancedSelection == 2) { display.setCursor(34, 3); display.print(F("SERVO")); }
      else { display.setCursor(34, 3); display.print(F("SALIR")); }
      display.set1X();
      drawArrows(true, true);
      break;
    case DURATION:
      drawTitle(F("TIEMPO"), 28);
      drawBigTimer(static_cast<uint32_t>(durationMinutes) * 60000UL);
      drawArrows(true, true);
      break;
    case SETTINGS:
      drawTitle(F("SERVO"), 34);
      display.set2X();
      if (settingsSelection == 0) { display.setCursor(28, 3); display.print(F("CERRAR")); }
      else if (settingsSelection == 1) { display.setCursor(34, 3); display.print(F("ABRIR")); }
      else if (settingsSelection == 2) { display.setCursor(28, 3); display.print(F("TIEMPO")); }
      else { display.setCursor(34, 3); display.print(F("SALIR")); }
      display.set1X();
      drawArrows(true, true);
      break;
    case EDIT_HOME_ANGLE:
    case EDIT_RELEASE_ANGLE:
    case EDIT_RELEASE_TIME:
      if (screen == EDIT_HOME_ANGLE) { drawTitle(F("CERRADO"), 22); drawBigValue(homeAngle); }
      else if (screen == EDIT_RELEASE_ANGLE) { drawTitle(F("ABIERTO"), 22); drawBigValue(releaseAngle); }
      else { drawTitle(F("RETENER"), 22); drawBigValue(releaseSeconds); }
      drawArrows(true, true);
      break;
    case RUNNING_SCREEN:
      if (shortTestInProgress) drawTitle(F("PRUEBA"), 28);
      else drawTitle(F("ACTIVO"), 28);
      drawBigTimer(remainingMs, shortTestInProgress);
      break;
    case PAUSED_SCREEN:
      drawTitle(F("PAUSA"), 34);
      drawBigTimer(remainingMs, shortTestInProgress);
      break;
    case DONE:
      if (releaseInProgress) drawTitle(F("LIBERANDO"), 10);
      else drawTitle(F("LISTO"), 34);
      break;
  }
}

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

void updateTimerText() {
  if (!displayAvailable || (screen != RUNNING_SCREEN && screen != PAUSED_SCREEN)) return;
  const uint32_t displayTick = shortTestInProgress
      ? (remainingMs + 999UL) / 1000UL
      : (remainingMs + 59999UL) / 60000UL;
  if (displayTick == lastRenderedTimerSecond) return;

  // Solo se borra el campo numérico azul; la cabecera amarilla no parpadea.
  display.clear(0, SCREEN_WIDTH - 1, 2, 5);
  drawBigTimer(remainingMs, shortTestInProgress);
  lastRenderedTimerSecond = displayTick;
}

void moveServoHome() { releaseServo.write(homeAngle); }
void moveServoRelease() { releaseServo.write(releaseAngle); }

void beginRelease(bool isTest) {
  releaseIsTest = isTest;
  releaseInProgress = true;
  releaseStartedMs = millis();
  moveServoRelease();
  screen = DONE;
  Serial.println(isTest ? F("Prueba de liberacion iniciada.") : F("Tiempo terminado: liberando granulos."));
}

void finishRelease() {
  moveServoHome();
  releaseInProgress = false;
  if (releaseIsTest) {
    screen = HOME;
    Serial.println(F("Prueba terminada: servo en posicion cerrada."));
  } else {
    runState = IDLE;
    remainingMs = 0;
    savePersistentData();
    screen = DONE;
    Serial.println(F("Fermentacion terminada: servo en posicion cerrada."));
  }
}

void startFermentation() {
  remainingMs = static_cast<uint32_t>(durationMinutes) * 60000UL;
  shortTestInProgress = false;
  runState = RUNNING;
  lastTickMs = lastSaveMs = millis();
  savePersistentData();
  screen = RUNNING_SCREEN;
  Serial.println(F("Fermentacion iniciada."));
}

void startMechanismTest() {
  remainingMs = MECHANISM_TEST_FERMENTATION_MS;
  shortTestInProgress = true;
  runState = RUNNING;
  lastTickMs = lastSaveMs = millis();
  savePersistentData();
  screen = RUNNING_SCREEN;
  Serial.println(F("Prueba de 5 segundos iniciada."));
}

void pauseFermentation() {
  runState = PAUSED;
  savePersistentData();
  screen = PAUSED_SCREEN;
  Serial.println(F("Temporizador pausado."));
}

void resumeFermentation() {
  runState = RUNNING;
  lastTickMs = lastSaveMs = millis();
  savePersistentData();
  screen = RUNNING_SCREEN;
  Serial.println(F("Temporizador reanudado."));
}

void cancelFermentation() {
  runState = IDLE;
  remainingMs = 0;
  shortTestInProgress = false;
  savePersistentData();
  moveServoHome();
  screen = HOME;
  Serial.println(F("Fermentacion cancelada. Servo en posicion cerrada."));
}

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
  Serial.println(F("=== KEFIR NANO: COMANDOS ==="));
  Serial.println(F("AYUDA                    Muestra esta lista"));
  Serial.println(F("ESTADO                   Muestra configuracion y temporizador"));
  Serial.println(F("DURACION <h> [min]       Guarda una duracion (0 min - 72 h)"));
  Serial.println(F("INICIAR [h] [min]        Inicia; con valores cambia la duracion"));
  Serial.println(F("PAUSA | CONTINUAR | CANCELAR"));
  Serial.println(F("PRUEBA                   Abre y cierra el servo una vez"));
  Serial.println(F("CERRAR                   Lleva el servo a la posicion cerrada"));
  Serial.println(F("CERRADO <0-180>          Ajusta el angulo cerrado"));
  Serial.println(F("LIBERACION <0-180>       Ajusta el angulo de liberacion"));
  Serial.println(F("RETENCION <1-10>         Ajusta los segundos de liberacion"));
  Serial.println(F("Ejemplos: DURACION 14 | INICIAR | INICIAR 12 30"));
  Serial.println(F("Escribe sin acentos y pulsa Enviar."));
}

void printStatus() {
  char timeText[16];
  formatDuration(remainingMs, timeText, sizeof(timeText));
  Serial.println(F("--- ESTADO ---"));
  Serial.print(F("Duracion configurada: ")); Serial.print(durationMinutes / 60);
  Serial.print(F(" h ")); Serial.print(durationMinutes % 60); Serial.println(F(" min"));
  Serial.print(F("Servo cerrado: ")); Serial.print(homeAngle);
  Serial.print(F(" | liberar: ")); Serial.print(releaseAngle);
  Serial.print(F(" | retencion: ")); Serial.print(releaseSeconds); Serial.println(F(" s"));
  if (releaseInProgress) Serial.println(F("Estado: LIBERANDO GRANULOS"));
  else if (runState == RUNNING) { Serial.print(F("Estado: FERMENTANDO | resta: ")); Serial.println(timeText); }
  else if (runState == PAUSED) { Serial.print(F("Estado: PAUSADO | resta: ")); Serial.println(timeText); }
  else Serial.println(F("Estado: EN ESPERA"));
}

bool setDurationFromArguments(char *hoursText, char *minutesText) {
  long hours = 0, minutes = 0;
  if (!parseNumber(hoursText, 0, 72, hours) ||
      (minutesText != NULL && !parseNumber(minutesText, 0, 59, minutes))) {
    Serial.println(F("Uso: DURACION <horas 0-72> [minutos 0-59]"));
    return false;
  }
  long total = hours * 60L + minutes;
  if (total < MIN_DURATION_MINUTES || total > MAX_DURATION_MINUTES) {
    Serial.println(F("La duracion debe estar entre 0 minutos y 72 horas."));
    return false;
  }
  durationMinutes = total;
  savePersistentData();
  Serial.print(F("Duracion guardada: ")); Serial.print(hours); Serial.print(F(" h "));
  Serial.print(minutes); Serial.println(F(" min."));
  return true;
}

void handleSerialCommand(char *line) {
  while (isspace(*line)) ++line;
  if (*line == '\0') return;
  for (char *p = line; *p; ++p) *p = toupper(*p);
  char *command = strtok(line, " \t");
  char *arg1 = strtok(NULL, " \t");
  char *arg2 = strtok(NULL, " \t");

  if (!strcmp(command, "AYUDA") || !strcmp(command, "HELP")) { printHelp(); return; }
  if (!strcmp(command, "ESTADO")) { printStatus(); return; }
  if (releaseInProgress) { Serial.println(F("El servo esta liberando; espera a que termine.")); return; }
  if (!strcmp(command, "DURACION")) {
    if (runState == IDLE) setDurationFromArguments(arg1, arg2);
    else Serial.println(F("No se puede cambiar la duracion durante una fermentacion."));
    return;
  }
  if (!strcmp(command, "INICIAR")) {
    if (runState != IDLE) Serial.println(F("Ya hay una fermentacion en curso o pausada."));
    else if (arg1 == NULL || setDurationFromArguments(arg1, arg2)) startFermentation();
    return;
  }
  if (!strcmp(command, "PAUSA")) {
    if (runState == RUNNING) pauseFermentation(); else Serial.println(F("No hay una fermentacion activa que pausar."));
    return;
  }
  if (!strcmp(command, "CONTINUAR")) {
    if (runState == PAUSED && remainingMs > 0) resumeFermentation(); else Serial.println(F("No hay una fermentacion pausada que continuar."));
    return;
  }
  if (!strcmp(command, "CANCELAR")) {
    if (runState != IDLE) cancelFermentation(); else Serial.println(F("No hay una fermentacion que cancelar."));
    return;
  }
  if (!strcmp(command, "PRUEBA")) {
    if (runState == IDLE) beginRelease(true); else Serial.println(F("Cancela o termina la fermentacion antes de probar el servo."));
    return;
  }
  if (!strcmp(command, "CERRAR")) { moveServoHome(); Serial.println(F("Servo en posicion cerrada.")); return; }

  long value = 0;
  if (!strcmp(command, "CERRADO")) {
    if (parseNumber(arg1, 0, 180, value)) {
      homeAngle = value; savePersistentData(); moveServoHome();
      Serial.println(F("Angulo cerrado guardado."));
    } else Serial.println(F("Uso: CERRADO <angulo 0-180>"));
    return;
  }
  if (!strcmp(command, "LIBERACION")) {
    if (parseNumber(arg1, 0, 180, value)) {
      releaseAngle = value; savePersistentData(); Serial.println(F("Angulo de liberacion guardado."));
    } else Serial.println(F("Uso: LIBERACION <angulo 0-180>"));
    return;
  }
  if (!strcmp(command, "RETENCION")) {
    if (parseNumber(arg1, 1, 10, value)) {
      releaseSeconds = value; savePersistentData(); Serial.println(F("Retencion guardada."));
    } else Serial.println(F("Uso: RETENCION <segundos 1-10>"));
    return;
  }
  Serial.println(F("Comando no reconocido. Escribe AYUDA."));
}

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
      Serial.println(F("Comando demasiado largo. Escribe AYUDA."));
    }
  }
}

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
    // En inicio, un toque inicia al soltar; mantener SELECT abre la edicion
    // del preajuste mostrado sin iniciar antes el temporizador.
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
  Wire.begin();                 // Nano: SDA = A4, SCL = A5 (I2C fijo).
  Wire.setClock(100000L);       // I2C estandar, igual que en la ESP32.
  byte oledAddress = findOledAddress();
  if (oledAddress != 0) {
    display.begin(&Adafruit128x64, oledAddress);
    display.setFont(Adafruit5x7);
    displayAvailable = true;
    Serial.print(F("OLED encontrada en I2C 0x"));
    Serial.println(oledAddress, HEX);
  } else {
    Serial.println(F("No se encontro OLED I2C en 0x3C ni 0x3D."));
    Serial.println(F("Nano clasico: SDA=A4, SCL=A5, VCC=3V3/5V y GND=GND."));
    Serial.println(F("El control por puerto serie sigue disponible."));
  }
  releaseServo.attach(PIN_SERVO);
  moveServoHome();
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
  Serial.println(F("Kefir Nano listo. Puerto serie: 115200 baudios."));
  printHelp();
}

void loop() {
  uint32_t now = millis();
  buttonUp.update(now); buttonDown.update(now); buttonSelect.update(now);
  if (buttonUp.pressed()) Serial.println(F("Boton detectado: ARRIBA (D2)"));
  if (buttonDown.pressed()) Serial.println(F("Boton detectado: ABAJO (D3)"));
  if (buttonSelect.pressed()) Serial.println(F("Boton detectado: SELECCIONAR (D4)"));
  readSerialCommands();
  updateTimer(now);
  // updateTimer() puede iniciar la liberacion en esta misma vuelta. No uses
  // `now`, que se tomo antes de esa llamada: si releaseStartedMs es unos ms
  // posterior, la resta sin signo se desborda y cerraria el servo de inmediato.
  const uint32_t releaseNow = millis();
  if (releaseInProgress && releaseNow - releaseStartedMs >= static_cast<uint32_t>(releaseSeconds) * 1000UL) {
    finishRelease();
  }
  handleButtons();
  if (needsFullRedraw()) {
    drawScreen();
  } else updateTimerText();
}
