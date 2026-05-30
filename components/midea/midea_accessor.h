#pragma once

#ifdef USE_ARDUINO

// MideaUART library headers
#include <Appliance/AirConditioner/AirConditioner.h>
#include <functional>

namespace esphome::midea::ac {

/**
 * Accessor for dudanov::midea::ac::AirConditioner protected members.
 * static_cast is safe — adds zero data members.
 */
class MideaAccessor : public dudanov::midea::ac::AirConditioner {
 public:
  /** Read m_status (truncated to bytes 1-10 via copyStatus). */
  static const dudanov::midea::ac::StatusData &get_status(
      const dudanov::midea::ac::AirConditioner &ac) {
    return static_cast<const MideaAccessor &>(ac).m_status;
  }

  /** Queue a raw request via m_queueRequestPriority. */
  static void queue_raw(dudanov::midea::ac::AirConditioner &ac,
                        dudanov::midea::FrameType type,
                        dudanov::midea::FrameData data,
                        std::function<dudanov::midea::ResponseStatus(
                            dudanov::midea::FrameData)> on_data,
                        std::function<void()> on_success = nullptr,
                        std::function<void()> on_error = nullptr) {
    static_cast<MideaAccessor &>(ac).m_queueRequestPriority(
        type, std::move(data), on_data, on_success, on_error);
  }

  /** Call m_readStatus for standard response handling (updates m_status). */
  static dudanov::midea::ResponseStatus call_read_status(
      dudanov::midea::ac::AirConditioner &ac,
      const dudanov::midea::FrameData &data) {
    return static_cast<MideaAccessor &>(ac).m_readStatus(data);
  }
};

}  // namespace esphome::midea::ac

#endif  // USE_ARDUINO
