#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <vector>

// =====================================================
// MotoKey CORE 3.2.0 - SHARED TOKEN / 3 CLIENTS
// Board: ESP32 Dev Module
// Arduino-ESP32 core: 3.3.12
// NimBLE-Arduino: 2.5.2
// =====================================================

static const char* FW_VERSION = "3.2.0";
static const char* DEVICE_NAME = "MotoKey";
static const char* SERVICE_UUID = "6f4f0001-8f3a-4d7a-9c12-4a494552494b";
static const char* COMMAND_UUID = "6f4f0002-8f3a-4d7a-9c12-4a494552494b";

static const bool RELAY_ACTIVE_LOW = true;
static const uint8_t POWER_PIN = 21;
static const uint8_t START_PIN = 25;
static const uint8_t ON_LEVEL = RELAY_ACTIVE_LOW ? LOW : HIGH;
static const uint8_t OFF_LEVEL = RELAY_ACTIVE_LOW ? HIGH : LOW;

static const uint32_t ARM_WINDOW_MS = 15000;
static const uint32_t DEFAULT_PULSE_MS = 800;
static const uint32_t MIN_PULSE_MS = 500;
static const uint32_t MAX_PULSE_MS = 3000;

Preferences prefs;
Preferences pairPrefs;
NimBLEServer* bleServer = nullptr;
NimBLECharacteristic* commandCharacteristic = nullptr;

// Each connected phone gets its own BLE session, but every phone uses
// the same shared owner token stored once in NVS.
struct ClientSession {
  uint16_t connHandle;
  bool authorized;
};
std::vector<ClientSession> sessions;

bool powerOn = false;
bool startActive = false;
bool armed = false;
uint32_t armedUntil = 0;
uint32_t startOffAt = 0;
uint32_t pulseMs = DEFAULT_PULSE_MS;

String pairingCode;
String ownerToken;
bool devicePaired = false;

// Protect shared-token retrieval from repeated pairing-code guesses.
static uint8_t tokenCodeFailures = 0;
static uint32_t tokenCodeWindowStarted = 0;
static uint32_t tokenCodeLockedUntil = 0;
static const uint8_t TOKEN_CODE_MAX_FAILURES = 8;
static const uint32_t TOKEN_CODE_WINDOW_MS = 60000;
static const uint32_t TOKEN_CODE_LOCK_MS = 60000;

// ------------------- helpers -------------------

bool deadlineReached(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) >= 0;
}

int findSession(uint16_t handle) {
  for (size_t i = 0; i < sessions.size(); ++i) {
    if (sessions[i].connHandle == handle) return (int)i;
  }
  return -1;
}

bool isAuthorized(uint16_t handle) {
  int i = findSession(handle);
  return i >= 0 && sessions[i].authorized;
}

void sendTo(uint16_t handle, const String& message) {
  if (commandCharacteristic && handle != BLE_HS_CONN_HANDLE_NONE) {
    commandCharacteristic->setValue(message.c_str());
    bool ok = commandCharacteristic->notify(handle);
    if (message.startsWith("OWNER_TOKEN:")) {
      Serial.printf("[BLE OUT][%u][%s] OWNER_TOKEN:<redacted>\n", handle, ok ? "OK" : "FAIL");
    } else {
      Serial.printf("[BLE OUT][%u][%s] %s\n", handle, ok ? "OK" : "FAIL", message.c_str());
    }
  }
}

void broadcast(const String& message) {
  if (!commandCharacteristic) return;
  commandCharacteristic->setValue(message.c_str());
  commandCharacteristic->notify();
  Serial.println("[BLE BROADCAST] " + message);
}

void setPowerRelay(bool on) {
  powerOn = on;
  digitalWrite(POWER_PIN, on ? ON_LEVEL : OFF_LEVEL);
  Serial.printf("[GPIO] POWER=%s GPIO21=%s\n", on ? "ON" : "OFF", digitalRead(POWER_PIN) ? "HIGH" : "LOW");
}

void setStartRelay(bool on) {
  startActive = on;
  digitalWrite(START_PIN, on ? ON_LEVEL : OFF_LEVEL);
  Serial.printf("[GPIO] START=%s GPIO25=%s\n", on ? "ON" : "OFF", digitalRead(START_PIN) ? "HIGH" : "LOW");
}

