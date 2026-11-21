#include <Arduino.h>
#include "config.h"
#include "face.h"
#include "sounds.h"

enum Mode : uint8_t { MODE_FACES, MODE_CRY, MODE_HAPPY };
Mode mode = MODE_FACES;
uint32_t modeStart = 0;
uint8_t faceIndex = F_NORMAL;

bool touchRaw = false, touchStable = false;
uint32_t touchChanged = 0, pressStart = 0, lastTapAt = 0, lastTouchAt = 0;
bool longFired = false;
uint8_t tapCount = 0;

float level = 0;
const uint16_t FRAME_MS = 16;
uint32_t lastFrame = 0;

void showFace(uint8_t id) {
  mode = MODE_FACES;
  faceIndex = id;
  faceLockGaze(false);
  setFace(id);
  soundPlay(SND_BLIP, FACES[id].blip);
  Serial.printf("face: %s\n", FACES[id].name);
}

void startCry() {
  mode = MODE_CRY; modeStart = millis();
  setFace(F_CRY, 380);
  faceLockGaze(true, 0, 3);
  soundPlay(SND_CRY);
  Serial.println("crying");
}

void startHappy() {
  mode = MODE_HAPPY; modeStart = millis();
  setFace(F_CHIRP, 380);
  faceLockGaze(true, 0, -3);
  soundPlay(SND_BIRDS);
  Serial.println("happy birds");
}

void backToFaces() {
  soundStop();
  mode = MODE_FACES;
  faceLockGaze(false);
  setFace(faceIndex, 450);
  triggerBlink();
}

void onSingleTap() {
  if (mode != MODE_FACES) backToFaces();
  else showFace((faceIndex + 1) % TAP_FACES);
}

void onDoubleTap() { startCry(); }
void onLongPress() { startHappy(); }

void handleTouch(uint32_t now) {
  bool pin = digitalRead(PIN_TOUCH);
  bool pressed = TOUCH_ACTIVE_HIGH ? pin : !pin;
  if (pressed != touchRaw) { touchRaw = pressed; touchChanged = now; }

  if (now - touchChanged > 25 && touchStable != touchRaw) {
    touchStable = touchRaw;
    lastTouchAt = now;
    if (touchStable) {
      pressStart = now;
      longFired = false;
    } else if (!longFired) {
      if (tapCount == 1 && now - lastTapAt <= DOUBLE_TAP_MS) {
        tapCount = 0;
        onDoubleTap();
      } else {
        tapCount = 1;
        lastTapAt = now;
      }
    }
  }

  if (touchStable && !longFired && now - pressStart >= LONG_PRESS_MS) {
    longFired = true;
    tapCount = 0;
    onLongPress();
  }

  if (tapCount == 1 && !touchStable && now - lastTapAt > DOUBLE_TAP_MS) {
    tapCount = 0;
    onSingleTap();
  }
}

void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c >= '0' && c < '0' + TAP_FACES) showFace(c - '0');
  else if (c == 'c') startCry();
  else if (c == 'h') startHappy();
  else if (c == 's') backToFaces();
  else if (c == 'b') triggerBlink();
}

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  pinMode(PIN_TOUCH, TOUCH_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);

  faceBegin(F_SLEEPY);
  soundBegin();

  setFace(F_NORMAL, 900);
  soundPlay(SND_HELLO);
  lastTouchAt = millis();
  Serial.println("ChotuBot ready");
}

void loop() {
  uint32_t now = millis();
  handleSerial();
  handleTouch(now);

  if (mode != MODE_FACES && now - modeStart > 600 && !soundBusy()) backToFaces();

#if ATTENTION_CRY_S > 0
  if (mode == MODE_FACES && now - lastTouchAt > ATTENTION_CRY_S * 1000UL) {
    lastTouchAt = now;
    startCry();
  }
#endif

  if (now - lastFrame < FRAME_MS) { delay(1); return; }
  lastFrame = now;

  float target = soundLevel();
  level += (target - level) * (target > level ? 0.6f : 0.15f);

  FaceFx fx;
  fx.level = level;
  fx.sob = (mode == MODE_CRY);
  fx.sway = (mode == MODE_HAPPY);

  faceUpdate(now);
  faceDraw(now, fx);
}
