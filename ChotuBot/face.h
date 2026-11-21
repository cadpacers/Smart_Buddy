#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "config.h"

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);

#define SCREEN_W 128
#define SCREEN_H 64

struct Face {
  const char *name;
  float eyeW, eyeH, eyeR, gap, baseY;
  float lidTop;      
  float happy;       
  float brow;        
  float heart;       
  float squint;      
  float mouthW, mouthCurve, mouthOpen, mouthY;
  float tears, notes, zzz, blush;
  uint16_t blip;     
};

enum FaceId : uint8_t {
  F_NORMAL, F_HAPPY, F_LOVE, F_SURPRISED, F_ANGRY, F_SAD, F_SLEEPY,
  F_CRY, F_CHIRP, F_COUNT
};
const uint8_t TAP_FACES = 7;   

const Face FACES[F_COUNT] = {
  {"normal",     30,  40,  10,  22, 27,  0,    0,    0,    0,    0,   24,  4,    0,    0,  0,    0,    0,  0,    660},
  {"happy",      32,  34,  12,  20, 26,  0,    1,    0,    0,    0,   30,  5,    6,    0,  0,    0,    0,  1,    880},
  {"love",       34,  34,  0,   16, 26,  0,    0,    0,    1,    0,   26,  5,    3,    0,  0,    0,    0,  1,    990},
  {"surprised",  30,  40,  14,  22, 25,  0,    0,    0,    0,    0,   12,  0,    8,    0,  0,    0,    0,  0,    1180},
  {"angry",      30,  34,  8,   18, 28,  0,    0,   -0.85, 0,    0,   22, -4,    0,    0,  0,    0,    0,  0,    330},
  {"sad",        30,  32,  10,  22, 29,  0,    0,    0.8,  0,    0,   24, -4,    0,    0,  0,    0,    0,  0,    440},
  {"sleepy",     30,  40,  10,  22, 29,  0.62, 0,    0.15, 0,    0,   14, -1,    0,    0,  0,    0,    1,  0,    392},
  {"cry",        26,  24,  0,   28, 20,  0,    0,    0,    0,    1,   24, -3,    12,   12, 1,    0,    0,  0,    0},
  {"chirp",      32,  34,  12,  20, 25,  0,    1,    0,    0,    0,   12,  2,    6,    0,  0,    1,    0,  1,    0},
};

struct FaceFx {
  float level = 0;     
  bool  sob = false;   
  bool  sway = false;  
};

static Face faceNow, faceFrom, faceTo;
static uint32_t tweenStart = 0;
static uint16_t tweenDur = 1;
static bool tweening = false;

static float gazeX = 0, gazeY = 0, gazeFromX = 0, gazeFromY = 0, gazeToX = 0, gazeToY = 0;
static uint32_t gazeStart = 0, nextSaccade = 0;
static uint16_t gazeDur = 1;
static bool gazeLocked = false;

static float blinkAmt = 0;
static uint32_t blinkStart = 0, nextBlink = 0;
static uint16_t blinkDur = 0;
static bool blinking = false;

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float easeInOut(float t) {
  t = clampf(t, 0, 1);
  return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
}
static inline float easeOut(float t) { t = clampf(t, 0, 1); return 1 - (1 - t) * (1 - t); }

static void box(int x, int y, int w, int h) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > SCREEN_W) w = SCREEN_W - x;
  if (y + h > SCREEN_H) h = SCREEN_H - y;
  if (w > 0 && h > 0) oled.drawBox(x, y, w, h);
}

static void rbox(int x, int y, int w, int h, int r) {
  if (x < 0 || y < 0 || x + w > SCREEN_W || y + h > SCREEN_H || r < 1 || h < 6) { box(x, y, w, h); return; }
  oled.drawRBox(x, y, w, h, r);
}

