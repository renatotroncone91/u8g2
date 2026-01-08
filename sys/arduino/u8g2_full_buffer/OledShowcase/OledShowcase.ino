/*
  OledShowcase.ino

  Demo "bello" con tre pulsanti per Arduino + OLED SSD1309 128x64.
  Schermate: Orologio, Onda, WiFi Scanner, WiFi Info.
  Pulsanti (con pull-up interno):
    PREV  -> D2
    NEXT  -> D3
    SELECT-> D4 (azione contestuale / inverti colori)
*/

#include <Arduino.h>
#include <U8g2lib.h>
#include <math.h>
#include <WiFiS3.h>
#include <string.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif

U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);

constexpr uint8_t kBtnPrev = 2;
constexpr uint8_t kBtnNext = 3;
constexpr uint8_t kBtnSel  = 4;

constexpr uint8_t kPages = 4;
constexpr unsigned long kDebounceMs = 40;
constexpr unsigned long kWifiRescanMs = 15000;

constexpr int kWifiMaxNetworks = 10;
char wifiSsid[kWifiMaxNetworks][33];
int wifiRssi[kWifiMaxNetworks];
bool wifiEncrypted[kWifiMaxNetworks];
int wifiCount = 0;

const char *kKnownSsid = "Casa";
const char *kKnownPass = "04071991";

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
bool wifiConnected = false;
bool wifiConnecting = false;
unsigned long wifiLastScan = 0;
unsigned long wifiConnectStart = 0;
int wifiSelected = 0;
int wifiStatus = WL_IDLE_STATUS;

unsigned long lastSecond = 0;
int seconds = 0;
int minutes = 34;
int hours   = 12;

unsigned long frameTimer = 0;
uint16_t frameCount = 0;
uint16_t fps = 0;

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

void wifiScan() {
  wifiCount = 0;
  int found = WiFi.scanNetworks();
  if (found <= 0) {
    return;
  }
  wifiCount = min(found, kWifiMaxNetworks);
  for (int i = 0; i < wifiCount; i++) {
    String ssid = WiFi.SSID(i);
    ssid.toCharArray(wifiSsid[i], sizeof(wifiSsid[i]));
    wifiRssi[i] = WiFi.RSSI(i);
    wifiEncrypted[i] = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
  }
}

void wifiStartConnect() {
  wifiConnecting = true;
  wifiConnectStart = millis();
  WiFi.disconnect();
  WiFi.begin(kKnownSsid, kKnownPass);
}

void wifiUpdateStatus() {
  wifiStatus = WiFi.status();
  wifiConnected = (wifiStatus == WL_CONNECTED);
  if (wifiConnecting && wifiConnected) {
    wifiConnecting = false;
  }
  if (wifiConnecting && (millis() - wifiConnectStart > 15000)) {
    wifiConnecting = false;
  }
}

void drawWifiPage() {
  drawHeader("WiFi Scanner");
  u8g2.setFont(u8g2_font_5x8_tf);

  if (wifiCount == 0) {
    u8g2.drawStr(0, 28, "Nessuna rete trovata");
    u8g2.drawStr(0, 40, "PREV/NEXT: pagine");
    u8g2.drawStr(0, 52, "SEL: forza scan");
    return;
  }

  int start = (wifiSelected / 4) * 4;
  int y = 26;
  for (int i = start; i < min(start + 4, wifiCount); i++) {
    if (i == wifiSelected) {
      u8g2.drawBox(0, y - 7, 128, 9);
      u8g2.setDrawColor(invertColors ? 1 : 0);
    }
    char line[32];
    const char *lock = wifiEncrypted[i] ? "*" : " ";
    snprintf(line, sizeof(line), "%s %s %ddBm", lock, wifiSsid[i], wifiRssi[i]);
    u8g2.drawStr(2, y, line);
    if (i == wifiSelected) {
      u8g2.setDrawColor(invertColors ? 0 : 1);
    }
    y += 10;
  }

  u8g2.setFont(u8g2_font_5x8_tf);
  if (strcmp(wifiSsid[wifiSelected], kKnownSsid) == 0) {
    u8g2.drawStr(0, 62, "SEL: connetti Casa");
  } else {
    u8g2.drawStr(0, 62, "SEL: nuova scansione");
  }
}

void drawWifiInfoPage() {
  drawHeader("WiFi Info");
  u8g2.setFont(u8g2_font_5x8_tf);

  char line[32];
  if (wifiConnected) {
    IPAddress ip = WiFi.localIP();
    snprintf(line, sizeof(line), "SSID: %s", WiFi.SSID().c_str());
    u8g2.drawStr(0, 26, line);
    snprintf(line, sizeof(line), "IP: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    u8g2.drawStr(0, 36, line);
    snprintf(line, sizeof(line), "RSSI: %ddBm", WiFi.RSSI());
    u8g2.drawStr(0, 46, line);
    snprintf(line, sizeof(line), "CH: %d", WiFi.channel());
    u8g2.drawStr(0, 56, line);
    return;
  }

  if (wifiConnecting) {
    u8g2.drawStr(0, 30, "Connessione...");
    u8g2.drawStr(0, 42, kKnownSsid);
    return;
  }

  u8g2.drawStr(0, 30, "Non connesso");
  u8g2.drawStr(0, 42, "SEL: connetti Casa");
}

void setup() {
  u8g2.begin();
  u8g2.setBusClock(8000000);

  pinMode(kBtnPrev, INPUT_PULLUP);
  pinMode(kBtnNext, INPUT_PULLUP);
  pinMode(kBtnSel, INPUT_PULLUP);

  randomSeed(micros());
  frameTimer = millis();
  lastSecond = millis();
  wifiLastScan = 0;
  WiFi.setHostname("u8g2-r4");
}

void loop() {
  updateClock();
  updateFps();
  wifiUpdateStatus();

  if (millis() - wifiLastScan >= kWifiRescanMs) {
    wifiLastScan = millis();
    wifiScan();
  }

  if (wasPressed(btnPrev)) {
    if (page == 2 && wifiCount > 0) {
      wifiSelected = (wifiSelected + wifiCount - 1) % wifiCount;
    } else {
      page = (page + kPages - 1) % kPages;
    }
  }
  if (wasPressed(btnNext)) {
    if (page == 2 && wifiCount > 0) {
      wifiSelected = (wifiSelected + 1) % wifiCount;
    } else {
      page = (page + 1) % kPages;
    }
  }
  if (wasPressed(btnSel)) {
    if (page == 3) {
      if (!wifiConnecting && !wifiConnected) {
        wifiStartConnect();
      }
    } else if (page == 2) {
      if (wifiCount == 0) {
        wifiScan();
      } else if (strcmp(wifiSsid[wifiSelected], kKnownSsid) == 0) {
        wifiStartConnect();
      } else {
        wifiScan();
      }
    } else {
      invertColors = !invertColors;
    }
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
      drawWifiPage();
      break;
    default:
      drawWifiInfoPage();
      break;
  }

  u8g2.sendBuffer();
}
