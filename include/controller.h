#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace brew {
inline double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
inline int ssrLevel(bool activeHigh, bool heating) { return heating == activeHigh ? 1 : 0; }
struct Stage {
  std::string name = "Warm-up & hold";
  double targetC = 78;
  unsigned holdSeconds = 600;
  bool boil = false;
  double boilPower = 70;
  Stage() = default;
  Stage(std::string label, double target, unsigned hold, bool boiling, double power)
    : name(std::move(label)), targetC(target), holdSeconds(hold), boil(boiling), boilPower(power) {}
};
struct Config {
  double volumeL = 25, powerW = 3000, boilC = 100, maxC = 105;
  unsigned maxRunSeconds = 4 * 3600;
  int ssrPin = 25, probePin = 4;
  bool activeHigh = true, fahrenheit = false, learning = true;
  bool sousVide = false;
  std::vector<Stage> stages = {{"Warm-up & hold", 78, 600, false, 70}, {"Boil", 100, 3600, true, 70}};
};
inline Config sousVideDefaults() {
  Config c;
  c.sousVide = true; c.volumeL = 10; c.powerW = 1000; c.maxC = 95;
  c.maxRunSeconds = 48 * 3600;
  c.stages = {{"Cook & hold", 60, 2 * 3600, false, 0}};
  return c;
}
inline bool safeOutputPin(int pin) {
  const int pins[] = {18,19,21,22,23,25,26,27,32,33};
  for (int p : pins) if (p == pin) return true;
  return false;
}
inline std::string validate(const Config& c) {
  if (!std::isfinite(c.volumeL) || c.volumeL < 1 || c.volumeL > 200) return "Volume must be 1–200 L";
  if (!std::isfinite(c.powerW) || c.powerW < 100 || c.powerW > 12000) return "Element power must be 100–12000 W";
  if (!std::isfinite(c.boilC) || c.boilC < 85 || c.boilC > 102) return "Boil point must be 85–102 °C";
  if (!std::isfinite(c.maxC) || (c.sousVide ? c.maxC < 42 || c.maxC > 95 : c.maxC < c.boilC + 2 || c.maxC > 110))
    return c.sousVide ? "Sous-vide cutoff must be 42–95 °C" : "Temperature cutoff must be at least 2 °C above boil point, at most 110 °C";
  if (c.maxRunSeconds < 60 || c.maxRunSeconds > (c.sousVide ? 72 : 12) * 3600u)
    return c.sousVide ? "Session limit must be 1–4320 min" : "Session limit must be 1–720 min";
  if (!safeOutputPin(c.ssrPin) || !(safeOutputPin(c.probePin) || c.probePin == 4) || c.ssrPin == c.probePin)
    return "Choose distinct supported SSR and probe GPIOs";
  if (c.stages.empty() || c.stages.size() > 8) return "Use 1–8 stages";
  uint64_t total = 0;
  for (const auto& s : c.stages) {
    if (s.name.empty() || s.name.size() > 48) return "Stage names must be 1–48 characters";
    if (c.sousVide) {
      if (s.boil) return "Boil stages are disabled in sous-vide mode";
      if (!std::isfinite(s.targetC) || s.targetC < 40 || s.targetC > 90) return "Sous-vide targets must be 40–90 °C";
      if (c.maxC < s.targetC + 2) return "Cutoff must be at least 2 °C above every sous-vide target";
    } else {
      if (!std::isfinite(s.targetC) || s.targetC < 20 || s.targetC > c.boilC) return "Stage targets must be 20 °C through the boil point";
      if (s.boil && std::abs(s.targetC - c.boilC) > 0.01) return "Boil stage target must equal the configured boil point";
      if (!s.boil && s.targetC > c.boilC - 2) return "Hold stages must be at least 2 °C below boiling";
    }
    if (!std::isfinite(s.boilPower) || s.boilPower < 0 || s.boilPower > 100) return "Boil power must be 0–100%";
    if (s.holdSeconds > (c.sousVide ? 48 : 6) * 3600u)
      return c.sousVide ? "Cook hold must be at most 2880 min" : "Stage hold must be at most 360 min";
    total += s.holdSeconds;
  }
  if (total >= c.maxRunSeconds) return "Session limit must exceed total hold time (allow time for heating)";
  return "";
}
enum class State { Idle, Running, Paused, Complete, Fault };
inline const char* stateName(State s) {
  switch (s) { case State::Running: return "running"; case State::Paused: return "paused";
    case State::Complete: return "complete"; case State::Fault: return "fault"; default: return "idle"; }
}

