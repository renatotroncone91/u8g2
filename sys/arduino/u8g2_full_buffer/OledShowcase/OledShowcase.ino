/*
  OledShowcase.ino

  Demo "bello" con tre pulsanti per Arduino + OLED SSD1309 128x64.
  Schermate: Orologio, Onda, Starfield, Diagnostica.
  Pulsanti (con pull-up interno):
    PREV  -> D2
    NEXT  -> D3
    SELECT-> D4 (inverte i colori)
*/

#include <Arduino.h>
#include <U8g2lib.h>
#include <math.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif

U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);

constexpr uint8_t kBtnPrev = 2;
constexpr uint8_t kBtnNext = 3;
constexpr uint8_t kBtnSel  = 4;

constexpr uint8_t kPages = 4;
constexpr unsigned long kDebounceMs = 40;

struct ButtonState {
  uint8_t pin;
  bool stable;
  bool lastReading;
  unsigned long lastChange;
};

ButtonState btnPrev{ kBtnPrev, false, false, 0 };
ButtonState btnNext{ kBtnNext, false, false, 0 };
ButtonState btnSel{ kBtnSel,  false, false, 0 };

uint8_t page = 0;
bool invertColors = false;

unsigned long lastSecond = 0;
int seconds = 0;
int minutes = 34;
int hours   = 12;

unsigned long frameTimer = 0;
uint16_t frameCount = 0;
uint16_t fps = 0;

struct Star {
  float x;
  float y;
  float speed;
};

constexpr uint8_t kStarCount = 18;
Star stars[kStarCount];

bool wasPressed(ButtonState &btn) {
  bool reading = (digitalRead(btn.pin) == LOW);
  if (reading != btn.lastReading) {
    btn.lastChange = millis();
    btn.lastReading = reading;
  }

  if ((millis() - btn.lastChange) > kDebounceMs && reading != btn.stable) {
    btn.stable = reading;
    if (btn.stable) {
      return true;
    }
  }
  return false;
}

void updateClock() {
  if (millis() - lastSecond >= 1000) {
    lastSecond += 1000;
    seconds++;
    if (seconds >= 60) {
      seconds = 0;
      minutes++;
      if (minutes >= 60) {
        minutes = 0;
        hours = (hours + 1) % 24;
      }
    }
  }
}

void updateFps() {
  frameCount++;
  if (millis() - frameTimer >= 1000) {
    fps = frameCount;
    frameCount = 0;
    frameTimer += 1000;
  }
}

void initStars() {
  for (uint8_t i = 0; i < kStarCount; i++) {
    stars[i].x = random(0, 128);
    stars[i].y = random(0, 64);
    stars[i].speed = 0.6f + (random(0, 100) / 100.0f);
  }
}

void updateStars() {
  for (uint8_t i = 0; i < kStarCount; i++) {
    stars[i].x -= stars[i].speed;
    if (stars[i].x < 0) {
      stars[i].x = 127;
      stars[i].y = random(0, 64);
      stars[i].speed = 0.6f + (random(0, 100) / 100.0f);
    }
  }
}

void drawHeader(const char *title) {
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 12, title);
  u8g2.drawHLine(0, 14, 128);
}

void drawClockPage() {
  drawHeader("Orologio");

  char buf[6];
  sprintf(buf, "%02d:%02d", hours, minutes);
  u8g2.setFont(u8g2_font_logisoso28_tf);
  u8g2.drawStr(0, 50, buf);

  int cx = 104;
  int cy = 40;
  int r  = 16;
  u8g2.drawCircle(cx, cy, r);

  float a = (seconds / 60.0f) * 2.0f * PI - PI / 2.0f;
  int x = cx + cos(a) * r;
  int y = cy + sin(a) * r;
  u8g2.drawLine(cx, cy, x, y);

  int w = (seconds * 128) / 60;
  u8g2.drawBox(0, 58, w, 4);
}

void drawWavePage() {
  drawHeader("Onda");
  unsigned long t = millis();
  for (int x = 0; x < 128; x++) {
    float phase = (x / 10.0f) + (t / 220.0f);
    int y = 40 + sin(phase) * 14;
    u8g2.drawPixel(x, y);
  }
}

void drawStarfieldPage() {
  drawHeader("Starfield");
  updateStars();
  for (uint8_t i = 0; i < kStarCount; i++) {
    u8g2.drawPixel((int)stars[i].x, (int)stars[i].y);
  }

  u8g2.drawFrame(0, 16, 128, 48);
  u8g2.drawHLine(0, 40, 128);
}

void drawInfoPage() {
  drawHeader("Diagnostica");
  u8g2.setFont(u8g2_font_6x12_tf);

  char line[24];
  sprintf(line, "Uptime: %lus", millis() / 1000);
  u8g2.drawStr(0, 30, line);

  sprintf(line, "FPS: %u", fps);
  u8g2.drawStr(0, 44, line);

  sprintf(line, "BTN: %d %d %d",
          digitalRead(kBtnPrev) == LOW,
          digitalRead(kBtnNext) == LOW,
          digitalRead(kBtnSel) == LOW);
  u8g2.drawStr(0, 58, line);
}

void setup() {
  u8g2.begin();
  u8g2.setBusClock(8000000);

  pinMode(kBtnPrev, INPUT_PULLUP);
  pinMode(kBtnNext, INPUT_PULLUP);
  pinMode(kBtnSel, INPUT_PULLUP);

  randomSeed(micros());
  initStars();
  frameTimer = millis();
  lastSecond = millis();
}

void loop() {
  updateClock();
  updateFps();

  if (wasPressed(btnPrev)) {
    page = (page + kPages - 1) % kPages;
  }
  if (wasPressed(btnNext)) {
    page = (page + 1) % kPages;
  }
  if (wasPressed(btnSel)) {
    invertColors = !invertColors;
  }

  u8g2.clearBuffer();
  u8g2.setDrawColor(invertColors ? 0 : 1);

  switch (page) {
    case 0:
      drawClockPage();
      break;
    case 1:
      drawWavePage();
      break;
    case 2:
      drawStarfieldPage();
      break;
    default:
      drawInfoPage();
      break;
  }

  u8g2.sendBuffer();
}