void powerOff() { setPowerRelay(false); }
void startOff() { setStartRelay(false); startOffAt = 0; }
void safeOff() { armed = false; armedUntil = 0; powerOff(); startOff(); }

void sendStatus(uint16_t handle) {
  String response = "STATUS:";
  response += powerOn ? "POWER_ON" : "POWER_OFF";
  response += ":";
  response += startActive ? "START_ON" : "START_OFF";
  response += ":";
  response += armed ? "ARMED" : "SAFE";
  response += ":PULSE:" + String(pulseMs);
  sendTo(handle, response);
}

bool validPairingCode(const String& code) {
  if (!code.startsWith("MK ") || code.length() != 17) return false;
  for (int i = 3; i < code.length(); ++i) {
    char c = code[i];
    if (c == '-') continue;
    if (c < '0' || c > '9') return false;
  }
  return true;
}

bool validToken(const String& token) {
  if (token.length() != 32) return false;
  for (int i = 0; i < token.length(); ++i) {
    char c = token[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    if (!ok) return false;
  }
  return true;
}

// ------------------- USB provisioning -------------------

void processSerialCommand(String cmd) {
  cmd.trim();

  if (cmd == "RESET_PAIRING:CONFIRM") {
    safeOff();
    pairPrefs.begin("mkpair", false);
    pairPrefs.clear();
    pairPrefs.end();
    pairingCode = "";
    ownerToken = "";
    devicePaired = false;
    for (auto& session : sessions) session.authorized = false;
    Serial.println("PAIRING_RESET_OK");
    return;
  }

  if (cmd.startsWith("SET_PAIR_CODE:")) {
    String code = cmd.substring(14);
    code.trim();
    if (devicePaired) { Serial.println("PAIR_CODE_DENIED:ALREADY_PAIRED"); return; }
    if (!validPairingCode(code)) { Serial.println("PAIR_CODE_DENIED:INVALID_FORMAT"); return; }
    pairPrefs.begin("mkpair", false);
    pairPrefs.putString("code", code);
    pairPrefs.putBool("paired", false);
    pairPrefs.end();
    pairingCode = code;
    devicePaired = false;
    Serial.println("PAIR_CODE_OK");
    Serial.println("PAIR_CODE:" + pairingCode);
    return;
  }

  if (cmd == "GET_PAIR_STATUS") {
    Serial.println(devicePaired ? "PAIRED" : "NOT_PAIRED");
    Serial.println(pairingCode.length() ? "PAIR_CODE_SET" : "PAIR_CODE_EMPTY");
    return;
  }
  if (cmd == "GET_VERSION") { Serial.println("MOTOKEY:" + String(FW_VERSION)); return; }
  Serial.println("USB_UNKNOWN_COMMAND");
}

void readSerialCommands() {
  static String input;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') { processSerialCommand(input); input = ""; }
    else if (c != '\r') {
      if (input.length() < 120) input += c;
      else { input = ""; Serial.println("USB_COMMAND_TOO_LONG"); }
    }
  }
}

// ------------------- pairing and authentication -------------------

