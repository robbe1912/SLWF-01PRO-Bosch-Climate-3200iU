#pragma once

#ifdef USE_ARDUINO

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "air_conditioner.h"

namespace esphome::midea::ac {

/**
 * Ionizer (Anion) switch entity.
 * Reads/writes 0xC0 status byte 9 bit 5 (0x20).
 * Capability: 0x021E (anion) — confirmed supported on Bosch 3200iU.
 * Optimistic: publishes desired state immediately, poll verifies.
 */
class IonizerSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(AirConditioner *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override {
    this->publish_state(state);  // Optimistic — instant HA feedback
    this->parent_->set_ionizer(state);
  }

  AirConditioner *parent_;
};

/**
 * Display Mute switch entity.
 * Toggle-only (protocol limitation): 0x41 0x61 DisplayToggleData.
 * Reads state from 0xC0 status byte 14 mask 0x70.
 * Bypasses ESPHome capability check (Bosch lacks 0x0224).
 * Optimistic: publishes desired state immediately, poll verifies.
 */
class MuteSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(AirConditioner *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override {
    // Toggle-only: if current state matches desired, do nothing
    if (this->parent_->get_mute_state() == state)
      return;
    this->publish_state(state);  // Optimistic — instant HA feedback
    this->parent_->toggle_mute();
  }

  AirConditioner *parent_;
};

}  // namespace esphome::midea::ac

#endif  // USE_ARDUINO
