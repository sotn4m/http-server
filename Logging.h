#pragma once

#include <functional>
#include <iostream>
#include <print>
#include <string_view>

namespace web {

enum class LogLevel { Error, Warn, Info };

using LogFn = std::function<void (LogLevel level, std::string_view message)>;

[[nodiscard]] inline const char* to_string (LogLevel level) noexcept {
  switch (level) {
    case LogLevel::Error:
      return "error";
    case LogLevel::Warn:
      return "warn";
    case LogLevel::Info:
      return "info";
  }
  return "unknown";
}

/// Logs to stderr (errors) or stdout (other levels). Suitable as default.
[[nodiscard]] inline const LogFn& default_stderr_logger () {
  static const LogFn logger = [] (LogLevel level, std::string_view message) {
    auto& stream = level == LogLevel::Error ? std::cerr : std::cout;
    std::println (stream, "[{}] {}", to_string (level), message);
  };
  return logger;
}

/// No-op logger for tests or embedded use (non-empty so `resolve_log_fn` keeps
/// it).
[[nodiscard]] inline const LogFn& null_logger () {
  static const LogFn logger = [] (LogLevel, std::string_view) {};
  return logger;
}

[[nodiscard]] inline LogFn resolve_log_fn (const LogFn& configured) {
  if (configured) {
    return configured;
  }
  return default_stderr_logger ();
}

inline void log_message (const LogFn& log,
                         LogLevel level,
                         std::string_view message) {
  if (log) {
    log (level, message);
  }
}

}  // namespace web
