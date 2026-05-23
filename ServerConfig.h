#pragma once

#include "Logging.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace web {

struct ServerConfig {
  std::string host {"0.0.0.0"};
  std::string port {"6969"};
  std::uint32_t listen_backlog {128};

  std::uint64_t max_body_bytes {static_cast<std::uint64_t> (1024) * 1024};
  std::size_t max_header_bytes {5 * 1024};
  std::chrono::seconds request_read_timeout {30};
  /// Applied before each response `async_write` on the connection stream.
  std::chrono::seconds response_write_timeout {30};

  /// Maximum concurrent `HttpSession` instances (`0` = unlimited). When
  /// exceeded, new connections receive HTTP 503 and are closed.
  std::size_t max_active_sessions {0};

  /// Empty `log_fn` selects `default_stderr_logger()` in `HttpServer`.
  LogFn log_fn {};
};

inline void validate_server_config (const ServerConfig& config) {
  if (config.host.empty () || config.port.empty ()) {
    throw std::invalid_argument (
        "ServerConfig: host and port must be non-empty strings");
  }
  if (config.listen_backlog < 1u || config.listen_backlog > 65535u) {
    throw std::invalid_argument (
        "ServerConfig: listen_backlog must be in the range [1, 65535]");
  }
  if (config.max_body_bytes == 0u) {
    throw std::invalid_argument (
        "ServerConfig: max_body_bytes must be greater than zero");
  }
  if (config.max_header_bytes == 0u) {
    throw std::invalid_argument (
        "ServerConfig: max_header_bytes must be greater than zero");
  }
  if (config.request_read_timeout.count () <= 0) {
    throw std::invalid_argument (
        "ServerConfig: request_read_timeout must be positive");
  }
  if (config.response_write_timeout.count () <= 0) {
    throw std::invalid_argument (
        "ServerConfig: response_write_timeout must be positive");
  }
}

}  // namespace web
