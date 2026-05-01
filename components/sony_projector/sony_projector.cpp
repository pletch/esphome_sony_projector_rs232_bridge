#include "sony_projector.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace sony_projector {

static const char *const TAG = "sony_projector";

void SonyProjector::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Sony Projector (ADCP)...");
  this->flush_input_();
  this->query_model_name_();
}

void SonyProjector::dump_config() {
  ESP_LOGCONFIG(TAG, "Sony Projector (ADCP):");
  LOG_UPDATE_INTERVAL(this);
  this->check_uart_settings(38400, 1, uart::UART_CONFIG_PARITY_EVEN, 8);
  LOG_TEXT_SENSOR("  ", "Status", this->status_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Model Name", this->model_name_text_sensor_);
  LOG_SENSOR("  ", "Light Source Hours", this->light_source_hours_sensor_);
  LOG_SENSOR("  ", "Temperature", this->temperature_sensor_);
  LOG_TEXT_SENSOR("  ", "Filter Status", this->filter_status_text_sensor_);
  LOG_SWITCH("  ", "Power", this->power_switch_);
}

void SonyProjector::flush_input_() {
  uint8_t b;
  while (this->available()) {
    this->read_byte(&b);
  }
}

void SonyProjector::enqueue_(std::string tx, ResponseCallback cb, uint32_t read_timeout_ms) {
  if (this->queue_.size() >= MAX_QUEUE_DEPTH) {
    ESP_LOGW(TAG, "Command queue full (%u), dropping request", (unsigned) this->queue_.size());
    return;
  }
  this->queue_.push_back(PendingCommand{std::move(tx), read_timeout_ms, std::move(cb)});
}

void SonyProjector::start_next_() {
  if (this->queue_.empty() || this->state_ != State::IDLE) {
    return;
  }
  PendingCommand cmd = std::move(this->queue_.front());
  this->queue_.pop_front();

  this->flush_input_();
  this->write_array(reinterpret_cast<const uint8_t *>(cmd.tx.data()), cmd.tx.size());
  this->flush();

  ESP_LOGV(TAG, "TX: %s", cmd.tx.c_str());

  this->state_ = State::WAITING_RESPONSE;
  this->rx_line_.clear();
  this->rx_deadline_ = millis() + cmd.read_timeout_ms;
  this->active_callback_ = std::move(cmd.callback);
}

void SonyProjector::finish_(bool success, const std::string &response) {
  ResponseCallback cb = std::move(this->active_callback_);
  this->state_ = State::IDLE;
  this->rx_line_.clear();
  if (cb) {
    cb(success, response);
  }
}

void SonyProjector::loop() {
  if (this->state_ == State::WAITING_RESPONSE) {
    while (this->available()) {
      uint8_t b;
      if (!this->read_byte(&b)) {
        break;
      }
      // Line terminator: CR+LF. Treat lone LF as terminator too, ignore stray CR.
      if (b == '\r') {
        continue;
      }
      if (b == '\n') {
        std::string line = this->rx_line_;
        ESP_LOGV(TAG, "RX: %s", line.c_str());
        this->finish_(true, line);
        break;
      }
      if (this->rx_line_.size() < MAX_LINE_LEN) {
        this->rx_line_.push_back(static_cast<char>(b));
      }
    }

    if (this->state_ == State::WAITING_RESPONSE &&
        (int32_t) (millis() - this->rx_deadline_) >= 0) {
      ESP_LOGW(TAG, "Response timeout (partial: '%s')", this->rx_line_.c_str());
      this->finish_(false, std::string{});
    }
  }

  if (this->state_ == State::IDLE && !this->queue_.empty()) {
    this->start_next_();
  }
}

void SonyProjector::send_command(const std::string &command, ResponseCallback cb) {
  std::string tx = command;
  tx.append("\r\n");
  this->enqueue_(std::move(tx), std::move(cb));
}

void SonyProjector::send_raw(const std::vector<uint8_t> &bytes) {
  if (bytes.empty()) {
    ESP_LOGW(TAG, "send_raw called with empty payload");
    return;
  }
  ESP_LOGI(TAG, "send_raw (%u bytes)", (unsigned) bytes.size());
  this->enqueue_(std::string(bytes.begin(), bytes.end()), nullptr);
}

void SonyProjector::update() {
  this->poll_status_();
  if (this->input_select_ != nullptr) {
    this->poll_input_();
  }

  // Slow metrics: only useful while fully on. During startup the projector
  // returns err_inactive ("temporarily invalidated"); in standby it returns
  // err_cmd or doesn't respond at all (manual §4-2-2). Neither produces a
  // real reading, and an err response during startup must NOT be interpreted
  // as "unsupported".
  if (!this->is_fully_on_ || !this->slow_poll_due_()) {
    return;
  }
  this->last_slow_poll_ = millis();

  if (this->light_source_hours_sensor_ != nullptr && this->light_source_hours_supported_) {
    this->poll_light_source_hours_();
  }
  if (this->temperature_sensor_ != nullptr && this->temperature_supported_) {
    this->poll_temperature_();
  }
  if (this->filter_status_text_sensor_ != nullptr && this->filter_status_supported_) {
    this->poll_filter_status_();
  }
}

bool SonyProjector::slow_poll_due_() const {
  if (this->last_slow_poll_ == 0) {
    return true;  // first run after boot
  }
  return (int32_t) (millis() - this->last_slow_poll_) >= (int32_t) SLOW_POLL_INTERVAL_MS;
}

void SonyProjector::poll_status_() {
  this->send_command("power_status ?",
                     [this](bool ok, const std::string &resp) {
                       this->handle_status_response_(ok, resp);
                     });
}

void SonyProjector::poll_light_source_hours_() {
  this->send_command("timer ?",
                     [this](bool ok, const std::string &resp) {
                       this->handle_light_source_hours_response_(ok, resp);
                     });
}

void SonyProjector::poll_temperature_() {
  this->send_command("temperature ?",
                     [this](bool ok, const std::string &resp) {
                       this->handle_temperature_response_(ok, resp);
                     });
}

void SonyProjector::poll_filter_status_() {
  this->send_command("filter_status ?",
                     [this](bool ok, const std::string &resp) {
                       this->handle_filter_status_response_(ok, resp);
                     });
}

void SonyProjector::poll_input_() {
  this->send_command("input ?",
                     [this](bool ok, const std::string &resp) {
                       this->handle_input_response_(ok, resp);
                     });
}

void SonyProjector::handle_input_response_(bool success, const std::string &response) {
  if (!success || is_error_response_(response)) {
    return;
  }
  const std::string val = unquote_(response);
  if (this->input_select_ == nullptr) {
    return;
  }
  // Only publish if the value is one of our configured options; otherwise the
  // Select component will reject and log. This keeps HA in sync without
  // requiring the user to list every possible ADCP input id.
  const auto &opts = this->input_select_->traits.get_options();
  for (const auto &o : opts) {
    if (o == val) {
      this->input_select_->publish_state(val);
      return;
    }
  }
  ESP_LOGD(TAG, "Current input '%s' is not in configured select options", val.c_str());
}

void SonyProjector::handle_status_response_(bool success, const std::string &response) {
  if (!success || is_error_response_(response)) {
    if (this->status_text_sensor_ != nullptr) {
      this->status_text_sensor_->publish_state(success ? response : std::string{"No Response"});
    }
    return;
  }

  // power_status? returns one of: "standby" "startup" "on" "cooling1" "cooling2"
  const std::string val = unquote_(response);
  const char *state_str = "Unknown";
  bool is_on = false;

  if (val == "standby") {
    state_str = "Standby";
  } else if (val == "startup") {
    state_str = "Starting";
    is_on = true;
  } else if (val == "on") {
    state_str = "On";
    is_on = true;
  } else if (val == "cooling1" || val == "cooling2") {
    state_str = "Cooling";
  } else {
    ESP_LOGW(TAG, "Unknown power_status: '%s'", val.c_str());
  }

  ESP_LOGD(TAG, "Status: %s", state_str);
  if (this->status_text_sensor_ != nullptr) {
    this->status_text_sensor_->publish_state(state_str);
  }
  if (this->power_switch_ != nullptr) {
    this->power_switch_->publish_state(is_on);
  }

  const bool is_fully_on = (val == "on");

  // Fire slow polls as soon as we first observe the projector fully on for
  // this power cycle — covers both startup→on transitions and the case where
  // the projector was already on at boot. We deliberately wait for "on" (not
  // "startup") because during the warm-up window the projector replies
  // err_inactive to metric queries (manual §4-2-2). last_slow_poll_=0 means
  // "no slow poll has happened in this on cycle yet".
  if (is_fully_on && this->last_slow_poll_ == 0) {
    ESP_LOGD(TAG, "Projector fully on; refreshing slow metrics now");
    this->last_slow_poll_ = millis();
    if (this->light_source_hours_sensor_ != nullptr && this->light_source_hours_supported_) {
      this->poll_light_source_hours_();
    }
    if (this->temperature_sensor_ != nullptr && this->temperature_supported_) {
      this->poll_temperature_();
    }
    if (this->filter_status_text_sensor_ != nullptr && this->filter_status_supported_) {
      this->poll_filter_status_();
    }
  }
  // On transition out of fully-on, clear values that don't make sense and
  // reset the slow-poll trigger so the next on refreshes metrics promptly.
  // Lamp hours and filter state are stable across power cycles so we leave
  // them at their last reading.
  if (!is_fully_on && this->is_fully_on_) {
    this->last_slow_poll_ = 0;
    if (this->temperature_sensor_ != nullptr && this->temperature_supported_) {
      this->temperature_sensor_->publish_state(NAN);
    }
  }
  this->is_powered_on_ = is_on;
  this->is_fully_on_ = is_fully_on;
}

void SonyProjector::handle_light_source_hours_response_(bool success, const std::string &response) {
  if (!success) {
    return;
  }
  if (is_error_response_(response)) {
    if (!this->is_fully_on_ || response == "err_inactive") {
      ESP_LOGD(TAG, "timer ? returned %s; will retry later", response.c_str());
      return;
    }
    ESP_LOGI(TAG, "timer ? not supported on %s (%s); disabling light_source_hours polling",
             this->model_name_.empty() ? "this model" : this->model_name_.c_str(),
             response.c_str());
    this->light_source_hours_supported_ = false;
    this->light_source_hours_sensor_->publish_state(NAN);
    return;
  }
  // timer? returns e.g. [{"operation":3400},{"light_src":2300},{"prev_light_src":3000}]
  // Prefer light_src (laser/lamp on-time), fall back to operation.
  int32_t hours = 0;
  if (extract_json_int_(response, "light_src", hours) ||
      extract_json_int_(response, "operation", hours)) {
    ESP_LOGD(TAG, "Light source hours: %ld", (long) hours);
    this->light_source_hours_sensor_->publish_state(static_cast<float>(hours));
  } else {
    ESP_LOGW(TAG, "Could not parse timer response: %s", response.c_str());
  }
}

void SonyProjector::handle_temperature_response_(bool success, const std::string &response) {
  if (!success) {
    return;
  }
  if (is_error_response_(response)) {
    if (!this->is_fully_on_ || response == "err_inactive") {
      ESP_LOGD(TAG, "temperature ? returned %s; will retry later", response.c_str());
      return;
    }
    ESP_LOGI(TAG, "temperature ? not supported on %s (%s); disabling temperature polling",
             this->model_name_.empty() ? "this model" : this->model_name_.c_str(),
             response.c_str());
    this->temperature_supported_ = false;
    this->temperature_sensor_->publish_state(NAN);
    return;
  }
  float value = 0.0f;
  // Returns JSON like [{"intake_air":23.0}]
  if (!extract_json_float_(response, "intake_air", value)) {
    char *end = nullptr;
    value = std::strtof(response.c_str(), &end);
    if (end == response.c_str()) {
      ESP_LOGW(TAG, "Could not parse temperature: %s", response.c_str());
      return;
    }
  }
  ESP_LOGD(TAG, "Temperature: %.1f", value);
  this->temperature_sensor_->publish_state(value);
}

void SonyProjector::handle_filter_status_response_(bool success, const std::string &response) {
  if (!success) {
    return;
  }
  if (is_error_response_(response)) {
    if (!this->is_fully_on_ || response == "err_inactive") {
      ESP_LOGD(TAG, "filter_status ? returned %s; will retry later", response.c_str());
      return;
    }
    ESP_LOGI(TAG, "filter_status ? not supported on %s (%s); disabling filter_status polling",
             this->model_name_.empty() ? "this model" : this->model_name_.c_str(),
             response.c_str());
    this->filter_status_supported_ = false;
    if (this->filter_status_text_sensor_ != nullptr) {
      this->filter_status_text_sensor_->publish_state("Not Supported");
    }
    return;
  }
  const std::string val = unquote_(response);
  const char *out = "Unknown";
  if (val == "normal") {
    out = "Normal";
  } else if (val == "clean") {
    out = "Clean Required";
  } else if (val == "replace") {
    out = "Replace Required";
  } else {
    ESP_LOGW(TAG, "Unknown filter_status: '%s'", val.c_str());
  }
  ESP_LOGD(TAG, "Filter status: %s", out);
  this->filter_status_text_sensor_->publish_state(out);
}

void SonyProjector::query_model_name_() {
  this->send_command("modelname ?",
                     [this](bool ok, const std::string &resp) {
                       this->handle_model_name_response_(ok, resp);
                     });
}

void SonyProjector::handle_model_name_response_(bool success, const std::string &response) {
  if (!success || is_error_response_(response)) {
    ESP_LOGW(TAG, "Could not read model name (%s)",
             success ? response.c_str() : "no response");
    return;
  }
  this->model_name_ = unquote_(response);
  ESP_LOGI(TAG, "Projector model: %s", this->model_name_.c_str());
  if (this->model_name_text_sensor_ != nullptr) {
    this->model_name_text_sensor_->publish_state(this->model_name_);
  }
}

void SonyProjector::set_input(const std::string &name) {
  // Quote the value per ADCP string-set syntax: input "hdmi1"
  std::string cmd = "input \"";
  cmd.append(name);
  cmd.append("\"");
  ESP_LOGI(TAG, "Set input: %s", name.c_str());
  this->send_command(cmd, [name](bool ok, const std::string &resp) {
    if (!ok) {
      ESP_LOGW(TAG, "Set input %s: no response", name.c_str());
    } else if (is_error_response_(resp)) {
      ESP_LOGW(TAG, "Set input %s rejected: %s", name.c_str(), resp.c_str());
    }
  });
}

void SonyProjector::request_power(bool on) {
  ESP_LOGI(TAG, "Power %s requested", on ? "ON" : "OFF");
  const std::string cmd = on ? "power \"on\"" : "power \"off\"";
  this->send_command(cmd, [this, on](bool ok, const std::string &resp) {
    if (!ok) {
      ESP_LOGW(TAG, "Power %s: no response", on ? "ON" : "OFF");
    } else if (is_error_response_(resp)) {
      // err_cmd in standby is documented for some models; one retry.
      ESP_LOGW(TAG, "Power %s returned %s, retrying once", on ? "ON" : "OFF", resp.c_str());
      const std::string retry = on ? "power \"on\"" : "power \"off\"";
      this->send_command(retry, nullptr);
    } else {
      ESP_LOGD(TAG, "Power %s ack: %s", on ? "ON" : "OFF", resp.c_str());
    }
  });

  // Re-poll after the unit settles so HA reflects reality.
  this->set_timeout("post_power_poll", on ? 1500 : 1500, [this]() { this->poll_status_(); });
}

bool SonyProjector::is_error_response_(const std::string &resp) {
  return resp.compare(0, 4, "err_") == 0;
}

std::string SonyProjector::unquote_(const std::string &resp) {
  if (resp.size() >= 2 && resp.front() == '"' && resp.back() == '"') {
    return resp.substr(1, resp.size() - 2);
  }
  return resp;
}

bool SonyProjector::extract_json_int_(const std::string &blob, const char *key, int32_t &out) {
  // Find "key": and parse the integer that follows. Whitespace tolerant.
  std::string needle = std::string{"\""} + key + "\"";
  size_t pos = blob.find(needle);
  if (pos == std::string::npos) {
    return false;
  }
  pos += needle.size();
  // Skip whitespace and ':'
  while (pos < blob.size() && (std::isspace(static_cast<unsigned char>(blob[pos])) || blob[pos] == ':')) {
    pos++;
  }
  if (pos >= blob.size()) {
    return false;
  }
  char *end = nullptr;
  long val = std::strtol(blob.c_str() + pos, &end, 10);
  if (end == blob.c_str() + pos) {
    return false;
  }
  out = static_cast<int32_t>(val);
  return true;
}

bool SonyProjector::extract_json_float_(const std::string &blob, const char *key, float &out) {
  // Find "key": and parse the float that follows. Whitespace tolerant.
  std::string needle = std::string{"\""} + key + "\"";
  size_t pos = blob.find(needle);
  if (pos == std::string::npos) {
    return false;
  }
  pos += needle.size();
  // Skip whitespace and ':'
  while (pos < blob.size() && (std::isspace(static_cast<unsigned char>(blob[pos])) || blob[pos] == ':')) {
    pos++;
  }
  if (pos >= blob.size()) {
    return false;
  }
  char *end = nullptr;
  float val = std::strtof(blob.c_str() + pos, &end);
  if (end == blob.c_str() + pos) {
    return false;
  }
  out = val;
  return true;
}

void SonyProjectorPowerSwitch::write_state(bool state) {
  if (this->parent_ != nullptr) {
    this->parent_->request_power(state);
  }
  this->publish_state(state);
}

void SonyProjectorInputSelect::control(const std::string &value) {
  if (this->parent_ != nullptr) {
    this->parent_->set_input(value);
  }
  // Optimistic update; the next `input ?` poll will reconcile if the
  // projector rejected the change.
  this->publish_state(value);
}

}  // namespace sony_projector
}  // namespace esphome
