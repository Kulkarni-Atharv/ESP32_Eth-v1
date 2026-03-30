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

// unique identity string for discovery
const char* DEVICE_ID = "ESP32_AUDIO_NODE";

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
      Serial.println("ETH Got IP (DHCP)");
      Serial.print("IP: ");
      Serial.println(ETH.localIP());
      Serial.print("Gateway: ");
      Serial.println(ETH.gatewayIP());
      Serial.print("Subnet: ");
      Serial.println(ETH.subnetMask());

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
    dfPlayer.volume(1);
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

  if (cmd == "PLAY:1") {
    dfPlayer.playMp3Folder(1);   // /mp3/0001.mp3
    Serial.println("Playing 0001.mp3");
    return true;
  }
  else if (cmd == "PLAY:2") {
    dfPlayer.playMp3Folder(2);   // /mp3/0002.mp3
    Serial.println("Playing 0002.mp3");
    return true;
  }
  else if (cmd == "STOP") {
    dfPlayer.stop();
    Serial.println("Stopped");
    return true;
  }
  else if (cmd == "VOLUP") {
    dfPlayer.volumeUp();
    Serial.println("Volume up");
    return true;
  }
  else if (cmd == "VOLDOWN") {
    dfPlayer.volumeDown();
    Serial.println("Volume down");
    return true;
  }

  Serial.println("Unknown audio command");
  return false;
}

// =======================
// Handle client command
// =======================
String handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return "EMPTY";

  Serial.print("Received command: ");
  Serial.println(cmd);

  // discovery command
  if (cmd == "PING") {
    Serial.println("PING received");
    return DEVICE_ID;
  }

  // optional: device info command
  if (cmd == "INFO") {
    String info = "ID:";
    info += DEVICE_ID;
    info += ",IP:";
    info += ETH.localIP().toString();
    return info;
  }

  // execute audio command immediately if possible
  if (executeAudioCommand(cmd)) {
    return "OK";
  }

  // if DFPlayer not ready yet, wait a bit and retry
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

  // Start Ethernet with DHCP
  bool ok = ETH.begin(ETH_PHY_W5500, ETH_ADDR, ETH_CS, ETH_INT, ETH_RST, SPI);
  Serial.print("ETH.begin returned: ");
  Serial.println(ok ? "true" : "false");

  if (ok) {
    Serial.println("Using DHCP (auto IP from router)...");
  } else {
    Serial.println("ETH failed to start");
  }

  // First DFPlayer try
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
    Serial.println("Client connected");

    String msg = "";
    unsigned long start = millis();

    while (client.connected() && millis() - start < 2000) {
      while (client.available()) {
        char c = client.read();

        if (c == '\n' || c == '\r') {
          if (msg.length() > 0) {
            String result = handleCommand(msg);
            client.println(result);
            client.stop();
            Serial.println("Client disconnected");
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
    Serial.println("Client disconnected");
  }
}