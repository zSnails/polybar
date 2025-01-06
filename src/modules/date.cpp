#include "modules/date.hpp"
#include <chrono>
#include <iomanip>
#include <thread>

#include "components/builder.hpp"
#include "drawtypes/animation.hpp"
#include "drawtypes/label.hpp"
#include "modules/meta/base.inl"
#include "utils/action_router.hpp"
#include "modules/meta/timer_module.hpp"

POLYBAR_NS

namespace modules {
  template class module<date_module>;

  date_module::date_module(const bar_settings& bar, string name_, const config& config)
      : timer_module<date_module>(bar, move(name_), config) {
    if (!m_bar.locale.empty()) {
      datetime_stream.imbue(std::locale(m_bar.locale.c_str()));
    }

    m_router->register_action(EVENT_TOGGLE, [this]() { action_toggle(); });

    m_dateformat = m_conf.get(name(), "date", ""s);
    m_dateformat_alt = m_conf.get(name(), "date-alt", ""s);
    m_timeformat = m_conf.get(name(), "time", ""s);
    m_timeformat_alt = m_conf.get(name(), "time-alt", ""s);

    if (m_dateformat.empty() && m_timeformat.empty()) {
      throw module_error("No date or time format specified");
    }

    set_interval(1s);

    m_formatter->add(DEFAULT_FORMAT, TAG_LABEL, {TAG_LABEL, TAG_DATE, TAG_ANIMATION_CLOCK});

    if (m_formatter->has(TAG_ANIMATION_CLOCK)) {
      m_animation_clock = load_animation(m_conf, name(), TAG_ANIMATION_CLOCK);
    }
    if (m_formatter->has(TAG_DATE)) {
      m_log.warn("%s: The format tag `<date>` is deprecated, use `<label>` instead.", name());

      m_formatter->get(DEFAULT_FORMAT)->value =
          string_util::replace_all(m_formatter->get(DEFAULT_FORMAT)->value, TAG_DATE, TAG_LABEL);
    }

    if (m_formatter->has(TAG_LABEL)) {
      m_label = load_optional_label(m_conf, name(), "label", "%date%");
    }
  }

  bool date_module::update() {
    auto time = std::time(nullptr);

    auto date_format = m_toggled ? m_dateformat_alt : m_dateformat;
    // Clear stream contents
    datetime_stream.str("");
    datetime_stream << std::put_time(localtime(&time), date_format.c_str());
    auto date_string = datetime_stream.str();

    auto time_format = m_toggled ? m_timeformat_alt : m_timeformat;
    // Clear stream contents
    datetime_stream.str("");
    datetime_stream << std::put_time(localtime(&time), time_format.c_str());
    auto time_string = datetime_stream.str();

    if (m_date == date_string && m_time == time_string) {
      return false;
    }

    m_date = date_string;
    m_time = time_string;

    if (m_label) {
      m_label->reset_tokens();
      m_label->replace_token("%date%", m_date);
      m_label->replace_token("%time%", m_time);
    }

    return true;
  }

  /**
   * Subthread runner that emits update events to refresh <animation-clock>
   * or in case it is used.
   */
  void date_module::subthread() {
    m_log.trace("%s: Start of subthread", name());

    while (running()) {
      auto now = chrono::steady_clock::now();
      auto framerate = 1000U; // milliseconds
      if (m_animation_clock) {
        m_animation_clock->increment();
        broadcast();
        framerate = m_animation_clock->framerate();
      }

      now += chrono::milliseconds(framerate);
      this_thread::sleep_until(now);
    }

    m_log.trace("%s: End of subthread", name());
  }

  /**
   * Dispatch the subthread used to update the
   * clock animation when the module is started
   */
  void date_module::start() {
    this->timer_module::start();

    // We only start animation thread if there is at least one animation.
    if (m_animation_clock) {
      m_subthread = thread(&date_module::subthread, this);
    }
  }

  /**
   * Release wake lock when stopping the module
   */
  void date_module::teardown() {
    if (m_subthread.joinable()) {
      m_subthread.join();
    }
  }

  bool date_module::build(builder* builder, const string& tag) const {
    if (tag == TAG_ANIMATION_CLOCK) {
      builder->node(m_animation_clock->get());
    } else if (tag == TAG_LABEL) {
      if (!m_dateformat_alt.empty() || !m_timeformat_alt.empty()) {
        builder->action(mousebtn::LEFT, *this, EVENT_TOGGLE, "", m_label);
      } else {
        builder->node(m_label);
      }
    } else {
      return false;
    }

    return true;
  }

  void date_module::action_toggle() {
    m_toggled = !m_toggled;
    wakeup();
  }
}  // namespace modules

POLYBAR_NS_END
