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
// Security settings
// =======================
const char* AUTH_TOKEN = "autonext@2050";   
IPAddress allowedClientIP( 192,168,0,191);  

const size_t MAX_CMD_LEN = 40;
const unsigned long CLIENT_READ_TIMEOUT_MS = 1500;
const unsigned long DF_RETRY_MS = 800;
const unsigned long CMD_RATE_LIMIT_MS = 200;

unsigned long last_cmd_time = 0;

// =======================
// DFPlayer
// =======================
DFRobotDFPlayerMini dfPlayer;
bool df_ready = false;
unsigned long last_df_init_try = 0;

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
// =======================
bool executeAudioCommand(const String &cmd) {
  if (!df_ready) return false;

  Serial.print("Executing command: ");
  Serial.println(cmd);

  if (cmd == "PLAY:1") {
    dfPlayer.playMp3Folder(1);
    Serial.println("Playing 0001.mp3");
    return true;
  }
  else if (cmd == "PLAY:2") {
    dfPlayer.playMp3Folder(2);
    Serial.println("Playing 0002.mp3");
    return true;
  }
  else if (cmd == "PLAY:3") {
    dfPlayer.playMp3Folder(3);
    Serial.println("Playing 0003.mp3");
    return true;
  }
  else if (cmd == "PLAY:4") {
    dfPlayer.playMp3Folder(4);
    Serial.println("Playing 0004.mp3");
    return true;
  }
  else if (cmd == "STOP") {
    dfPlayer.stop();
    Serial.println("Stopped");
    return true;
  }

  else if (cmd.startsWith("VOL:")) {
    int vol = cmd.substring(4).toInt();

    if (vol < 0) vol = 0;
    if (vol > 30) vol = 30;

    dfPlayer.volume(vol);

    Serial.print("Volume set to: ");
    Serial.println(vol);

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

  Serial.println("Unknown command");
  return false;
}

// =======================
// Parse AUTH format
// Expected: AUTH:autonex@2050;PLAY:1
// =======================
bool parseAuthenticatedCommand(const String &input, String &actualCmd) {
  int sep = input.indexOf(';');
  if (sep < 0) return false;

  String authPart = input.substring(0, sep);
  String cmdPart  = input.substring(sep + 1);

  authPart.trim();
  cmdPart.trim();

  if (!authPart.startsWith("AUTH:")) return false;

  String token = authPart.substring(5);
  token.trim();

  if (token != AUTH_TOKEN) return false;
  if (cmdPart.length() == 0) return false;

  actualCmd = cmdPart;
  return true;
}

// =======================
// Handle client command
// =======================
String handleCommand(String rawCmd) {
  rawCmd.trim();

  if (rawCmd.length() == 0) return "EMPTY";
  if (rawCmd.length() > MAX_CMD_LEN) return "TOO_LONG";

  unsigned long now = millis();
  if (now - last_cmd_time < CMD_RATE_LIMIT_MS) {
    return "TOO_FAST";
  }
  last_cmd_time = now;

  String cmd = "";
  if (!parseAuthenticatedCommand(rawCmd, cmd)) {
    Serial.println("Authentication failed");
    return "DENIED";
  }

  Serial.print("Authenticated command: ");
  Serial.println(cmd);

  if (executeAudioCommand(cmd)) {
    return "OK";
  }

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

  Serial2.begin(9600, SERIAL_8N1, DF_RX, DF_TX);

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

  
  tryInitDFPlayer();
}

void loop() {
  
  tryInitDFPlayer();

  if (!server_started) {
    delay(5);
    return;
  }

  WiFiClient client = server.available();
  if (!client) {
    delay(1);
    return;
  }

  IPAddress remote = client.remoteIP();
  Serial.print("Client connected from: ");
  Serial.println(remote);

  
  if (remote != allowedClientIP) {
    Serial.println("Blocked unauthorized IP");
    client.println("DENIED_IP");
    client.stop();
    return;
  }

  String msg = "";
  unsigned long start = millis();

  while (client.connected() && millis() - start < CLIENT_READ_TIMEOUT_MS) {
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
        if (msg.length() < MAX_CMD_LEN) {
          msg += c;
        } else {
          client.println("TOO_LONG");
          client.stop();
          return;
        }
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