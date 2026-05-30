#pragma once

#ifdef USE_ARDUINO

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "air_conditioner.h"

namespace esphome::midea::ac {

/**
 * Timer number entity (ON or OFF timer).
 * Reads/writes 0xC0 status bytes 4-5.
 * Encoding: byte = 0x7F + floor(minutes / 15). 0x7F = no timer.
 * Resolution: 15 minutes. Range: 0–1440 minutes (24h).
 */
class TimerNumber : public number::Number, public Component {
 public:
  void set_parent(AirConditioner *parent) { this->parent_ = parent; }
  void set_is_on_timer(bool is_on) { this->is_on_timer_ = is_on; }

 protected:
  void control(float value) override {
    uint16_t minutes = static_cast<uint16_t>(value);
    if (this->is_on_timer_)
      this->parent_->set_timer_on(minutes);
    else
      this->parent_->set_timer_off(minutes);
  }

  AirConditioner *parent_;
  bool is_on_timer_;
};

}  // namespace esphome::midea::ac

#endif  // USE_ARDUINO
