/*
  OledWifiManager.ino

  Programma dedicato per Arduino R4 + OLED SSD1309 128x64.
  - Scansione WiFi
  - Selezione rete con 3 pulsanti
  - Connessione automatica alla rete "Casa"
  - Info di rete (IP, RSSI, canale, gateway)

  Pulsanti (con pull-up interno):
    PREV  -> D2
    NEXT  -> D3
    SELECT-> D4
*/

#include <Arduino.h>
#include <U8g2lib.h>
#include <WiFiS3.h>
#include <string.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif

U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);

constexpr uint8_t kBtnPrev = 2;
constexpr uint8_t kBtnNext = 3;
constexpr uint8_t kBtnSel  = 4;

constexpr unsigned long kDebounceMs = 40;
constexpr unsigned long kConnectTimeoutMs = 15000;
constexpr unsigned long kDhcpTimeoutMs = 10000;

constexpr int kWifiMaxNetworks = 10;
char wifiSsid[kWifiMaxNetworks][33];
int wifiRssi[kWifiMaxNetworks];
bool wifiEncrypted[kWifiMaxNetworks];
int wifiCount = 0;
int wifiSelected = 0;

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

enum class UiState {
  Scan,
  List,
  Connecting,
  Dhcp,
  Info,
  Error
};

UiState state = UiState::Scan;
unsigned long stateStart = 0;
char errorMsg[32] = "";

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

void setState(UiState next, const char *msg = nullptr) {
  state = next;
  stateStart = millis();
  if (msg) {
    strncpy(errorMsg, msg, sizeof(errorMsg) - 1);
    errorMsg[sizeof(errorMsg) - 1] = '\0';
  }
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
  wifiSelected = 0;
}

void startConnection() {
  WiFi.disconnect();
  WiFi.begin(kKnownSsid, kKnownPass);
  setState(UiState::Connecting);
}

bool hasValidIp() {
  IPAddress ip = WiFi.localIP();
  return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

void updateWifiState() {
  switch (state) {
    case UiState::Scan:
      wifiScan();
      setState(UiState::List);
      break;
    case UiState::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        setState(UiState::Dhcp);
      } else if (millis() - stateStart > kConnectTimeoutMs) {
        setState(UiState::Error, "Timeout conn.");
      }
      break;
    case UiState::Dhcp:
      if (hasValidIp()) {
        setState(UiState::Info);
      } else if (millis() - stateStart > kDhcpTimeoutMs) {
        setState(UiState::Error, "DHCP timeout");
      }
      break;
    default:
      break;
  }
}

void drawHeader(const char *title) {
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 12, title);
  u8g2.drawHLine(0, 14, 128);
}

void drawScan() {
  drawHeader("WiFi Scan");
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 32, "Scansione in corso...");
}

void drawList() {
  drawHeader("Reti WiFi");
  u8g2.setFont(u8g2_font_5x8_tf);

  if (wifiCount == 0) {
    u8g2.drawStr(0, 30, "Nessuna rete trovata");
    u8g2.drawStr(0, 44, "SELECT: nuova scan");
    return;
  }

  int start = (wifiSelected / 4) * 4;
  int y = 26;
  for (int i = start; i < min(start + 4, wifiCount); i++) {
    if (i == wifiSelected) {
      u8g2.drawBox(0, y - 7, 128, 9);
      u8g2.setDrawColor(0);
    }
    char line[32];
    const char *lock = wifiEncrypted[i] ? "*" : " ";
    snprintf(line, sizeof(line), "%s %s %ddBm", lock, wifiSsid[i], wifiRssi[i]);
    u8g2.drawStr(2, y, line);
    if (i == wifiSelected) {
      u8g2.setDrawColor(1);
    }
    y += 10;
  }

  if (strcmp(wifiSsid[wifiSelected], kKnownSsid) == 0) {
    u8g2.drawStr(0, 62, "SEL: connetti Casa");
  } else {
    u8g2.drawStr(0, 62, "SEL: solo Casa");
  }
}

void drawConnecting(const char *label) {
  drawHeader(label);
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 30, "SSID: Casa");
  u8g2.drawStr(0, 44, "Attendi...");
}

void drawInfo() {
  drawHeader("Info WiFi");
  u8g2.setFont(u8g2_font_5x8_tf);

  IPAddress ip = WiFi.localIP();
  IPAddress gw = WiFi.gatewayIP();

  char line[32];
  snprintf(line, sizeof(line), "SSID: %s", WiFi.SSID().c_str());
  u8g2.drawStr(0, 24, line);
  snprintf(line, sizeof(line), "IP: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  u8g2.drawStr(0, 34, line);
  snprintf(line, sizeof(line), "RSSI: %ddBm", WiFi.RSSI());
  u8g2.drawStr(0, 44, line);
  snprintf(line, sizeof(line), "CH: %d", WiFi.channel());
  u8g2.drawStr(0, 54, line);
  snprintf(line, sizeof(line), "GW: %u.%u.%u.%u", gw[0], gw[1], gw[2], gw[3]);
  u8g2.drawStr(0, 62, line);
}

void drawError() {
  drawHeader("Errore");
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 30, errorMsg);
  u8g2.drawStr(0, 46, "SELECT: nuova scan");
}

void setup() {
  u8g2.begin();
  u8g2.setBusClock(8000000);

  pinMode(kBtnPrev, INPUT_PULLUP);
  pinMode(kBtnNext, INPUT_PULLUP);
  pinMode(kBtnSel, INPUT_PULLUP);

  WiFi.setHostname("u8g2-r4");
  setState(UiState::Scan);
}

void loop() {
  updateWifiState();

  if (wasPressed(btnPrev)) {
    if (state == UiState::List && wifiCount > 0) {
      wifiSelected = (wifiSelected + wifiCount - 1) % wifiCount;
    }
  }

  if (wasPressed(btnNext)) {
    if (state == UiState::List && wifiCount > 0) {
      wifiSelected = (wifiSelected + 1) % wifiCount;
    }
  }

  if (wasPressed(btnSel)) {
    if (state == UiState::List) {
      if (wifiCount == 0) {
        setState(UiState::Scan);
      } else if (strcmp(wifiSsid[wifiSelected], kKnownSsid) == 0) {
        startConnection();
      } else {
        setState(UiState::Error, "Rete non salvata");
      }
    } else if (state == UiState::Info) {
      WiFi.disconnect();
      setState(UiState::Scan);
    } else if (state == UiState::Error) {
      setState(UiState::Scan);
    }
  }

  u8g2.clearBuffer();
  switch (state) {
    case UiState::Scan:
      drawScan();
      break;
    case UiState::List:
      drawList();
      break;
    case UiState::Connecting:
      drawConnecting("Connessione");
      break;
    case UiState::Dhcp:
      drawConnecting("DHCP");
      break;
    case UiState::Info:
      drawInfo();
      break;
    case UiState::Error:
      drawError();
      break;
  }
  u8g2.sendBuffer();
}