class Controller {
 public:
  Config config;
  State state = State::Idle;
  bool manual = false, reached = false, sensorOK = false;
  bool foodAdded = false;
  double temperature = 0, output = 0, manualPower = 0;
  double kp = 12, ki = 0.05, kd = 30, efficiency = 0.8;
  double holdElapsed = 0, runElapsed = 0;
  unsigned stageIndex = 0, learnedSamples = 0;
  std::string fault;

  void sample(double c, uint32_t now) {
    if (!std::isfinite(c) || c < -10 || c > 125) {
      sensorOK = false;
      if (active()) trip("Temperature probe disconnected or invalid");
      return;
    }
    temperature = c; sensorOK = true; lastSample_ = now; hasSample_ = true;
    if (c >= config.maxC) trip("Over-temperature cutoff");
  }
  bool active() const { return state == State::Running || state == State::Paused; }
  bool sensorFresh(uint32_t now) const { return hasSample_ && sensorOK && uint32_t(now - lastSample_) <= 3000; }
  bool start(uint32_t now) {
    if (state == State::Fault || active() || !validate(config).empty() || !sensorFresh(now) || temperature >= config.maxC) return false;
    state = State::Running; manual = false; reached = false; fault.clear(); stageIndex = 0;
    foodAdded = false;
    holdElapsed = runElapsed = output = manualPower = integral_ = 0;
    learnedSamples = 0; lastTick_ = now; lastTemp_ = temperature;
    tuneFromModel();
    learningSeconds_ = learningEnergy_ = 0; learningStart_ = temperature;
    return true;
  }
  void stop() { state = State::Idle; output = manualPower = 0; manual = false; reached = false; foodAdded = false; integral_ = 0; }
  bool confirmFood(uint32_t now) {
    if (!config.sousVide || state != State::Running || manual || foodAdded || !reached ||
        !sensorFresh(now) || std::abs(temperature-target()) > 0.3) return false;
    foodAdded = true; return true;
  }
  bool resetFault(uint32_t now) {
    if (state != State::Fault || !sensorFresh(now) || temperature >= config.maxC - 2) return false;
    fault.clear(); stop(); return true;
  }
  bool pause() {
    if (state != State::Running) return false;
    state = State::Paused; output = 0;
    learningSeconds_ = learningEnergy_ = 0; learningStart_ = temperature; return true;
  }
  bool resume(uint32_t now) {
    if (state != State::Paused || !sensorFresh(now)) return false;
    state = State::Running; integral_ = 0; lastTemp_ = temperature;
    learningSeconds_ = learningEnergy_ = 0; learningStart_ = temperature; return true;
  }
  bool setManual(bool enabled, double power) {
    if (!active() || !std::isfinite(power) || power < 0 || power > 100) return false;
    if (manual != enabled) {
      integral_ = 0; lastTemp_ = temperature;
      learningSeconds_ = learningEnergy_ = 0; learningStart_ = temperature;
    }
    manual = enabled; manualPower = enabled ? power : 0; output = 0; return true;
  }
  void trip(const std::string& reason) {
    if (state != State::Fault) fault = reason;
    state = State::Fault; output = manualPower = 0; manual = false;
  }
  double target() const { return config.stages[std::min<size_t>(stageIndex, config.stages.size()-1)].targetC; }
  // Sensible-heat estimate, revised using each run's measured effective efficiency.
  double heatingSeconds(double from, double to) const {
    return std::max(0.0, to-from) * config.volumeL * 4186.0 / (config.powerW * clamp(efficiency, 0.2, 1.0));
  }
  double remainingSeconds() const {
    if (!active()) return 0;
    const auto& s = config.stages[stageIndex];
    return heatingSeconds(temperature, s.targetC) + std::max(0.0, s.holdSeconds-holdElapsed);
  }
  void tick(uint32_t now) {
    double dt = uint32_t(now-lastTick_) / 1000.0;
    lastTick_ = now;
    if (!active()) { output = 0; return; }
    if (!sensorFresh(now)) { sensorOK = false; trip("Temperature probe stale"); return; }
    if (temperature >= config.maxC) { trip("Over-temperature cutoff"); return; }
    runElapsed += dt; // Manual and paused time count toward the hard session limit.
    if (runElapsed >= config.maxRunSeconds) { trip("Maximum session time exceeded"); return; }
    if (dt > 3) { trip("Control loop stalled"); return; }
    if (state == State::Paused) { output = 0; lastTemp_ = temperature; return; }
    if (manual) { output = manualPower; lastTemp_ = temperature; return; }
    auto& s = config.stages[stageIndex];
    bool inBand = config.sousVide ? std::abs(temperature-s.targetC) <= 0.3 : temperature >= s.targetC-1.0;
    if (config.sousVide ? inBand : temperature >= s.targetC - (s.boil ? 0.5 : 0.3)) reached = true;
    // Sous-vide needs an explicit food-added confirmation and a two-sided bath band.
    bool canTime = reached && (!config.sousVide || foodAdded);
    if (canTime && inBand) holdElapsed += dt;
    if (canTime && holdElapsed >= s.holdSeconds) {
      ++stageIndex; holdElapsed = 0; reached = false; integral_ = 0;
      learningSeconds_ = learningEnergy_ = 0; learningStart_ = temperature;
      if (stageIndex >= config.stages.size()) { state = State::Complete; output = 0; --stageIndex; return; }
      output = 0; lastTemp_ = temperature; return;
    }
    learn(dt, s);
    if (s.boil && reached) {
      output = temperature < s.targetC - 1.0 ? 100 : s.boilPower;
    } else {
      double error = s.targetC-temperature;
      double derivative = dt > 0 ? (temperature-lastTemp_)/dt : 0;
      // Integrator only near target; clamp and conditional integration prevent windup.
      double candidate = clamp(integral_ + ki*error*dt, 0, 65);
      double raw = kp*error + candidate - kd*derivative;
      if (std::abs(error) < 5 && ((raw < 100 && raw > 0) || (raw >= 100 && error < 0) || (raw <= 0 && error > 0))) integral_ = candidate;
      output = clamp(kp*error + integral_ - kd*derivative, 0, 100);
      if (temperature >= s.targetC + 0.5) output = 0;
    }
    lastTemp_ = temperature;
  }
 private:
  uint32_t lastSample_ = 0, lastTick_ = 0;
  bool hasSample_ = false;
  double integral_ = 0, lastTemp_ = 0;
  double learningSeconds_ = 0, learningEnergy_ = 0, learningStart_ = 0;
  void tuneFromModel() {
    double rate = config.powerW*efficiency/(config.volumeL*4186);
    kp = clamp(1.5 / std::max(rate, 0.01), 5, 25);
    ki = clamp(kp/240, 0.02, 0.12);
    kd = clamp(kp*3, 15, 75);
  }
  void learn(double dt, const Stage& s) {
    if (!config.learning || reached || temperature > s.targetC-5 || output < 60 || dt <= 0) {
      learningSeconds_ = learningEnergy_ = 0; learningStart_ = temperature; return;
    }
    learningSeconds_ += dt;
    learningEnergy_ += config.powerW * output/100 * dt;
    if (learningSeconds_ >= 30 && temperature-learningStart_ >= 0.5 && learningEnergy_ > 0) {
      double measured = clamp(config.volumeL*4186*(temperature-learningStart_) / learningEnergy_, 0.2, 1.0);
      efficiency = clamp(efficiency*0.75 + measured*0.25, 0.2, 1.0);
      tuneFromModel();
      ++learnedSamples;
      learningSeconds_ = learningEnergy_ = 0; learningStart_ = temperature;
    }
  }
};
} // namespace brew