static void ellipse(int cx, int cy, int rx, int ry) {
  if (rx < 1 || ry < 1) return;
  for (int dy = -ry; dy <= ry; dy++) {
    float k = 1.0f - (float)(dy * dy) / (float)(ry * ry);
    int hw = lroundf(rx * sqrtf(k > 0 ? k : 0));
    int y = cy + dy;
    if (y < 0 || y >= SCREEN_H) continue;
    int x0 = cx - hw, x1 = cx + hw;
    if (x0 < 0) x0 = 0;
    if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
    if (x1 >= x0) oled.drawHLine(x0, y, x1 - x0 + 1);
  }
}

static void pixel(int x, int y) {
  if (x >= 0 && y >= 0 && x < SCREEN_W && y < SCREEN_H) oled.drawPixel(x, y);
}

static void line(int x0, int y0, int x1, int y1) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    pixel(x0, y0);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void triggerBlink(uint16_t ms = 150) {
  if (blinking) return;
  blinking = true; blinkStart = millis(); blinkDur = ms;
}

void lookAt(float x, float y, uint16_t ms = 180) {
  gazeFromX = gazeX; gazeFromY = gazeY;
  gazeToX = clampf(x, -8, 8); gazeToY = clampf(y, -4, 4);
  gazeStart = millis(); gazeDur = ms ? ms : 1;
}

Face blendFace(const Face &a, const Face &b, float t) {
  Face r = (t < 0.5f) ? a : b;
  r.eyeW = lerpf(a.eyeW, b.eyeW, t);   r.eyeH = lerpf(a.eyeH, b.eyeH, t);
  r.eyeR = lerpf(a.eyeR, b.eyeR, t);   r.gap = lerpf(a.gap, b.gap, t);
  r.baseY = lerpf(a.baseY, b.baseY, t);
  r.lidTop = lerpf(a.lidTop, b.lidTop, t);
  r.happy = lerpf(a.happy, b.happy, t); r.brow = lerpf(a.brow, b.brow, t);
  r.heart = lerpf(a.heart, b.heart, t);
  r.squint = lerpf(a.squint, b.squint, t);
  r.mouthY = lerpf(a.mouthY, b.mouthY, t);
  r.mouthW = lerpf(a.mouthW, b.mouthW, t);
  r.mouthCurve = lerpf(a.mouthCurve, b.mouthCurve, t);
  r.mouthOpen = lerpf(a.mouthOpen, b.mouthOpen, t);
  r.tears = lerpf(a.tears, b.tears, t); r.notes = lerpf(a.notes, b.notes, t);
  r.zzz = lerpf(a.zzz, b.zzz, t);       r.blush = lerpf(a.blush, b.blush, t);
  return r;
}

void setFace(uint8_t id, uint16_t ms = 320) {
  if (id >= F_COUNT) return;
  faceFrom = faceNow;
  faceTo = FACES[id];
  tweenStart = millis(); tweenDur = ms ? ms : 1; tweening = true;
}

void faceBegin(uint8_t startId) {
  oled.begin();
  oled.setBusClock(I2C_HZ);
  oled.setContrast(255);
  faceNow = faceTo = FACES[startId];
  uint32_t t = millis();
  nextBlink = t + 1500; nextSaccade = t + 2000;
}
void faceLockGaze(bool lock, float x = 0, float y = 0) {
  gazeLocked = lock;
  if (lock) lookAt(x, y, 300);
}

void faceUpdate(uint32_t now) {
  if (tweening) {
    float t = (float)(now - tweenStart) / tweenDur;
    if (t >= 1) { t = 1; tweening = false; }
    faceNow = blendFace(faceFrom, faceTo, easeInOut(t));
  }

  bool canBlink = faceNow.tears < 0.5f && faceNow.happy < 0.5f;
  if (blinking) {
    float t = (float)(now - blinkStart) / blinkDur;
    if (t >= 1) { blinking = false; blinkAmt = 0; }
    else blinkAmt = (t < 0.4f) ? easeOut(t / 0.4f) : 1 - easeInOut((t - 0.4f) / 0.6f);
  }
  if (!blinking && now > nextBlink) {
    if (canBlink) triggerBlink();
    nextBlink = now + random(2200, 5200);
    if (random(100) < 22) nextBlink = now + random(260, 420);
  }

  float gt = easeOut((float)(now - gazeStart) / gazeDur);
  gazeX = lerpf(gazeFromX, gazeToX, gt);
  gazeY = lerpf(gazeFromY, gazeToY, gt);
  if (!gazeLocked && now > nextSaccade) {
    lookAt(random(-100, 101) / 100.0f * 7.0f, random(-100, 101) / 100.0f * 3.0f, random(140, 260));
    nextSaccade = now + random(1400, 4200);
  }
}

