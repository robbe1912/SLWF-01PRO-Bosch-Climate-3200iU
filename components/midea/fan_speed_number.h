#pragma once

#ifdef USE_ARDUINO

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "air_conditioner.h"

namespace esphome::midea::ac {

/**
 * Fan speed number entity.
 * Reads/writes 0xC0 status byte 3 (direct percentage 0-100).
 * Optimistic: publishes desired state immediately, poll verifies.
 */
class FanSpeedNumber : public number::Number, public Component {
 public:
  void set_parent(AirConditioner *parent) { this->parent_ = parent; }

 protected:
  void control(float value) override {
    this->publish_state(value);  // Optimistic — instant HA feedback
    this->parent_->set_fan_speed(static_cast<uint8_t>(value));
  }

  AirConditioner *parent_;
};

}  // namespace esphome::midea::ac

#endif  // USE_ARDUINO
