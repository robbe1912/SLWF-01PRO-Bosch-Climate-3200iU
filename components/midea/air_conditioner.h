#pragma once

#ifdef USE_ARDUINO

// MideaUART
#include <Appliance/AirConditioner/AirConditioner.h>

#include "appliance_base.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include <vector>
#include <cstdint>

namespace esphome::midea::ac {

using sensor::Sensor;
using climate::ClimateCall;
using climate::ClimatePreset;
using climate::ClimateTraits;
using climate::ClimateMode;
using climate::ClimateSwingMode;
using climate::ClimateFanMode;
using climate::ClimateModeMask;
using climate::ClimateSwingModeMask;
using climate::ClimatePresetMask;

class AirConditioner : public ApplianceBase<dudanov::midea::ac::AirConditioner>, public climate::Climate {
 public:
  void dump_config() override;
  void set_outdoor_temperature_sensor(Sensor *sensor) { this->outdoor_sensor_ = sensor; }
  void set_humidity_setpoint_sensor(Sensor *sensor) { this->humidity_sensor_ = sensor; }
  void set_power_sensor(Sensor *sensor) { this->power_sensor_ = sensor; }
  void on_status_change() override;
  void loop() override;

  /* ############### */
  /* ### ACTIONS ### */
  /* ############### */

  void do_follow_me(float temperature, bool use_fahrenheit, bool beeper = false);
  void do_display_toggle();
  void do_swing_step();
  void do_beeper_on() { this->set_beeper_feedback(true); }
  void do_beeper_off() { this->set_beeper_feedback(false); }
  void do_power_on() { this->base_.setPowerState(true); }
  void do_power_off() { this->base_.setPowerState(false); }
  void do_power_toggle() { this->base_.setPowerState(this->mode == ClimateMode::CLIMATE_MODE_OFF); }
  void set_supported_modes(ClimateModeMask modes) { this->supported_modes_ = modes; }
  void set_supported_swing_modes(ClimateSwingModeMask modes) { this->supported_swing_modes_ = modes; }
  void set_supported_presets(ClimatePresetMask presets) { this->supported_presets_ = presets; }
  void set_custom_presets(std::initializer_list<const char *> presets) { this->set_supported_custom_presets(presets); }
  void set_custom_fan_modes(std::initializer_list<const char *> modes) { this->set_supported_custom_fan_modes(modes); }

  /* ########################## */
  /* ### IONIZER / MUTE / TIMER ### */
  /* ########################## */

  /** Set ionizer switch entity pointer (called from Python codegen). */
  void set_ionizer_switch(switch_::Switch *sw) { this->ionizer_switch_ = sw; }
  /** Set mute (display) switch entity pointer. */
  void set_mute_switch(switch_::Switch *sw) { this->mute_switch_ = sw; }
  /** Set timer ON number entity pointer. */
  void set_timer_on_number(number::Number *num) { this->timer_on_number_ = num; }
  /** Set timer OFF number entity pointer. */
  void set_timer_off_number(number::Number *num) { this->timer_off_number_ = num; }
  /** Set fan speed number entity pointer. */
  void set_fan_speed_number(number::Number *num) { this->fan_speed_number_ = num; }

  /** Read ionizer state from cached status. */
  bool get_ionizer_state() const;
  /** Read mute (display off) state from cached status. */
  bool get_mute_state() const;
  /** Read ON timer in minutes from cached status. */
  uint16_t get_timer_on_minutes() const;
  /** Read OFF timer in minutes from cached status. */
  uint16_t get_timer_off_minutes() const;
  /** Read fan speed (0-100%) from cached status. */
  uint8_t get_fan_speed() const;

  /** Send ionizer on/off command. */
  void set_ionizer(bool state);
  /** Send display mute toggle (toggle-only per protocol). */
  void toggle_mute();
  /** Send timer ON command. */
  void set_timer_on(uint16_t minutes);
  /** Send timer OFF command. */
  void set_timer_off(uint16_t minutes);
  /** Send fan speed command (0-100%). */
  void set_fan_speed(uint8_t percent);

  /** Map dudanov FanMode to custom display string (with Custom detection). */
  const char *fan_mode_to_string(dudanov::midea::ac::FanMode mode);
  /** Map custom display string back to dudanov FanMode. */
  dudanov::midea::ac::FanMode string_to_fan_mode(const std::string &mode);

 protected:
  void control(const ClimateCall &call) override;
  ClimateTraits traits() override;
  ClimateModeMask supported_modes_{};
  ClimateSwingModeMask supported_swing_modes_{};
  ClimatePresetMask supported_presets_{};
  bool frost_protection_set_{false};
  Sensor *outdoor_sensor_{nullptr};
  Sensor *humidity_sensor_{nullptr};
  Sensor *power_sensor_{nullptr};
  switch_::Switch *ionizer_switch_{nullptr};
  switch_::Switch *mute_switch_{nullptr};
  number::Number *timer_on_number_{nullptr};
  number::Number *timer_off_number_{nullptr};
  number::Number *fan_speed_number_{nullptr};

  /* Periodic polling for custom entities */
  uint32_t last_poll_ms_{0};
  uint32_t last_full_query_ms_{0};
  uint32_t fan_speed_write_ms_{0};  // Cooldown after fan speed write
  std::vector<uint8_t> last_full_status_;  // Last full 0xC0 response payload
  static constexpr uint32_t POLL_INTERVAL_MS = 5000;        // 5s: poll ionizer+timer from m_status
  static constexpr uint32_t FULL_QUERY_INTERVAL_MS = 10000; // 10s: full 0xC0 query for mute (byte 14)
  static constexpr uint32_t FAN_SPEED_COOLDOWN_MS = 3000;   // 3s: skip fan speed poll after write
  bool full_query_pending_{false};  // Rate-limit: only one full query in-flight at a time
  bool in_status_change_{false};    // Re-entrancy guard for on_status_change

  /* Mute state persistence across AC power cycles */
  bool last_known_mute_{false};     // Saved mute state (persisted to flash)
  bool mute_restore_pending_{false}; // Need to restore mute after power-on
  uint32_t power_on_detected_ms_{0}; // Timestamp when power-on was detected
  static constexpr uint32_t MUTE_RESTORE_DELAY_MS = 2000;  // Wait 2s after power-on before toggling

  /** Poll m_status for ionizer + timer state (bytes 1-10 valid). */
  void poll_custom_entities();
  /** Send a custom 0xC0 status query to capture full response. */
  void send_full_status_query();
  /** Update custom entities (ionizer, mute, timer) from full response. */
  void update_custom_entities(const uint8_t *raw, size_t size);
};

}  // namespace esphome::midea::ac

#endif  // USE_ARDUINO
