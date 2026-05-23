#include "HttpSession.h"

#include <atomic>
#include <chrono>
#include <format>
#include <functional>
#include <random>
#include <thread>

namespace web {

namespace {

std::string random_hex (std::size_t byte_count) {
  // Use random_device when available, with a deterministic fallback seed for
  // platforms where random_device is slow, throws, or has poor entropy.
  thread_local std::mt19937 generator = [] {
    try {
      return std::mt19937 {std::random_device {}()};
    } catch (...) {
      const auto time_seed = static_cast<unsigned> (
          std::chrono::steady_clock::now ().time_since_epoch ().count ());
      const auto thread_seed = static_cast<unsigned> (
          std::hash<std::thread::id> {}(std::this_thread::get_id ()));
      return std::mt19937 {time_seed ^ (thread_seed << 1)};
    }
  }();
  std::uniform_int_distribution<unsigned> distribution (0u, 255u);
  constexpr char kHexDigits[] = "0123456789abcdef";

  std::string output;
  output.reserve (byte_count * 2);
  for (std::size_t index = 0; index < byte_count; ++index) {
    const auto value = distribution (generator);
    output.push_back (kHexDigits[(value >> 4) & 0x0F]);
    output.push_back (kHexDigits[value & 0x0F]);
  }
  return output;
}

std::string make_request_id () {
  return random_hex (8);
}

std::string format_remote_endpoint (const tcp::socket& socket) {
  boost::system::error_code error_code;
  const auto endpoint = socket.remote_endpoint (error_code);
  if (error_code) {
    return "unknown";
  }
  return std::format ("{}:{}", endpoint.address ().to_string (),
                      endpoint.port ());
}

}  // namespace

HttpSession::HttpSession (tcp::socket&& socket,
                          RequestHandler requestHandler,
                          LogFn log_fn,
                          std::shared_ptr<SessionState> session_state,
                          SessionConfig config)
    : stream_ {std::move (socket)},
      requestHandler_ {std::move (requestHandler)},
      log_fn_ {std::move (log_fn)},
      session_state_ {std::move (session_state)},
      context_ {.request_id = make_request_id (),
                .remote_address = format_remote_endpoint (stream_.socket ())},
      config_ {config} {}

HttpSession::~HttpSession () {
  session_state_->active_sessions.fetch_sub (1, std::memory_order_relaxed);
  session_state_->condition.notify_all ();
}

void HttpSession::run () {
  net::dispatch (
      stream_.get_executor (),
      beast::bind_front_handler (&HttpSession::read, shared_from_this ()));
}

void HttpSession::read () {
  requestParser_.emplace ();
  requestParser_->body_limit (config_.max_body_bytes);
  requestParser_->header_limit (config_.max_header_bytes);
  buffer_.clear ();
  buffer_.shrink_to_fit ();

  stream_.expires_after (config_.request_read_timeout);
  http::async_read (
      stream_, buffer_, *requestParser_,
      beast::bind_front_handler (&HttpSession::onRead, shared_from_this ()));
}

void HttpSession::onRead (beast::error_code ec, std::size_t) {
  if (ec == http::error::end_of_stream) {
    close ();
    return;
  }

  if (ec == http::error::body_limit) {
    log_message (
        log_fn_, LogLevel::Warn,
        std::format ("Request body limit exceeded: {}", ec.message ()));
    sendError (http::status::payload_too_large, "Maximum body size exceeded.");
    return;
  }
  if (ec == http::error::header_limit) {
    log_message (
        log_fn_, LogLevel::Warn,
        std::format ("Request header limit exceeded: {}", ec.message ()));
    sendError (http::status::request_header_fields_too_large,
               "Maximum header size exceeded.");
    return;
  }

  if (ec) {
    log_message (log_fn_, LogLevel::Warn,
                 std::format ("Error reading HTTP request: {}", ec.message ()));
    close ();
    return;
  }

  current_request_version_ = requestParser_->get ().version ();
  auto self = shared_from_this ();
  const auto completed = std::make_shared<std::atomic<bool>> (false);
  const auto executor = stream_.get_executor ();

  RequestCompletion complete = [self, completed, executor] (
                                   http::message_generator&& message) mutable {
    if (completed->exchange (true)) {
      log_message (self->log_fn_, LogLevel::Warn,
                   "Request completion invoked more than once");
      return;
    }

    net::dispatch (executor, [self, message = std::move (message)] () mutable {
      try {
        self->write (std::move (message));
      } catch (const std::exception& exception) {
        log_message (self->log_fn_, LogLevel::Error,
                     std::format ("Failed to start response write: {}",
                                  exception.what ()));
        self->sendError (http::status::internal_server_error,
                         "Internal Server Error");
      } catch (...) {
        log_message (self->log_fn_, LogLevel::Error,
                     "Failed to start response write");
        self->sendError (http::status::internal_server_error,
                         "Internal Server Error");
      }
    });
  };

  try {
    requestHandler_ (requestParser_->release (), context_,
                     std::move (complete));
  } catch (const std::exception& exception) {
    log_message (log_fn_, LogLevel::Error,
                 std::format ("Request handler threw: {}", exception.what ()));
    if (!completed->exchange (true)) {
      sendError (http::status::internal_server_error, "Internal Server Error");
    }
  } catch (...) {
    log_message (log_fn_, LogLevel::Error,
                 "Request handler threw an unknown exception");
    if (!completed->exchange (true)) {
      sendError (http::status::internal_server_error, "Internal Server Error");
    }
  }
}

void HttpSession::write (http::message_generator&& msg) {
  auto keepAlive = msg.keep_alive ();
  stream_.expires_after (config_.response_write_timeout);
  beast::async_write (
      stream_, std::move (msg),
      beast::bind_front_handler (&HttpSession::onWrite, shared_from_this (),
                                 keepAlive));
}

void HttpSession::onWrite (bool keepAlive, beast::error_code ec, std::size_t) {
  if (ec) {
    log_message (
        log_fn_, LogLevel::Warn,
        std::format ("Error writing HTTP response: {}", ec.message ()));
    close ();
    return;
  }
  if (!keepAlive) {
    close ();
    return;
  }

  read ();
}

void HttpSession::close () {
  stream_.close ();
}

void HttpSession::sendError (http::status status, std::string_view msg) {
  http::response<http::string_body> response {status, current_request_version_};
  response.set (http::field::content_type, "text/plain");
  response.keep_alive (false);
  response.body () = msg;
  response.prepare_payload ();

  write (std::move (response));
}
}  // namespace web