void processLegacyPair(uint16_t handle, const String& cmd) {
  // A board is paired exactly once. Further phones retrieve this SAME token
  // only after entering the common pairing code (GET_OWNER_TOKEN).
  if (devicePaired) { sendTo(handle, "PAIR_DENIED:ALREADY_PAIRED"); return; }
  int separator = cmd.indexOf(':', 5);
  if (separator < 0) { sendTo(handle, "PAIR_DENIED:FORMAT"); return; }

  String suppliedCode = cmd.substring(5, separator);
  suppliedCode.trim();
  suppliedCode.toUpperCase();
  String suppliedToken = cmd.substring(separator + 1);
  suppliedToken.trim();

  if (pairingCode.isEmpty()) { sendTo(handle, "PAIR_DENIED:NO_SETUP_CODE"); return; }

  const uint32_t now = millis();
  if (tokenCodeLockedUntil != 0 && !deadlineReached(now, tokenCodeLockedUntil)) {
    sendTo(handle, "PAIR_DENIED:RATE_LIMIT");
    return;
  }
  if (tokenCodeLockedUntil != 0 && deadlineReached(now, tokenCodeLockedUntil)) {
    tokenCodeLockedUntil = 0;
    tokenCodeFailures = 0;
    tokenCodeWindowStarted = now;
  }
  if (tokenCodeWindowStarted == 0 || (uint32_t)(now - tokenCodeWindowStarted) > TOKEN_CODE_WINDOW_MS) {
    tokenCodeWindowStarted = now;
    tokenCodeFailures = 0;
  }
  if (suppliedCode != pairingCode) {
    ++tokenCodeFailures;
    if (tokenCodeFailures >= TOKEN_CODE_MAX_FAILURES) {
      tokenCodeLockedUntil = now + TOKEN_CODE_LOCK_MS;
      tokenCodeFailures = 0;
      sendTo(handle, "PAIR_DENIED:RATE_LIMIT");
      Serial.println("[PAIR] Initial pairing temporarily rate-limited");
    } else {
      sendTo(handle, "PAIR_DENIED:WRONG_CODE");
    }
    return;
  }
  tokenCodeFailures = 0;
  tokenCodeWindowStarted = now;
  if (!validToken(suppliedToken)) { sendTo(handle, "PAIR_DENIED:INVALID_TOKEN"); return; }

  pairPrefs.begin("mkpair", false);
  pairPrefs.putString("token", suppliedToken); // one shared token for all phones
  pairPrefs.putBool("paired", true);
  pairPrefs.end();

  ownerToken = suppliedToken;
  devicePaired = true;

  int i = findSession(handle);
  if (i >= 0) sessions[i].authorized = true;

  sendTo(handle, "PAIR_OK");
  Serial.println("[PAIR] Shared MotoKey token created");
}

void processAuth(uint16_t handle, const String& cmd) {
  String suppliedToken = cmd.substring(5);
  suppliedToken.trim();

  if (!devicePaired || ownerToken.isEmpty()) {
    sendTo(handle, "AUTH_DENIED:NOT_PAIRED");
    return;
  }

  int i = findSession(handle);
  if (i < 0) {
    sendTo(handle, "AUTH_DENIED:SESSION_MISSING");
    return;
  }

  if (suppliedToken == ownerToken) {
    sessions[i].authorized = true;
    sendTo(handle, "AUTH_OK");
    Serial.printf("[AUTH] Shared-token client authorized on %u\n", handle);
    return;
  }

  sessions[i].authorized = false;
  sendTo(handle, "AUTH_DENIED:WRONG_TOKEN");
}

void processSharedTokenRequest(uint16_t handle, String suppliedCode) {
  suppliedCode.trim();
  suppliedCode.toUpperCase();

  const uint32_t now = millis();
  if (tokenCodeLockedUntil != 0 && !deadlineReached(now, tokenCodeLockedUntil)) {
    sendTo(handle, "TOKEN_DENIED:RATE_LIMIT");
    return;
  }
  if (tokenCodeLockedUntil != 0 && deadlineReached(now, tokenCodeLockedUntil)) {
    tokenCodeLockedUntil = 0;
    tokenCodeFailures = 0;
    tokenCodeWindowStarted = now;
  }
  if (tokenCodeWindowStarted == 0 || (uint32_t)(now - tokenCodeWindowStarted) > TOKEN_CODE_WINDOW_MS) {
    tokenCodeWindowStarted = now;
    tokenCodeFailures = 0;
  }

  if (!devicePaired || ownerToken.isEmpty()) {
    sendTo(handle, "TOKEN_DENIED:NOT_PAIRED");
    return;
  }
  if (pairingCode.isEmpty()) {
    sendTo(handle, "TOKEN_DENIED:NO_SETUP_CODE");
    return;
  }
  if (suppliedCode != pairingCode) {
    ++tokenCodeFailures;
    if (tokenCodeFailures >= TOKEN_CODE_MAX_FAILURES) {
      tokenCodeLockedUntil = now + TOKEN_CODE_LOCK_MS;
      tokenCodeFailures = 0;
      sendTo(handle, "TOKEN_DENIED:RATE_LIMIT");
      Serial.println("[PAIR] Shared-token retrieval temporarily rate-limited");
    } else {
      sendTo(handle, "TOKEN_DENIED:WRONG_CODE");
    }
    return;
  }

  tokenCodeFailures = 0;
  tokenCodeWindowStarted = now;
  // Return the already-established token. All phones therefore store exactly
  // the same 32-character token; no per-phone credentials are created.
  sendTo(handle, "OWNER_TOKEN:" + ownerToken);
  Serial.printf("[PAIR] Shared token issued to BLE session %u\n", handle);
}

