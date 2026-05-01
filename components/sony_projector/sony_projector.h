#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace sony_projector {

// ADCP (Advanced Display Control Protocol), serial transport.
// US-ASCII text commands, terminated with CR+LF. Responses are also CR+LF
// terminated and are one of:
//   ok                          -> success (SET / action)
//   err_cmd / err_val / ...     -> error
//   "value"                     -> quoted string (GET on string-typed cmd)
//   123                         -> bare number (GET on numeric cmd)
//   [..] or {..}                -> JSON array/object (e.g. timer?)

class SonyProjector;

class SonyProjectorPowerSwitch : public switch_::Switch {
 public:
  void set_parent(SonyProjector *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override;
  SonyProjector *parent_{nullptr};
};

class SonyProjectorInputSelect : public select::Select {
 public:
  void set_parent(SonyProjector *parent) { this->parent_ = parent; }

 protected:
  void control(const std::string &value) override;
  SonyProjector *parent_{nullptr};
};

class SonyProjector : public PollingComponent, public uart::UARTDevice {
 public:
  // Called with the trimmed response line (without CR+LF). On timeout, success
  // is false and response is empty.
  using ResponseCallback = std::function<void(bool success, const std::string &response)>;

  void set_power_switch(SonyProjectorPowerSwitch *sw) { this->power_switch_ = sw; }
  void set_status_text_sensor(text_sensor::TextSensor *ts) { this->status_text_sensor_ = ts; }
  void set_light_source_hours_sensor(sensor::Sensor *s) { this->light_source_hours_sensor_ = s; }
  void set_temperature_sensor(sensor::Sensor *s) { this->temperature_sensor_ = s; }
  void set_filter_status_text_sensor(text_sensor::TextSensor *ts) { this->filter_status_text_sensor_ = ts; }
  void set_model_name_text_sensor(text_sensor::TextSensor *ts) { this->model_name_text_sensor_ = ts; }
  void set_input_select(SonyProjectorInputSelect *sel) { this->input_select_ = sel; }

  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Public API
  void request_power(bool on);
  // Set the active input. `name` is the ADCP input id ("hdmi1", "hdmi2", etc.).
  void set_input(const std::string &name);
  // Send an arbitrary ADCP command. CR+LF is appended automatically.
  void send_command(const std::string &command, ResponseCallback cb = nullptr);
  // Escape hatch: send raw bytes verbatim (no terminator added).
  void send_raw(const std::vector<uint8_t> &bytes);

 protected:
  static constexpr uint32_t DEFAULT_READ_TIMEOUT_MS = 1500;
  static constexpr size_t MAX_QUEUE_DEPTH = 8;
  static constexpr size_t MAX_LINE_LEN = 512;
  // Slow-changing metrics (lamp/laser hours, temperature, filter status) only
  // poll on this cadence regardless of the component's update_interval, which
  // governs power-status polling.
  static constexpr uint32_t SLOW_POLL_INTERVAL_MS = 15UL * 60UL * 1000UL;

  enum class State : uint8_t {
    IDLE,
    WAITING_RESPONSE,
  };

  struct PendingCommand {
    std::string tx;  // already includes CR+LF if applicable
    uint32_t read_timeout_ms;
    ResponseCallback callback;
  };

  void flush_input_();
  void enqueue_(std::string tx, ResponseCallback cb,
                uint32_t read_timeout_ms = DEFAULT_READ_TIMEOUT_MS);
  void start_next_();
  void finish_(bool success, const std::string &response);

  void poll_status_();
  void poll_light_source_hours_();
  void poll_temperature_();
  void poll_filter_status_();
  void poll_input_();
  void query_model_name_();
  void handle_status_response_(bool success, const std::string &response);
  void handle_light_source_hours_response_(bool success, const std::string &response);
  void handle_temperature_response_(bool success, const std::string &response);
  void handle_filter_status_response_(bool success, const std::string &response);
  void handle_input_response_(bool success, const std::string &response);
  void handle_model_name_response_(bool success, const std::string &response);

  // Helpers
  static bool is_error_response_(const std::string &resp);
  static std::string unquote_(const std::string &resp);
  // Extract integer value of `key` from a JSON-ish blob like
  // [{"operation":3400},{"light_src":2300}]. Returns false if not found.
  static bool extract_json_int_(const std::string &blob, const char *key, int32_t &out);
  // Extract float value of `key` from a JSON-ish blob. Returns false if not found.
  static bool extract_json_float_(const std::string &blob, const char *key, float &out);

  text_sensor::TextSensor *status_text_sensor_{nullptr};
  sensor::Sensor *light_source_hours_sensor_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};
  text_sensor::TextSensor *filter_status_text_sensor_{nullptr};
  text_sensor::TextSensor *model_name_text_sensor_{nullptr};
  SonyProjectorPowerSwitch *power_switch_{nullptr};
  SonyProjectorInputSelect *input_select_{nullptr};

  // Capability flags. Start true; if the projector returns err_* on the first
  // probe, the relevant flag is cleared and polling stops.
  bool light_source_hours_supported_{true};
  bool temperature_supported_{true};
  bool filter_status_supported_{true};
  std::string model_name_;
  uint32_t last_slow_poll_{0};
  bool is_powered_on_{false};   // includes startup; drives the HA switch state
  bool is_fully_on_{false};     // only true while power_status == "on"
  bool slow_poll_due_() const;

  std::deque<PendingCommand> queue_;
  State state_{State::IDLE};
  std::string rx_line_;
  uint32_t rx_deadline_{0};
  ResponseCallback active_callback_{};
};

}  // namespace sony_projector
}  // namespace esphome
