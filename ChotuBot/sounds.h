#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>
#include "config.h"

enum Sound : uint8_t { SND_NONE = 0, SND_HELLO, SND_BLIP, SND_CRY, SND_BIRDS };

#define AUDIO_SR    22050      
#define AUDIO_BLK   32         
#define AUDIO_CHUNK 256        

static I2SClass       i2s;
static QueueHandle_t  g_soundQ = nullptr;
static volatile Sound g_playing = SND_NONE;
static volatile bool  g_stopSound = false;
static volatile float g_soundLevel = 0;     
static volatile uint16_t g_blipPitch = 660;

static int16_t  SINE[256];
static int16_t  g_buf[AUDIO_CHUNK * 2];
static int      g_bufN = 0;
static int32_t  g_peak = 0;
static uint32_t g_rng = 0x2545F491;
static const int32_t VOL_Q8 = VOLUME_PCT * 256 / 100;
static inline int16_t noise16() {
  g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
  return (int16_t)g_rng;
}

static inline int32_t sine16(uint32_t ph) {
  uint32_t i = ph >> 24, f = (ph >> 16) & 0xFF;
  int32_t a = SINE[i], b = SINE[(i + 1) & 255];
  return a + (((b - a) * (int32_t)f) >> 8);
}

static inline uint32_t freqInc(float hz) { return (uint32_t)(hz * (4294967296.0f / AUDIO_SR)); }

static void flushAudio() {
  if (g_bufN == 0) return;
  i2s.write((uint8_t *)g_buf, g_bufN * 4);
  g_soundLevel = g_peak / 32768.0f;
  g_bufN = 0; g_peak = 0;
}

static inline void out(int32_t s) {
  s = (s * VOL_Q8) >> 8;
  if (s > 24000) s = 24000 + ((s - 24000) >> 2);
  else if (s < -24000) s = -24000 + ((s + 24000) >> 2);
  if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
  g_buf[g_bufN * 2] = (int16_t)s;
  g_buf[g_bufN * 2 + 1] = (int16_t)s;
  int32_t a = s < 0 ? -s : s;
  if (a > g_peak) g_peak = a;
  if (++g_bufN == AUDIO_CHUNK) flushAudio();
}

static bool silence(int ms) {
  int n = AUDIO_SR * ms / 1000;
  for (int i = 0; i < n; i++) {
    out(0);
    if ((i & 255) == 0 && g_stopSound) return false;
  }
  return true;
}
static bool chirp(float f0, float f1, int ms, float amp, float trillHz = 0, float trillDepth = 0) {
  int n = AUDIO_SR * ms / 1000;
  uint32_t ph = 0;
  for (int i = 0; i < n; i += AUDIO_BLK) {
    float t0 = (float)i / n, t1 = (float)(i + AUDIO_BLK) / n;
    if (t1 > 1) t1 = 1;
    float f = f0 * powf(f1 / f0, t0);
    if (trillHz > 0) f *= 1.0f + trillDepth * sinf(6.2832f * trillHz * i / AUDIO_SR);
    uint32_t inc = freqInc(f);
    int32_t a0 = (int32_t)(sinf(3.1416f * t0) * amp * 32767);
    int32_t a1 = (int32_t)(sinf(3.1416f * t1) * amp * 32767);
    for (int j = 0; j < AUDIO_BLK && i + j < n; j++) {
      int32_t a = a0 + (a1 - a0) * j / AUDIO_BLK;
      ph += inc;
      out((sine16(ph) * a) >> 15);
    }
    if (g_stopSound) return false;
  }
  return true;
}
struct Reson {
  int32_t c1 = 0, c2 = 0, g = 0, y1 = 0, y2 = 0;
  void set(float hz, float bw) {
    float r = expf(-3.1416f * bw / AUDIO_SR);
    c1 = (int32_t)(2.0f * r * cosf(6.2832f * hz / AUDIO_SR) * 16384);
    c2 = (int32_t)(-r * r * 16384);
    g  = (int32_t)((1.0f - r) * 16384);
  }
  inline int32_t step(int32_t x) {
    int64_t acc = (int64_t)g * x + (int64_t)c1 * y1 + (int64_t)c2 * y2;
    int32_t y = (int32_t)(acc >> 14);
    y2 = y1; y1 = y;
    return y;
  }
};
static bool wah(int ms, float fA, float fB, float fC, float amp) {
  int n = AUDIO_SR * ms / 1000;
  uint32_t ph = 0;
  Reson r1, r2, r3;
  int32_t prevA = 0;
  for (int i = 0; i < n; i += AUDIO_BLK) {
    float t = (float)i / n;
    float f = (t < 0.25f) ? fA + (fB - fA) * (t / 0.25f)
                          : fB + (fC - fB) * ((t - 0.25f) / 0.75f);
    f *= 1.0f + 0.035f * sinf(6.2832f * 6.5f * i / AUDIO_SR);
    uint32_t inc = freqInc(f);

    float g = (float)i / (AUDIO_SR * 0.12f);
    if (g > 1) g = 1;
    r1.set(500 + 600 * g, 180);
    r2.set(900 + 900 * g, 220);
    r3.set(3000, 400);

    float ms_in = (float)i * 1000 / AUDIO_SR, ms_left = (float)(n - i) * 1000 / AUDIO_SR;
    float env = 1.0f;
    if (ms_in < 70) env = ms_in / 70;
    if (ms_left < 180) env *= ms_left / 180;
    env *= 1.0f - 0.2f * t;
    int32_t A = (int32_t)(env * amp * 32767);

    for (int j = 0; j < AUDIO_BLK && i + j < n; j++) {
      ph += inc;
      int32_t src = ((int32_t)(ph >> 16) - 32768) + (noise16() >> 3);
      int32_t y = r1.step(src) * 3 + r2.step(src) * 2 + r3.step(src);
      int32_t a = prevA + (A - prevA) * j / AUDIO_BLK;
      out((y * (a >> 4)) >> 12);
    }
    prevA = A;
    if (g_stopSound) return false;
  }
  return true;
}
static bool gasp(int ms) {
  int n = AUDIO_SR * ms / 1000;
  Reson r; r.set(2200, 900);
  for (int i = 0; i < n; i++) {
    float t = (float)i / n;
    int32_t a = (int32_t)(t * (1.0f - t) * 4 * 3500);
    out((r.step(noise16()) * a) >> 12);
    if ((i & 255) == 0 && g_stopSound) return false;
  }
  return true;
}