// ------------------- commands -------------------

void processCommand(uint16_t handle, String cmd) {
  cmd.trim();
  if (cmd.isEmpty()) return;
  String upper = cmd; upper.toUpperCase();
  if (upper.startsWith("PAIR:")) {
    Serial.printf("[BLE IN][%u] PAIR:<redacted>\n", handle);
    processLegacyPair(handle, cmd);
    return;
  }
  if (upper.startsWith("AUTH:")) {
    Serial.printf("[BLE IN][%u] AUTH:<redacted>\n", handle);
    processAuth(handle, cmd);
    return;
  }
  if (upper.startsWith("GET_OWNER_TOKEN:")) {
    Serial.printf("[BLE IN][%u] GET_OWNER_TOKEN:<redacted>\n", handle);
    processSharedTokenRequest(handle, cmd.substring(16));
    return;
  }

  Serial.printf("[BLE IN][%u] %s\n", handle, upper.c_str());
  if (!isAuthorized(handle)) { sendTo(handle, "DENIED:NOT_AUTHORIZED"); return; }

  if (upper == "PING") { sendTo(handle, "PONG"); return; }
  if (upper == "IDENTIFY") { sendTo(handle, "MOTOKEY:" + String(FW_VERSION)); return; }
  if (upper == "STATUS") { sendStatus(handle); return; }
  if (upper == "IO_STATUS") {
    sendTo(handle, "IO:GPIO21:" + String(digitalRead(POWER_PIN) ? "HIGH" : "LOW") + ":GPIO25:" + String(digitalRead(START_PIN) ? "HIGH" : "LOW"));
    return;
  }
  if (upper == "GET_PULSE") { sendTo(handle, "PULSE:" + String(pulseMs)); return; }
  if (upper.startsWith("SET_PULSE:")) {
    if (startActive) { sendTo(handle, "DENIED:START_ACTIVE"); return; }
    uint32_t value = upper.substring(10).toInt();
    if (value < MIN_PULSE_MS || value > MAX_PULSE_MS) { sendTo(handle, "DENIED:PULSE_RANGE"); return; }
    pulseMs = value;
    prefs.begin("ierikhon", false); prefs.putUInt("pulse_ms", pulseMs); prefs.end();
    sendTo(handle, "PULSE_SET:" + String(pulseMs)); return;
  }
  if (upper == "POWER_ON") { setPowerRelay(true); sendTo(handle, "POWER_ON_OK"); return; }
  if (upper == "POWER_OFF") { powerOff(); sendTo(handle, "POWER_OFF_OK"); return; }
  if (upper == "ARM") {
    if (startActive) { sendTo(handle, "DENIED:START_ACTIVE"); return; }
    armed = true; armedUntil = millis() + ARM_WINDOW_MS; sendTo(handle, "ARMED:15000"); return;
  }
  if (upper == "START") {
    if (!armed) { sendTo(handle, "DENIED:NOT_ARMED"); return; }
    if (deadlineReached(millis(), armedUntil)) { armed = false; armedUntil = 0; sendTo(handle, "DENIED:ARM_EXPIRED"); return; }
    armed = false; armedUntil = 0; setStartRelay(true); startOffAt = millis() + pulseMs;
    sendTo(handle, "START_OK:" + String(pulseMs)); return;
  }
  if (upper == "DISARM" || upper == "STOP") { armed = false; armedUntil = 0; startOff(); sendTo(handle, "STOP_OK"); return; }
  if (upper == "ALL_OFF") { safeOff(); sendTo(handle, "ALL_OFF_OK"); return; }

  sendTo(handle, "DENIED:UNKNOWN_COMMAND");
}

