#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>
#include <driver/gpio.h>
#include <esp_task_wdt.h>
#include "controller.h"
#include "web_page.h"

#if !CONFIG_IDF_TARGET_ESP32
#error WebBrew currently supports classic ESP32/WROOM-32 only. Review GPIOs before porting.
#endif

brew::Controller controller;
WebServer server(80);
Preferences prefs;
OneWire* wire = nullptr;
DallasTemperature* probe = nullptr;
DeviceAddress probeAddress;
bool probeFound = false;
unsigned conversions = 0;
uint32_t requestAt = 0, tickAt = 0, windowAt = 0;
String password, csrf, wifiSsid, wifiPassword, apName;
bool finishedSaved = false;

String randomToken(unsigned n) {
  const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  String out;
  while (n--) out += alphabet[esp_random() % (sizeof(alphabet)-1)];
  return out;
}
int offLevel(const brew::Config& c) { return brew::ssrLevel(c.activeHigh, false); }
void off() { gpio_set_level((gpio_num_t)controller.config.ssrPin, offLevel(controller.config)); }
void configureSSR(const brew::Config& c) {
  auto pin = (gpio_num_t)c.ssrPin;
  // Preload the output latch BEFORE switching direction. Internal bias reinforces OFF.
  gpio_set_level(pin, offLevel(c));
  gpio_set_pull_mode(pin, c.activeHigh ? GPIO_PULLDOWN_ONLY : GPIO_PULLUP_ONLY);
  gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}
