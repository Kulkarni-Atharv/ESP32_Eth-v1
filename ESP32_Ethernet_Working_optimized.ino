#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ETH.h>
#include "DFRobotDFPlayerMini.h"

// =======================
// W5500 pins for Waveshare ESP32-S3-ETH
// =======================
#define ETH_SPI_SCK   13
#define ETH_SPI_MISO  12
#define ETH_SPI_MOSI  11
#define ETH_CS        14
#define ETH_INT       10
#define ETH_RST        9
#define ETH_ADDR       1

// =======================
// DFPlayer UART pins
// =======================
#define DF_RX 44
#define DF_TX 43

// =======================
// Network
// =======================
WiFiServer server(5000);
bool eth_connected = false;
bool server_started = false;

IPAddress local_IP(192, 168, 0, 62);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns1(8, 8, 8, 8);
IPAddress dns2(8, 8, 4, 4);
// =======================
// DFPlayer
// =======================
DFRobotDFPlayerMini dfPlayer;
bool df_ready = false;
unsigned long last_df_init_try = 0; 
const unsigned long DF_RETRY_MS = 800;

// =======================
// Ethernet events
// =======================
void onEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      ETH.setHostname("esp32-audio");
      break;

    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;

    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.println("ETH Got IP");
      Serial.print("IP: ");
      Serial.println(ETH.localIP());
      Serial.print("MAC: ");
      Serial.println(ETH.macAddress());
      eth_connected = true;

      if (!server_started) {
        server.begin();
        server_started = true;
        Serial.println("TCP server started on port 5000");
      }
      break;

    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      server_started = false;
      break;

    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stop");
      eth_connected = false;
      server_started = false;
      break;

    default:
      break;
  }
}

// =======================
// DFPlayer init / retry
// =======================
void tryInitDFPlayer() {
  if (df_ready) return;
  if (millis() - last_df_init_try < DF_RETRY_MS) return;

  last_df_init_try = millis();

  Serial.println("Trying DFPlayer init...");
  Serial2.begin(9600, SERIAL_8N1, DF_RX, DF_TX);

  if (dfPlayer.begin(Serial2, true, true)) {
    df_ready = true;
    dfPlayer.volume(5);
    Serial.println("DFPlayer ready");
  } else {
    Serial.println("DFPlayer not ready yet");
  }
}

// =======================
// Audio command executor
// returns true only if command really executed
// =======================
bool executeAudioCommand(const String &cmd) {
  if (!df_ready) return false;

  Serial.print("Executing command: ");
  Serial.println(cmd);

  // ================= AUDIO COMMANDS =================
  if (cmd == "PLAY:1") {
    dfPlayer.playMp3Folder(1);
    Serial.println("0001 - Please wear a helmet");
    return true;
  }
  else if (cmd == "PLAY:2") {
    dfPlayer.playMp3Folder(2);
    Serial.println("0002 - Helmet near a crane");
    return true;
  }
  else if (cmd == "PLAY:3") {
    dfPlayer.playMp3Folder(3);
    Serial.println("0003 - Safety harness");
    return true;
  }
  else if (cmd == "PLAY:4") {
    dfPlayer.playMp3Folder(4);
    Serial.println("0004 - Suspended load");
    return true;
  }

  // ================= STOP =================
  else if (cmd == "STOP") {
    dfPlayer.stop();
    Serial.println("Stopped");
    return true;
  }

  // ================= DIRECT VOLUME CONTROL =================
  else if (cmd.startsWith("VOL:")) {
    int vol = cmd.substring(4).toInt();

    if (vol >= 0 && vol <= 30) {
      dfPlayer.volume(vol);
      Serial.print("Volume set to: ");
      Serial.println(vol);
      return true;
    } else {
      Serial.println("Volume must be 0–30");
      return false;
    }
  }

  Serial.println("Unknown command");
  return false;
}

// =======================
// Handle client command
// wait a short time for DFPlayer if needed
// =======================
String handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return "EMPTY";

  Serial.print("Received command: ");
  Serial.println(cmd);

  // First try immediately
  if (executeAudioCommand(cmd)) {
    return "OK";
  }

  // If DFPlayer not ready yet, wait briefly and retry
  unsigned long waitStart = millis();
  while (millis() - waitStart < 1500) {
    tryInitDFPlayer();
    if (executeAudioCommand(cmd)) {
      return "OK";
    }
    delay(20);
  }

  Serial.println("DFPlayer still not ready, command not executed");
  return "BUSY";
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("Booting...");

  // Prepare UART early
  Serial2.begin(9600, SERIAL_8N1, DF_RX, DF_TX);

  // Ethernet first
  WiFi.onEvent(onEvent);

  pinMode(ETH_RST, OUTPUT);
  digitalWrite(ETH_RST, LOW);
  delay(10);
  digitalWrite(ETH_RST, HIGH);
  delay(50);

  SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI);

  bool ok = ETH.begin(ETH_PHY_W5500, ETH_ADDR, ETH_CS, ETH_INT, ETH_RST, SPI);
  Serial.print("ETH.begin returned: ");
  Serial.println(ok ? "true" : "false");

  if (ok) {
    if (!ETH.config(local_IP, gateway, subnet, dns1, dns2)) {
      Serial.println("Static IP config failed");
    } else {
      Serial.println("Static IP configured");
    }
  } else {
    Serial.println("ETH failed to start");
  }

  // First DFPlayer try without blocking boot too much
  tryInitDFPlayer();
}

void loop() {
  // Keep retrying DFPlayer in background until ready
  tryInitDFPlayer();

  if (!server_started) {
    delay(5);
    return;
  }

  WiFiClient client = server.available();

  if (client) {
    String msg = "";
    unsigned long start = millis();

    while (client.connected() && millis() - start < 1500) {
      while (client.available()) {
        char c = client.read();

        if (c == '\n' || c == '\r') {
          if (msg.length() > 0) {
            String result = handleCommand(msg);
            client.println(result);
            client.stop();
            return;
          }
        } else {
          msg += c;
        }
      }
      delay(1);
    }

    if (msg.length() > 0) {
      String result = handleCommand(msg);
      client.println(result);
    } else {
      client.println("EMPTY");
    }

    client.stop();
  }
}