// ------------------- BLE callbacks -------------------

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
    // In NimBLE-Arduino 2.5.2 the new value is set before onWrite() is called.
    NimBLEAttValue value = characteristic->getValue();

    String command;
    command.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
      command += static_cast<char>(value[i]);
    }

    processCommand(connInfo.getConnHandle(), command);
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    uint16_t handle = connInfo.getConnHandle();
    if (findSession(handle) < 0) {
      ClientSession session;
      session.connHandle = handle;
      session.authorized = false;
      sessions.push_back(session);
    }
    Serial.printf("[BLE] Client connected handle=%u clients=%u/3\n", handle, (unsigned)sessions.size());
    NimBLEDevice::startAdvertising();
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    uint16_t handle = connInfo.getConnHandle();
    for (size_t i = 0; i < sessions.size(); ++i) {
      if (sessions[i].connHandle == handle) { sessions.erase(sessions.begin() + i); break; }
    }
    Serial.printf("[BLE] Client disconnected handle=%u reason=%d remaining=%u/3\n", handle, reason, (unsigned)sessions.size());
    // Do not interrupt another connected controller. Fail-safe only after the last client leaves.
    if (sessions.empty()) safeOff();
    NimBLEDevice::startAdvertising();
  }
};

void startBle() {
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setMTU(64);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());
  bleServer->advertiseOnDisconnect(true);

  NimBLEService* service = bleServer->createService(SERVICE_UUID);
  commandCharacteristic = service->createCharacteristic(
    COMMAND_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY,
    512
  );
  static CommandCallbacks callbacks;
  commandCharacteristic->setCallbacks(&callbacks);
  commandCharacteristic->setValue("MOTOKEY_READY");
  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setName(DEVICE_NAME);
  advertising->enableScanResponse(true);
  NimBLEDevice::startAdvertising();
  Serial.println("[BLE] NimBLE advertising started");
}

// ------------------- setup / loop -------------------

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(POWER_PIN, OUTPUT);
  pinMode(START_PIN, OUTPUT);
  digitalWrite(POWER_PIN, OFF_LEVEL);
  digitalWrite(START_PIN, OFF_LEVEL);
  powerOn = false; startActive = false;

  Serial.println();
  Serial.println("================================");
  Serial.println("MotoKey CORE 3.2.0 SHARED TOKEN");
  Serial.println("ESP32 Dev Module / Arduino core 3.3.12");
  Serial.println("NimBLE-Arduino 2.5.2");
  Serial.println("================================");

  // Keep the existing pulse settings. Never clear namespace 'ierikhon'.
  prefs.begin("ierikhon", false);
  pulseMs = prefs.getUInt("pulse_ms", DEFAULT_PULSE_MS);
  if (pulseMs < MIN_PULSE_MS || pulseMs > MAX_PULSE_MS) { pulseMs = DEFAULT_PULSE_MS; prefs.putUInt("pulse_ms", pulseMs); }
  prefs.end();

  // Preserve legacy owner pairing keys: code, token, paired.
  pairPrefs.begin("mkpair", false);
  pairingCode = pairPrefs.getString("code", "");
  ownerToken = pairPrefs.getString("token", "");
  devicePaired = pairPrefs.getBool("paired", false);
  pairPrefs.end();

  Serial.println("Firmware: " + String(FW_VERSION));
  Serial.println("Pulse: " + String(pulseMs) + " ms");
  Serial.println("Pairing: " + String(devicePaired ? "PAIRED" : "NOT PAIRED"));
  Serial.println("Authorization: one shared token for up to 3 simultaneous phones");
  startBle();
  Serial.println("[SYSTEM] Ready");
  Serial.println("USB: SET_PAIR_CODE:MK 1234-5678-9012 | GET_PAIR_STATUS | GET_VERSION | RESET_PAIRING:CONFIRM");
  Serial.println("BLE auth: PAIR:<code>:<token> | AUTH:<shared-token> | GET_OWNER_TOKEN:<code>");
}

void loop() {
  readSerialCommands();
  uint32_t now = millis();
  if (armed && deadlineReached(now, armedUntil)) { armed = false; armedUntil = 0; Serial.println("[TIMER] ARM expired"); }
  if (startActive && startOffAt && deadlineReached(now, startOffAt)) { startOff(); Serial.println("[TIMER] START pulse finished"); }
  delay(5);
}