static void playHello() {
  if (!chirp(700, 950, 90, 0.6f)) return;
  if (!silence(30)) return;
  if (!chirp(950, 1300, 90, 0.6f)) return;
  if (!silence(30)) return;
  chirp(1300, 1900, 160, 0.6f);
}

static void playBlip() {
  float p = g_blipPitch;
  if (!chirp(p, p * 1.5f, 70, 0.55f)) return;
  chirp(p * 1.5f, p * 1.2f, 60, 0.45f);
}

static void playCry() {
  if (!wah(1000, 430, 560, 400, 1.0f)) return;
  if (!gasp(240)) return;
  if (!wah(800, 470, 610, 430, 1.0f)) return;
  if (!gasp(200)) return;
  if (!wah(1300, 420, 590, 360, 1.0f)) return;
  if (!gasp(260)) return;
  if (!wah(180, 380, 420, 360, 0.6f)) return;
  if (!silence(60)) return;
  wah(220, 380, 430, 340, 0.6f);
}

static void playBirds() {
  for (int phrase = 0; phrase < 5; phrase++) {
    int kind = random(3);
    if (kind == 0) {
      int c = random(2, 5);
      for (int k = 0; k < c; k++) {
        if (!chirp(random(4200, 5200), random(2400, 3000), random(60, 90), 0.8f)) return;
        if (!silence(random(30, 60))) return;
      }
    } else if (kind == 1) {
      if (!chirp(2200, 4800, 160, 0.8f)) return;
      if (!silence(50)) return;
      if (!chirp(4800, 3200, 90, 0.7f)) return;
    } else {
      if (!chirp(3700, 3300, 450, 0.7f, 28, 0.12f)) return;
    }
    if (!silence(random(150, 320))) return;
  }
}

static void audioTask(void *) {
  Sound s;
  for (;;) {
    if (xQueueReceive(g_soundQ, &s, portMAX_DELAY) != pdTRUE) continue;
    g_stopSound = false;
    g_playing = s;
    switch (s) {
      case SND_HELLO: playHello(); break;
      case SND_BLIP:  playBlip();  break;
      case SND_CRY:   playCry();   break;
      case SND_BIRDS: playBirds(); break;
      default: break;
    }
    g_stopSound = false;
    silence(40);
    flushAudio();
    g_soundLevel = 0;
    if (uxQueueMessagesWaiting(g_soundQ) == 0) g_playing = SND_NONE;
  }
}

void soundBegin() {
  for (int i = 0; i < 256; i++) SINE[i] = (int16_t)(sinf(i * 6.2832f / 256) * 32767);
  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN);
  if (!i2s.begin(I2S_MODE_STD, AUDIO_SR, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("I2S init failed, check the amplifier pins");
  }
  g_soundQ = xQueueCreate(1, sizeof(Sound));
  xTaskCreate(audioTask, "audio", 4096, nullptr, 2, nullptr);
}

void soundPlay(Sound s, uint16_t blipPitch = 0) {
  if (blipPitch) g_blipPitch = blipPitch;
  if (g_playing != SND_NONE) g_stopSound = true;
  g_playing = s;
  xQueueOverwrite(g_soundQ, &s);
}

void soundStop() { if (g_playing != SND_NONE) g_stopSound = true; }
bool soundBusy() { return g_playing != SND_NONE; }
float soundLevel() { return g_soundLevel; }
