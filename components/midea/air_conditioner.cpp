#ifdef USE_ARDUINO

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "air_conditioner.h"
#include "midea_accessor.h"
#include "ac_adapter.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <functional>

namespace esphome::midea::ac {

using dudanov_ac = dudanov::midea::ac::AirConditioner;
using FrameType = dudanov::midea::FrameType;
using FrameData = dudanov::midea::FrameData;
using ResponseStatus = dudanov::midea::ResponseStatus;
using MideaFanMode = dudanov::midea::ac::FanMode;

static void set_sensor(Sensor *sensor, float value) {
  if (sensor != nullptr && (!sensor->has_state() || sensor->get_raw_state() != value))
    sensor->publish_state(value);
}

template<typename T> void update_property(T &property, const T &value, bool &flag) {
  if (property != value) {
    property = value;
    flag = true;
  }
}

void AirConditioner::on_status_change() {
  // Add frost protection custom preset once when autoconf completes
  if (this->base_.getAutoconfStatus() == dudanov::midea::AUTOCONF_OK &&
      this->base_.getCapabilities().supportFrostProtectionPreset() && !this->frost_protection_set_) {
    auto traits = this->get_traits();
    const auto &existing = traits.get_supported_custom_presets();
    bool found = false;
    for (const char *p : existing) {
      if (strcmp(p, Constants::FREEZE_PROTECTION) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      std::vector<const char *> merged(existing.begin(), existing.end());
      merged.push_back(Constants::FREEZE_PROTECTION);
      this->set_supported_custom_presets(merged);
    }
    this->frost_protection_set_ = true;
  }
  bool need_publish = false;
  update_property(this->target_temperature, this->base_.getTargetTemp(), need_publish);
  update_property(this->current_temperature, this->base_.getIndoorTemp(), need_publish);
  auto mode = Converters::to_climate_mode(this->base_.getMode());
  update_property(this->mode, mode, need_publish);
  auto swing_mode = Converters::to_climate_swing_mode(this->base_.getSwingMode());
  update_property(this->swing_mode, swing_mode, need_publish);
  // Preset
  auto preset = this->base_.getPreset();
  if (Converters::is_custom_midea_preset(preset)) {
    if (this->set_custom_preset_(Converters::to_custom_climate_preset(preset)))
      need_publish = true;
  } else if (this->set_preset_(Converters::to_climate_preset(preset))) {
    need_publish = true;
  }
  // Fan mode — standard AUTO + custom strings (percentage-based labels)
  auto fan_mode = this->base_.getFanMode();
  uint8_t raw_fan = static_cast<uint8_t>(fan_mode);
  if (raw_fan == MideaFanMode::FAN_AUTO) {
    // byte 3 = 102 = standard auto
    if (this->set_fan_mode_(ClimateFanMode::CLIMATE_FAN_AUTO))
      need_publish = true;
  } else if (raw_fan >= 1 && raw_fan <= 100) {
    // Custom speed — use percentage-based label
    const char *fan_str = this->fan_mode_to_string(fan_mode);
    if (this->set_custom_fan_mode_(fan_str))
      need_publish = true;
  }
  if (need_publish)
    this->publish_state();
  set_sensor(this->outdoor_sensor_, this->base_.getOutdoorTemp());
  set_sensor(this->power_sensor_, this->base_.getPowerUsage());
  set_sensor(this->humidity_sensor_, this->base_.getIndoorHum());

  // Also update custom entities when standard props change (bonus update)
  this->poll_custom_entities();
  if (!this->last_full_status_.empty() && this->last_full_status_.size() > 14) {
    this->update_custom_entities(this->last_full_status_.data(), this->last_full_status_.size());
  }
}

void AirConditioner::loop() {
  // Call parent loop (runs dudanov base_.loop())
  ApplianceBase<dudanov_ac>::loop();

  // Periodic polling for custom entities that on_status_change() misses
  uint32_t now = millis();
  if (now - this->last_poll_ms_ < POLL_INTERVAL_MS)
    return;
  this->last_poll_ms_ = now;

  // Poll ionizer + timer from m_status (bytes 1-10 are valid after copyStatus)
  this->poll_custom_entities();

  // Periodic full status query for mute (byte 14 not in copyStatus range)
  if (now - this->last_full_query_ms_ >= FULL_QUERY_INTERVAL_MS && !this->full_query_pending_) {
    this->last_full_query_ms_ = now;
    this->send_full_status_query();
  }
}

void AirConditioner::poll_custom_entities() {
  const auto &status = MideaAccessor::get_status(this->base_);
  const uint8_t *raw = status.data();
  size_t raw_size = status.size();

  if (raw_size < 10) return;  // Need at least 10 bytes

  // Ionizer: byte 9 bit 5 (0x20)
  if (this->ionizer_switch_ != nullptr) {
    bool ionizer = (raw[9] & 0x20) != 0;
    if (!this->ionizer_switch_->has_state() || this->ionizer_switch_->state != ionizer)
      this->ionizer_switch_->publish_state(ionizer);
  }

  // Timer ON: byte 4
  if (this->timer_on_number_ != nullptr) {
    uint8_t on_byte = raw[4];
    uint16_t on_min = (on_byte == 0x7F) ? 0 : (on_byte - 0x7F) * 15;
    if (!this->timer_on_number_->has_state() || this->timer_on_number_->state != on_min)
      this->timer_on_number_->publish_state(on_min);
  }

  // Timer OFF: byte 5
  if (this->timer_off_number_ != nullptr) {
    uint8_t off_byte = raw[5];
    uint16_t off_min = (off_byte == 0x7F) ? 0 : (off_byte - 0x7F) * 15;
    if (!this->timer_off_number_->has_state() || this->timer_off_number_->state != off_min)
      this->timer_off_number_->publish_state(off_min);
  }

  // Fan speed: byte 3 (0 = auto, 1-100 = manual percentage, 102 = auto from AC)
  // Map: 102 (AC auto) → 0 (our "auto" value for slider)
  if (this->fan_speed_number_ != nullptr) {
    if (millis() - this->fan_speed_write_ms_ >= FAN_SPEED_COOLDOWN_MS) {
      uint8_t speed = raw[3];
      float slider_val = (speed == 102) ? 0.0f : (speed <= 100) ? (float)speed : 0.0f;
      if (!this->fan_speed_number_->has_state() || this->fan_speed_number_->state != slider_val)
        this->fan_speed_number_->publish_state(slider_val);
    }
  }
}

void AirConditioner::send_full_status_query() {
  if (this->full_query_pending_) return;  // Rate-limit: one at a time
  this->full_query_pending_ = true;

  // Build 0xC0 status query payload (22 bytes)
  // Format: 41 81 00 FF 03 FF 00 02 00x12 03 <msg_id>
  static uint8_t msg_id = 0;
  std::vector<uint8_t> query = {
      0x41, 0x81, 0x00, 0xFF, 0x03, 0xFF, 0x00, 0x02,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x03, msg_id++
  };

  FrameData fd(query.begin(), query.end());
  fd.appendCRC();  // CRC required — AC rejects frames without it

  MideaAccessor::queue_raw(this->base_, FrameType::DEVICE_QUERY, std::move(fd),
      // onData: capture full response for mute + update all custom entities
      [this](FrameData data) -> ResponseStatus {
        this->full_query_pending_ = false;  // Allow next query
        if (data.size() > 2 && data.data()[0] == 0xC0) {
          this->last_full_status_.assign(data.data(), data.data() + data.size());
          this->update_custom_entities(data.data(), data.size());
        }
        return ResponseStatus::RESPONSE_OK;
      },
      // onError: clear pending flag
      [this]() { this->full_query_pending_ = false; },
      nullptr);
}

void AirConditioner::update_custom_entities(const uint8_t *raw, size_t size) {
  ESP_LOGD(Constants::TAG, "update_custom_entities: size=%zu", size);

  // Ionizer: byte 9 bit 5 (0x20)
  if (this->ionizer_switch_ != nullptr && size > 9) {
    bool ionizer = (raw[9] & 0x20) != 0;
    ESP_LOGD(Constants::TAG, "  ionizer byte[9]=0x%02X → %s", raw[9], ionizer ? "ON" : "OFF");
    if (!this->ionizer_switch_->has_state() || this->ionizer_switch_->state != ionizer)
      this->ionizer_switch_->publish_state(ionizer);
  }

  // Mute (display off): byte 14 mask 0x70
  if (this->mute_switch_ != nullptr && size > 14) {
    bool muted = (raw[14] & 0x70) != 0;
    ESP_LOGD(Constants::TAG, "  mute byte[14]=0x%02X → %s", raw[14], muted ? "MUTED" : "NORMAL");
    if (!this->mute_switch_->has_state() || this->mute_switch_->state != muted)
      this->mute_switch_->publish_state(muted);
  }

  // Timer ON: byte 4, Timer OFF: byte 5
  if (this->timer_on_number_ != nullptr && size > 4) {
    uint8_t on_byte = raw[4];
    uint16_t on_min = (on_byte == 0x7F) ? 0 : (on_byte - 0x7F) * 15;
    ESP_LOGD(Constants::TAG, "  timer_on byte[4]=0x%02X → %u min", on_byte, on_min);
    if (!this->timer_on_number_->has_state() || this->timer_on_number_->state != on_min)
      this->timer_on_number_->publish_state(on_min);
  }
  if (this->timer_off_number_ != nullptr && size > 5) {
    uint8_t off_byte = raw[5];
    uint16_t off_min = (off_byte == 0x7F) ? 0 : (off_byte - 0x7F) * 15;
    ESP_LOGD(Constants::TAG, "  timer_off byte[5]=0x%02X → %u min", off_byte, off_min);
    if (!this->timer_off_number_->has_state() || this->timer_off_number_->state != off_min)
      this->timer_off_number_->publish_state(off_min);
  }

  // Fan speed: byte 3 (0 = auto, 1-100 = manual percentage, 102 = auto from AC)
  // Map: 102 (AC auto) → 0 (our "auto" value for slider)
  if (this->fan_speed_number_ != nullptr && size > 3) {
    if (millis() - this->fan_speed_write_ms_ >= FAN_SPEED_COOLDOWN_MS) {
      uint8_t speed = raw[3];
      ESP_LOGD(Constants::TAG, "  fan_speed byte[3]=%u → slider=%u", speed, speed == 102 ? 0 : speed);
      float slider_val = (speed == 102) ? 0.0f : (speed <= 100) ? (float)speed : 0.0f;
      if (!this->fan_speed_number_->has_state() || this->fan_speed_number_->state != slider_val)
        this->fan_speed_number_->publish_state(slider_val);
    }
  }
}

void AirConditioner::control(const ClimateCall &call) {
  dudanov::midea::ac::Control ctrl{};
  ESP_LOGD(Constants::TAG, "control() called");
  auto target_temp_val = call.get_target_temperature();
  if (target_temp_val.has_value())
    ctrl.targetTemp = *target_temp_val;
  auto swing_mode_val = call.get_swing_mode();
  if (swing_mode_val.has_value())
    ctrl.swingMode = Converters::to_midea_swing_mode(*swing_mode_val);
  auto mode_val = call.get_mode();
  if (mode_val.has_value())
    ctrl.mode = Converters::to_midea_mode(*mode_val);
  auto preset_val = call.get_preset();
  if (preset_val.has_value()) {
    ctrl.preset = Converters::to_midea_preset(*preset_val);
  } else if (call.has_custom_preset()) {
    ctrl.preset = Converters::to_midea_preset(call.get_custom_preset().c_str());
  }
  auto fan_mode_val = call.get_fan_mode();
  if (fan_mode_val.has_value()) {
    ESP_LOGD(Constants::TAG, "control: standard fan_mode=%d", (int)*fan_mode_val);
    ctrl.fanMode = Converters::to_midea_fan_mode(*fan_mode_val);
  } else if (call.has_custom_fan_mode()) {
    const std::string &fm = call.get_custom_fan_mode();
    ESP_LOGD(Constants::TAG, "control: custom fan_mode=%s", fm.c_str());
    ctrl.fanMode = this->string_to_fan_mode(fm);
  } else {
    ESP_LOGD(Constants::TAG, "control: NO fan_mode set in call");
  }
  this->base_.control(ctrl);
}

ClimateTraits AirConditioner::traits() {
  auto traits = ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_min_temperature(17);
  traits.set_visual_max_temperature(30);
  traits.set_visual_temperature_step(0.5);
  traits.set_supported_modes(this->supported_modes_);
  traits.set_supported_swing_modes(this->supported_swing_modes_);
  traits.set_supported_presets(this->supported_presets_);
  // Standard fan modes: AUTO is always available
  traits.add_supported_fan_mode(ClimateFanMode::CLIMATE_FAN_AUTO);
  // Custom fan modes for percentage-based labels (no CUSTOM in dropdown)
  const std::vector<const char *> custom_fan_modes = {
      Constants::FAN_20, Constants::FAN_40, Constants::FAN_60,
      Constants::FAN_80, Constants::FAN_100
  };
  this->set_supported_custom_fan_modes(custom_fan_modes);
  traits.set_supported_custom_fan_modes(custom_fan_modes);
  if (this->base_.getAutoconfStatus() == dudanov::midea::AUTOCONF_OK) {
    Converters::to_climate_traits(traits, this->base_.getCapabilities());
  }
  if (!traits.get_supported_modes().empty())
    traits.add_supported_mode(ClimateMode::CLIMATE_MODE_OFF);
  if (!traits.get_supported_swing_modes().empty())
    traits.add_supported_swing_mode(ClimateSwingMode::CLIMATE_SWING_OFF);
  if (!traits.get_supported_presets().empty())
    traits.add_supported_preset(ClimatePreset::CLIMATE_PRESET_NONE);
  return traits;
}

void AirConditioner::dump_config() {
  ESP_LOGCONFIG(Constants::TAG,
                "MideaDongle:\n"
                "  [x] Period: %" PRIu32 "ms\n"
                "  [x] Response timeout: %" PRIu32 "ms\n"
                "  [x] Request attempts: %d",
                this->base_.getPeriod(), this->base_.getTimeout(), this->base_.getNumAttempts());
#ifdef USE_REMOTE_TRANSMITTER
  ESP_LOGCONFIG(Constants::TAG, "  [x] Using RemoteTransmitter");
#endif
  if (this->base_.getAutoconfStatus() == dudanov::midea::AUTOCONF_OK) {
    this->base_.getCapabilities().dump();
  } else if (this->base_.getAutoconfStatus() == dudanov::midea::AUTOCONF_ERROR) {
    ESP_LOGW(Constants::TAG,
             "Failed to get 0xB5 capabilities report. Suggest to disable it in config and manually set your "
             "appliance options.");
  }
  this->dump_traits_(Constants::TAG);
}

/* ACTIONS */

void AirConditioner::do_follow_me(float temperature, bool use_fahrenheit, bool beeper) {
#ifdef USE_REMOTE_TRANSMITTER
  if (!std::isfinite(temperature)) {
    ESP_LOGW(Constants::TAG, "Follow me action requires a finite temperature, got: %f", temperature);
    return;
  }
  uint8_t temp_uint8 =
      static_cast<uint8_t>(esphome::clamp<long>(std::lroundf(temperature), 0L, static_cast<long>(UINT8_MAX)));
  char temp_symbol = use_fahrenheit ? 'F' : 'C';
  ESP_LOGD(Constants::TAG, "Follow me action called with temperature: %.5f °%c, rounded to: %u °%c", temperature,
           temp_symbol, temp_uint8, temp_symbol);
  IrFollowMeData data(temp_uint8, use_fahrenheit, beeper);
  this->transmitter_.transmit(data);
#else
  ESP_LOGW(Constants::TAG, "Action needs remote_transmitter component");
#endif
}

void AirConditioner::do_swing_step() {
#ifdef USE_REMOTE_TRANSMITTER
  IrSpecialData data(0x01);
  this->transmitter_.transmit(data);
#else
  ESP_LOGW(Constants::TAG, "Action needs remote_transmitter component");
#endif
}

void AirConditioner::do_display_toggle() {
  // Call displayToggle() directly — it's public in dudanov library.
  // Bypasses ESPHome capability check (Bosch lacks cap 0x0224).
  ESP_LOGD(Constants::TAG, "do_display_toggle: calling base_.displayToggle()");
  this->base_.displayToggle();
  // Trigger immediate full query — reset timer so loop() picks it up on next iteration
  this->last_full_query_ms_ = 0;
}

/* IONIZER / MUTE / TIMER */

bool AirConditioner::get_ionizer_state() const {
  const auto &status = MideaAccessor::get_status(this->base_);
  return status.size() > 9 && (status.data()[9] & 0x20) != 0;
}

bool AirConditioner::get_mute_state() const {
  // Use full status cache (m_status doesn't have byte 14)
  if (!this->last_full_status_.empty() && this->last_full_status_.size() > 14)
    return (this->last_full_status_[14] & 0x70) != 0;
  return false;
}

uint16_t AirConditioner::get_timer_on_minutes() const {
  const auto &status = MideaAccessor::get_status(this->base_);
  if (status.size() <= 4) return 0;
  uint8_t byte_val = status.data()[4];
  return (byte_val == 0x7F) ? 0 : (byte_val - 0x7F) * 15;
}

uint16_t AirConditioner::get_timer_off_minutes() const {
  const auto &status = MideaAccessor::get_status(this->base_);
  if (status.size() <= 5) return 0;
  uint8_t byte_val = status.data()[5];
  return (byte_val == 0x7F) ? 0 : (byte_val - 0x7F) * 15;
}

void AirConditioner::set_ionizer(bool state) {
  ESP_LOGD(Constants::TAG, "set_ionizer: %s", state ? "ON" : "OFF");
  // Build raw 0x40 control frame from current m_status
  const auto &current = MideaAccessor::get_status(this->base_);
  std::vector<uint8_t> bytes(current.data(), current.data() + current.size());
  // Ensure command prefix
  bytes[0] = 0x40;
  // Set ionizer bit: byte 9 bit 5 (0x20)
  bytes[9] = (bytes[9] & ~0x20) | (state ? 0x20 : 0x00);

  FrameData fd(bytes.begin(), bytes.end());
  fd.appendCRC();
  MideaAccessor::queue_raw(this->base_, FrameType::DEVICE_CONTROL, std::move(fd),
      [this](FrameData data) -> ResponseStatus {
        return MideaAccessor::call_read_status(this->base_, data);
      },
      nullptr, nullptr);
}

void AirConditioner::toggle_mute() {
  ESP_LOGD(Constants::TAG, "toggle_mute: calling displayToggle()");
  this->base_.displayToggle();
  // Trigger immediate full query on next loop iteration
  this->last_full_query_ms_ = 0;
}

void AirConditioner::set_timer_on(uint16_t minutes) {
  ESP_LOGD(Constants::TAG, "set_timer_on: %u minutes", minutes);
  const auto &current = MideaAccessor::get_status(this->base_);
  std::vector<uint8_t> bytes(current.data(), current.data() + current.size());
  bytes[0] = 0x40;
  // Timer ON: byte 4. Encoding: 0x7F = no timer, 0x7F + (min/15) = timer value
  bytes[4] = (minutes == 0) ? 0x7F : 0x7F + (minutes / 15);
  // Timer flag: byte 6 bit 4 (0x10)
  if (minutes > 0 || bytes[5] != 0x7F)
    bytes[6] |= 0x10;
  else
    bytes[6] &= ~0x10;

  FrameData fd(bytes.begin(), bytes.end());
  fd.appendCRC();
  MideaAccessor::queue_raw(this->base_, FrameType::DEVICE_CONTROL, std::move(fd),
      [this](FrameData data) -> ResponseStatus {
        return MideaAccessor::call_read_status(this->base_, data);
      },
      nullptr, nullptr);
}

void AirConditioner::set_timer_off(uint16_t minutes) {
  ESP_LOGD(Constants::TAG, "set_timer_off: %u minutes", minutes);
  const auto &current = MideaAccessor::get_status(this->base_);
  std::vector<uint8_t> bytes(current.data(), current.data() + current.size());
  bytes[0] = 0x40;
  // Timer OFF: byte 5
  bytes[5] = (minutes == 0) ? 0x7F : 0x7F + (minutes / 15);
  // Timer flag: byte 6 bit 4 (0x10)
  if (minutes > 0 || bytes[4] != 0x7F)
    bytes[6] |= 0x10;
  else
    bytes[6] &= ~0x10;

  FrameData fd(bytes.begin(), bytes.end());
  fd.appendCRC();
  MideaAccessor::queue_raw(this->base_, FrameType::DEVICE_CONTROL, std::move(fd),
      [this](FrameData data) -> ResponseStatus {
        return MideaAccessor::call_read_status(this->base_, data);
      },
      nullptr, nullptr);
}

/* FAN SPEED */

uint8_t AirConditioner::get_fan_speed() const {
  const auto &status = MideaAccessor::get_status(this->base_);
  if (status.size() <= 3) return 0;
  return status.data()[3];  // Direct percentage 0-100
}

void AirConditioner::set_fan_speed(uint8_t percent) {
  // 0 = Auto (send 102), 1-100 = manual speed
  uint8_t ac_value = (percent == 0) ? 102 : percent;
  ESP_LOGD(Constants::TAG, "set_fan_speed: slider=%u%% → AC byte=%u", percent, ac_value);
  this->fan_speed_write_ms_ = millis();  // Start cooldown — skip poll for 3s
  const auto &current = MideaAccessor::get_status(this->base_);
  std::vector<uint8_t> bytes(current.data(), current.data() + current.size());
  bytes[0] = 0x40;
  bytes[3] = ac_value;  // Fan speed: 102=auto, 1-100=manual

  FrameData fd(bytes.begin(), bytes.end());
  fd.appendCRC();
  MideaAccessor::queue_raw(this->base_, FrameType::DEVICE_CONTROL, std::move(fd),
      [this](FrameData data) -> ResponseStatus {
        return MideaAccessor::call_read_status(this->base_, data);
      },
      nullptr, nullptr);
}

/* FAN MODE MAPPING (percentage-based custom labels) */

const char *AirConditioner::fan_mode_to_string(dudanov::midea::ac::FanMode mode) {
  // FanMode enum values == byte 3 values: FAN_AUTO=102, FAN_SILENT=20, FAN_LOW=40,
  // FAN_MEDIUM=60, FAN_HIGH=80, FAN_TURBO=100. Custom speeds (1-100, non-standard) 
  // cast to FanMode(N) and don't match any enum case.
  switch (mode) {
    case MideaFanMode::FAN_SILENT: return Constants::FAN_20;
    case MideaFanMode::FAN_LOW:   return Constants::FAN_40;
    case MideaFanMode::FAN_MEDIUM: return Constants::FAN_60;
    case MideaFanMode::FAN_HIGH:  return Constants::FAN_80;
    case MideaFanMode::FAN_TURBO: return Constants::FAN_100;
    case MideaFanMode::FAN_AUTO:  return Constants::FAN_AUTO;
    default: {
      // Non-standard speed value (e.g. FanMode(45) for 45%)
      uint8_t raw = static_cast<uint8_t>(mode);
      if (raw >= 1 && raw <= 100) {
        return Constants::FAN_CUSTOM;
      }
      return Constants::FAN_AUTO;
    }
  }
}

dudanov::midea::ac::FanMode AirConditioner::string_to_fan_mode(const std::string &mode) {
  if (mode == Constants::FAN_20)  return MideaFanMode::FAN_SILENT;
  if (mode == Constants::FAN_40)  return MideaFanMode::FAN_LOW;
  if (mode == Constants::FAN_60)  return MideaFanMode::FAN_MEDIUM;
  if (mode == Constants::FAN_80)  return MideaFanMode::FAN_HIGH;
  if (mode == Constants::FAN_100) return MideaFanMode::FAN_TURBO;
  if (mode == Constants::FAN_AUTO) return MideaFanMode::FAN_AUTO;
  // "Custom" or unknown → no change (caller should skip control())
  return MideaFanMode::FAN_AUTO;
}

}  // namespace esphome::midea::ac

#endif  // USE_ARDUINO