void configureProbe() {
  delete probe; delete wire;
  wire = new OneWire(controller.config.probePin);
  probe = new DallasTemperature(wire);
  probe->begin();
  probeFound = probe->getDeviceCount() == 1 && probe->getAddress(probeAddress, 0);
  if (probeFound) probe->setResolution(probeAddress, 12);
  probe->setWaitForConversion(false);
  conversions = 0; controller.sensorOK = false;
  probe->requestTemperatures(); requestAt = millis();
}
void configToJson(JsonObject obj, const brew::Config& c) {
  obj["volumeL"] = c.volumeL; obj["powerW"] = c.powerW;
  obj["boilC"] = c.boilC; obj["maxC"] = c.maxC;
  obj["maxRunSeconds"] = c.maxRunSeconds;
  obj["ssrPin"] = c.ssrPin; obj["probePin"] = c.probePin;
  obj["activeHigh"] = c.activeHigh; obj["fahrenheit"] = c.fahrenheit; obj["learning"] = c.learning;
  auto stages = obj["stages"].to<JsonArray>();
  for (const auto& s : c.stages) {
    auto row = stages.add<JsonObject>();
    row["name"] = s.name; row["targetC"] = s.targetC; row["holdSeconds"] = s.holdSeconds;
    row["boil"] = s.boil; row["boilPower"] = s.boilPower;
  }
}
bool readConfig(JsonVariantConst o, brew::Config& c) {
  if (!o.is<JsonObjectConst>() || !o["volumeL"].is<double>() || !o["powerW"].is<double>() ||
      !o["boilC"].is<double>() || !o["maxC"].is<double>() || !o["maxRunSeconds"].is<unsigned>() ||
      !o["ssrPin"].is<int>() || !o["probePin"].is<int>() || !o["activeHigh"].is<bool>() ||
      !o["fahrenheit"].is<bool>() || !o["learning"].is<bool>() || !o["stages"].is<JsonArrayConst>()) return false;
  c.volumeL = o["volumeL"]; c.powerW = o["powerW"]; c.boilC = o["boilC"]; c.maxC = o["maxC"];
  c.maxRunSeconds = o["maxRunSeconds"]; c.ssrPin = o["ssrPin"]; c.probePin = o["probePin"];
  c.activeHigh = o["activeHigh"]; c.fahrenheit = o["fahrenheit"]; c.learning = o["learning"];
  c.stages.clear();
  if (o["stages"].size() > 8) return false;
  for (auto row : o["stages"].as<JsonArrayConst>()) {
    if (!row["name"].is<const char*>() || !row["targetC"].is<double>() || !row["holdSeconds"].is<unsigned>() ||
        !row["boil"].is<bool>() || !row["boilPower"].is<double>()) return false;
    c.stages.push_back({row["name"].as<std::string>(), row["targetC"].as<double>(), row["holdSeconds"].as<unsigned>(),
      row["boil"].as<bool>(), row["boilPower"].as<double>()});
  }
  return brew::validate(c).empty();
}
void reply(int code, const String& message) {
  JsonDocument d; d["message"] = message;
  String body; serializeJson(d, body); server.send(code, "application/json", body);
}
bool authenticated(bool mutation = false) {
  if (!server.authenticate("brew", password.c_str())) { server.requestAuthentication(); return false; }
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  if (mutation && server.header("X-WebBrew-Token") != csrf) { reply(403, "Reload the dashboard before making changes"); return false; }
  return true;
}
bool body(JsonDocument& d) {
  if (server.arg("plain").length() > 8192) { reply(413, "Request too large"); return false; }
  if (deserializeJson(d, server.arg("plain")) || !d.is<JsonObject>()) { reply(400, "Invalid JSON"); return false; }
  return true;
}
void status() {
  if (!authenticated()) return;
  JsonDocument d;
  d["csrf"] = csrf;
  d["state"] = brew::stateName(controller.state);
  d["manual"] = controller.manual; d["sensorOK"] = controller.sensorFresh(millis());
  if (controller.sensorFresh(millis())) d["temperatureC"] = controller.temperature;
  else d["temperatureC"] = nullptr;
  d["targetC"] = controller.target(); d["output"] = controller.output;
  d["manualPower"] = controller.manualPower; d["stageIndex"] = controller.stageIndex;
  d["reached"] = controller.reached; d["holdElapsed"] = controller.holdElapsed;
  d["runElapsed"] = controller.runElapsed; d["remainingSeconds"] = controller.remainingSeconds();
  d["fault"] = controller.fault; d["kp"] = controller.kp; d["ki"] = controller.ki; d["kd"] = controller.kd;
  d["efficiency"] = controller.efficiency; d["learnedSamples"] = controller.learnedSamples;
  d["wifiConnected"] = WiFi.status() == WL_CONNECTED; d["wifiSsid"] = wifiSsid;
  d["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  configToJson(d["config"].to<JsonObject>(), controller.config);
  String data; serializeJson(d, data); server.send(200, "application/json", data);
}
void saveConfig() {
  if (!authenticated(true)) return;
  if (controller.active() || controller.state == brew::State::Fault) { reply(409, "Stop the session and clear faults before changing settings"); return; }
  JsonDocument d; if (!body(d)) return;
  brew::Config next;
  if (!readConfig(d.as<JsonVariantConst>(), next)) { reply(400, "Invalid settings; check ranges, stage targets, pins and session limit"); return; }
  String serialized; serializeJson(d, serialized);
  // Persist first: no pin reassignment if storage failed.
  if (prefs.putString("config", serialized) != serialized.length()) { reply(500, "Could not save settings"); return; }
  const auto old = controller.config;
  off();
  if (next.ssrPin != old.ssrPin) gpio_set_direction((gpio_num_t)old.ssrPin, GPIO_MODE_INPUT);
  controller.config = next;
  configureSSR(next);
  if (next.probePin != old.probePin) configureProbe();
  controller.stageIndex = 0; controller.holdElapsed = 0;
  reply(200, "Settings saved");
}
void command() {
  if (!authenticated(true)) return;
  JsonDocument d; if (!body(d)) return;
  String action = d["action"] | "";
  bool ok = false;
  if (action == "start") { ok = controller.start(millis()); if (ok) { windowAt = millis(); finishedSaved = false; } }
  else if (action == "stop") {
    off();
    // Stop never clears a latched fault.
    if (controller.state != brew::State::Fault) controller.stop();
    ok = true;
  } else if (action == "pause") { ok = controller.pause(); off(); }
  else if (action == "resume") ok = controller.resume(millis());
  else if (action == "reset") ok = controller.resetFault(millis());
  else if (action == "mode" && d["manual"].is<bool>() && d["power"].is<double>()) {
    ok = controller.setManual(d["manual"], d["power"]); off(); windowAt = millis();
  }
  reply(ok ? 200 : 409, ok ? "Command accepted" : "Command unavailable; check session, probe and fault status");
}
void saveNetwork() {
  if (!authenticated(true)) return;
  if (controller.active() || controller.state == brew::State::Fault) { reply(409, "Stop heating and clear faults first"); return; }
  JsonDocument d; if (!body(d)) return;
  if (!d["ssid"].is<const char*>() || !d["password"].is<const char*>()) { reply(400, "SSID and password are required"); return; }
  String ssid = d["ssid"].as<String>(), pass = d["password"].as<String>();
  if (ssid.length() > 32 || pass.length() > 63 || (pass.length() && pass.length() < 8)) { reply(400, "Invalid Wi-Fi name or password length"); return; }
  wifiSsid = ssid; wifiPassword = pass;
  prefs.putString("ssid", wifiSsid); prefs.putString("wifiPass", wifiPassword);
  if (wifiSsid.length()) WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  else WiFi.disconnect(false, false);
  reply(200, "Wi-Fi saved; the setup access point stays available");
}
void changePassword() {
  if (!authenticated(true)) return;
  if (controller.active() || controller.state == brew::State::Fault) { reply(409, "Stop heating and clear faults first"); return; }
  JsonDocument d; if (!body(d)) return;
  String next = d["password"] | "";
  if (next.length() < 12 || next.length() > 63) { reply(400, "Use 12–63 characters"); return; }
  if (prefs.putString("password", next) != next.length()) { reply(500, "Could not save password"); return; }
  reply(200, "Password saved; reboot with heating disconnected to update the access point password");
  password = next; csrf = randomToken(24);
}
void setup() {
  Serial.begin(115200);
  prefs.begin("webbrew", false);
  String saved = prefs.getString("config", "");
  bool invalidStored = false;
  if (saved.length()) {
    JsonDocument d; brew::Config loaded;
    if (!deserializeJson(d, saved) && readConfig(d.as<JsonVariantConst>(), loaded)) controller.config = loaded;
    else invalidStored = true;
  }
  configureSSR(controller.config); off();
  if (invalidStored) controller.trip("Saved configuration invalid; reflash/clear NVS with heater disconnected");
  controller.efficiency = brew::clamp(prefs.getDouble("efficiency", 0.8), 0.2, 1.0);
  password = prefs.getString("password", "");
  if (password.length() < 12) { password = randomToken(16); prefs.putString("password", password); }
  csrf = randomToken(24);
  wifiSsid = prefs.getString("ssid", ""); wifiPassword = prefs.getString("wifiPass", "");
  uint64_t mac = ESP.getEfuseMac();
  char suffix[5]; snprintf(suffix, sizeof(suffix), "%04X", (unsigned)(mac & 0xFFFF));
  apName = String("WebBrew-") + suffix;
  WiFi.mode(WIFI_AP_STA); WiFi.setAutoReconnect(true);
  WiFi.softAP(apName.c_str(), password.c_str());
  if (wifiSsid.length()) WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  Serial.printf("\nWebBrew setup: %s\nWi-Fi / dashboard password: %s\nDashboard user: brew\nAddress: http://192.168.4.1\n", apName.c_str(), password.c_str());
  configureProbe();
  const char* headers[] = {"X-WebBrew-Token"}; server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, []() {
    if (!authenticated()) return;
    server.sendHeader("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
    server.send_P(200, "text/html; charset=utf-8", WEB_PAGE);
  });
  server.on("/api/status", HTTP_GET, status);
  server.on("/api/config", HTTP_POST, saveConfig);
  server.on("/api/command", HTTP_POST, command);
  server.on("/api/network", HTTP_POST, saveNetwork);
  server.on("/api/password", HTTP_POST, changePassword);
  server.onNotFound([]() { reply(404, "Not found"); });
  server.begin();
  esp_task_wdt_init(5, true); esp_task_wdt_add(nullptr);
  tickAt = windowAt = millis();
}
void loop() {
  uint32_t now = millis();
  if (uint32_t(now-requestAt) >= 1000) {
    ++conversions;
    // Discard first conversion; DS18B20's power-up register is 85 °C.
    if (probeFound && conversions > 1) controller.sample(probe->getTempC(probeAddress), now);
    else if (!probeFound) controller.sample(NAN, now);
    probe->requestTemperatures(); requestAt = now;
  }
  if (uint32_t(now-tickAt) >= 1000) { controller.tick(now); tickAt = now; }
  if (uint32_t(now-windowAt) >= 2000) windowAt += (uint32_t(now-windowAt)/2000)*2000;
  bool on = controller.state == brew::State::Running && controller.sensorFresh(now) &&
    controller.temperature < controller.config.maxC && uint32_t(now-windowAt) < controller.output*20;
  gpio_set_level((gpio_num_t)controller.config.ssrPin, on ? !offLevel(controller.config) : offLevel(controller.config));
  if (!controller.active() && !finishedSaved && controller.learnedSamples) {
    prefs.putDouble("efficiency", controller.efficiency); finishedSaved = true;
  }
  server.handleClient();
  esp_task_wdt_reset();
  delay(2);
}