static void drawHeart(int cx, int cy, float s, float sy) {
  int r = lroundf(s * 0.27f);
  int ry = max(1, (int)lroundf(r * sy));
  int dy = lroundf(s * 0.12f * sy);
  ellipse(cx - lroundf(s * 0.23f), cy - dy, r, ry);
  ellipse(cx + lroundf(s * 0.23f), cy - dy, r, ry);
  int x0 = cx - lroundf(s * 0.49f), x1 = cx + lroundf(s * 0.49f);
  int yt = cy - lroundf(s * 0.04f * sy), yb = cy + lroundf(s * 0.42f * sy);
  for (int y = yt; y <= yb; y++) {
    float k = (float)(y - yt) / max(1, yb - yt);
    int a = lroundf(lerpf(x0, cx, k)), b = lroundf(lerpf(x1, cx, k));
    if (y >= 0 && y < SCREEN_H) box(a, y, b - a + 1, 1);
  }
}

static int drawEye(const Face &f, int8_t side, float ox, float oy, uint32_t now) {
  float w = f.eyeW;
  float h = f.eyeH * (1.0f - blinkAmt * 0.94f);
  if (h < 3) h = 3;
  float cx = SCREEN_W * 0.5f + side * (f.gap * 0.5f + w * 0.5f) + ox;
  float cy = f.baseY + oy;
  int wi = lroundf(w), hi = lroundf(h);
  int x = lroundf(cx - w * 0.5f), y = lroundf(cy - h * 0.5f);

  oled.setDrawColor(1);
  if (f.heart > 0.5f) {
    float pulse = 1.0f + 0.07f * sinf(now * 0.012f);
    drawHeart(lroundf(cx), lroundf(cy), w * pulse, h / f.eyeH);
    return y + hi;
  }

  if (f.squint > 0.5f) {
    int sh = lroundf(f.eyeH * 0.5f);
    int tipX = side < 0 ? x + wi : x;
    int backX = side < 0 ? x : x + wi;
    int midY = lroundf(cy);
    for (int k = -2; k <= 2; k++) {
      line(backX, midY - sh + k, tipX, midY + k);
      line(tipX, midY + k, backX, midY + sh + k);
    }
    return midY + sh + 2;
  }

  int maxR = min(wi, hi) / 2 - 1;
  rbox(x, y, wi, hi, lroundf(clampf(f.eyeR, 0, (float)maxR)));

  oled.setDrawColor(0);
  if (f.lidTop > 0.02f) box(x - 1, y - 1, wi + 2, lroundf(f.lidTop * hi) + 1);

  if (f.happy > 0.02f) {
    float ry = h * 0.8f;
    float ecy = y + lerpf(h + ry + 2, h * 0.35f + ry, f.happy);
    ellipse(lroundf(cx), lroundf(ecy), lroundf(w * 0.62f), lroundf(ry));
  }

  if (fabsf(f.brow) > 0.02f && hi >= 6) {
    int amt = lroundf(fabsf(f.brow) * hi * 0.6f);
    int top = y + lroundf(f.lidTop * hi) - 1;
    bool outer = f.brow > 0;
    int xc = ((side < 0) == outer) ? x - 1 : x + wi;
    int xf = ((side < 0) == outer) ? x + wi : x - 1;
    for (int k = 0; k <= amt; k++) {
      int len = lroundf((float)(amt - k) / amt * (wi + 1));
      int y2 = top + k;
      if (xc < xf) box(xc, y2, len, 1); else box(xc - len + 1, y2, len, 1);
    }
  }
  oled.setDrawColor(1);
  return y + hi;
}

static void drawMouth(const Face &f, float ox, float oy, float open) {
  int half = lroundf(f.mouthW * 0.5f);
  if (half < 2) return;
  float base = f.baseY + oy + f.eyeH * 0.5f + 7.0f + f.mouthY;
  int cx = lroundf(SCREEN_W * 0.5f + ox);
  for (int i = -half; i <= half; i++) {
    float t = (float)i / half;
    float k = 1.0f - t * t;
    float o = open * sqrtf(k);
    float u = base + f.mouthCurve * k - o * 0.5f;
    int y0 = lroundf(u), len = lroundf(2 + o);
    box(cx + i, y0, 1, len);
  }
}

static void drawTears(int ex, int eyBottom, uint32_t now, uint32_t phase) {
  for (int d = 0; d < 2; d++) {
    float p = (float)((now + phase + d * 350) % 700) / 700.0f;
    int y = eyBottom + 3 + lroundf((SCREEN_H + 6 - eyBottom) * p * p);
    ellipse(ex, y, 2, 2);
    box(ex - 1, y - 2, 3, 1);
    box(ex, y - 4, 1, 2);
  }
}

static void drawNote(int x, int y) {
  ellipse(x, y, 2, 2);
  box(x + 2, y - 8, 1, 8);
  line(x + 2, y - 8, x + 5, y - 5);
}

static void drawOverlays(const Face &f, int lx, int rx, int ly, int ry, uint32_t now) {
  oled.setDrawColor(1);
  if (f.tears > 0.5f) {
    drawTears(lx, ly, now, 0);
    drawTears(rx, ry, now, 200);
  }
  if (f.notes > 0.5f) {
    for (int n = 0; n < 2; n++) {
      float p = (float)((now + n * 700) % 1400) / 1400.0f;
      int y = 44 - lroundf(p * 40);
      int sx = lroundf(sinf(p * 6.2832f) * 2);
      drawNote((n == 0 ? 6 : 116) + sx, y);
    }
  }
  if (f.zzz > 0.5f) {
    oled.setFont(u8g2_font_6x10_tr);
    float p = (float)(now % 2400) / 2400.0f;
    int y = 22 - lroundf(p * 12);
    if (y > 9) oled.drawStr(110, y, "z");
    if (p > 0.3f) oled.drawStr(116, y - 6 > 9 ? y - 6 : 9, "Z");
  }
}

static void drawBlush(const Face &f, int ex, int eyBottom, int8_t side) {
  if (f.blush < 0.5f) return;
  int bx = ex + side * lroundf(f.eyeW * 0.15f);
  int by = eyBottom + 3;
  for (int k = 0; k < 3; k++) line(bx - 5 + k * 3, by + 2, bx - 3 + k * 3, by);
}

void faceDraw(uint32_t now, const FaceFx &fx) {
  const Face &f = faceNow;
  float breathe = sinf(now * 0.0016f) * 1.2f;
  float ox = gazeX, oy = gazeY + breathe;

  if (fx.sob) {
    if (fx.level > 0.25f) ox += ((now / 45) & 1) ? 1 : -1;
    oy += fx.level * 2.0f;
  }
  if (fx.sway) ox += sinf(now * 0.006f) * 3.0f;

  float open = f.mouthOpen;
  if (fx.sob)  open *= 0.35f + 0.9f * fx.level;
  if (fx.sway) open *= 0.2f + 1.0f * fx.level;

  oled.clearBuffer();
  int lb = drawEye(f, -1, ox, oy, now);
  int rb = drawEye(f, +1, ox, oy, now);
  int lx = lroundf(SCREEN_W * 0.5f - (f.gap * 0.5f + f.eyeW * 0.5f) + ox);
  int rx = lroundf(SCREEN_W * 0.5f + (f.gap * 0.5f + f.eyeW * 0.5f) + ox);
  drawBlush(f, lx, lb, -1);
  drawBlush(f, rx, rb, +1);
  drawMouth(f, ox * 0.6f, oy, open);
  drawOverlays(f, lx - lroundf(f.eyeW * 0.3f), rx + lroundf(f.eyeW * 0.3f), lb, rb, now);
  oled.sendBuffer();
}
