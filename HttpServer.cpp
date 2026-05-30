#include "HttpServer.h"

#include <Json.h>
#include <boost/asio/error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <format>
#include <system_error>
#include "HttpSession.h"

namespace web {

namespace {

namespace beast = boost::beast;
namespace http = beast::http;

constexpr int kMaxConsecutiveAcceptErrors = 8;

std::string error_payload (std::string_view error_code) {
  return json::serialize (json::object ({{"error", std::string {error_code}}}));
}

void reject_overloaded_connection (tcp::socket socket, const LogFn& log_fn) {
  boost::system::error_code error_code;
  beast::tcp_stream stream {std::move (socket)};
  stream.expires_after (std::chrono::seconds {5});

  http::response<http::string_body> response {http::status::service_unavailable,
                                              11};
  response.set (http::field::content_type, "application/json; charset=utf-8");
  response.set (http::field::connection, "close");
  response.body () = error_payload ("service_unavailable");
  response.prepare_payload ();

  http::write (stream, response, error_code);
  stream.close ();
  if (error_code) {
    log_message (log_fn, LogLevel::Warn,
                 std::format ("Failed to write overload response: {}",
                              error_code.message ()));
  }
}

bool is_transient_accept_error (const boost::system::error_code& error_code) {
  return error_code == net::error::interrupted ||
         error_code == net::error::try_again ||
         error_code == net::error::connection_aborted ||
         error_code == net::error::network_down ||
         error_code == net::error::network_reset;
}

}  // namespace

HttpServer::HttpServer (net::io_context::executor_type executor,
                        ServerConfig config,
                        RequestHandler requestHandler)
    : acceptor_ {executor},
      config_ {},
      requestHandler_ {std::move (requestHandler)},
      log_fn_ {resolve_log_fn (config.log_fn)},
      session_state_ {std::make_shared<SessionState> ()} {
  validate_server_config (config);
  config_ = std::move (config);

  tcp::resolver resolver (executor);
  const auto results = resolver.resolve (config_.host, config_.port);
  if (results.empty ()) {
    throw std::system_error (
        std::make_error_code (std::errc::address_not_available),
        "tcp::resolver::resolve returned no endpoints for the given host/port");
  }

  const tcp::endpoint endpoint = *results.begin ();

  acceptor_.open (endpoint.protocol ());
  acceptor_.set_option (net::socket_base::reuse_address (true));
  acceptor_.bind (endpoint);
  acceptor_.listen (static_cast<int> (config_.listen_backlog));
  const auto bound = acceptor_.local_endpoint ();
  log_message (log_fn_, LogLevel::Info,
               std::format ("Listening on {}:{}", bound.address ().to_string (),
                            bound.port ()));
}

tcp::endpoint HttpServer::local_endpoint () const {
  return acceptor_.local_endpoint ();
}

std::size_t HttpServer::active_sessions () const noexcept {
  return session_state_->active_sessions.load (std::memory_order_relaxed);
}

bool HttpServer::wait_for_sessions (std::chrono::milliseconds timeout) const {
  std::unique_lock lock {session_state_->mutex};
  return session_state_->condition.wait_for (lock, timeout, [this] {
    return session_state_->active_sessions.load (std::memory_order_relaxed) ==
           0;
  });
}

void HttpServer::start () {
  accept ();
}

void HttpServer::stop () noexcept {
  if (stopped_.exchange (true)) {
    return;
  }

  net::post (acceptor_.get_executor (), [this] {
    boost::system::error_code error_code;
    acceptor_.cancel (error_code);
    if (error_code) {
      log_message (log_fn_, LogLevel::Warn,
                   std::format ("HttpServer: acceptor cancel failed: {}",
                                error_code.message ()));
    }
    error_code.clear ();
    acceptor_.close (error_code);
    if (error_code) {
      log_message (log_fn_, LogLevel::Warn,
                   std::format ("HttpServer: acceptor close failed: {}",
                                error_code.message ()));
    }
  });
}

void HttpServer::accept () {
  if (stopped_.load ()) {
    return;
  }

  acceptor_.async_accept (
      beast::bind_front_handler (&HttpServer::onAccept, this));
}

void HttpServer::onAccept (boost::system::error_code ec, tcp::socket socket) {
  if (ec) {
    if (ec == net::error::operation_aborted) {
      return;
    }

    log_message (
        log_fn_, LogLevel::Error,
        std::format ("Error accepting connections: {}", ec.message ()));

    if (!is_transient_accept_error (ec)) {
      log_message (log_fn_, LogLevel::Error,
                   "HttpServer: non-transient accept error; stopping accept "
                   "loop");
      return;
    }

    const int failures =
        consecutive_accept_errors_.fetch_add (1, std::memory_order_relaxed) + 1;
    if (failures >= kMaxConsecutiveAcceptErrors) {
      log_message (log_fn_, LogLevel::Error,
                   "HttpServer: too many consecutive accept errors; stopping "
                   "accept loop");
      return;
    }

    if (!stopped_.load ()) {
      accept ();
    }
    return;
  }

  consecutive_accept_errors_.store (0, std::memory_order_relaxed);

  if (stopped_.load ()) {
    try {
      socket.close ();
    } catch (...) {
      log_message (log_fn_, LogLevel::Error,
                   "HttpServer: close accepted socket during shutdown failed");
    }
    return;
  }

  const std::size_t active_after_accept =
      session_state_->active_sessions.fetch_add (1, std::memory_order_acq_rel) +
      1;
  if (config_.max_active_sessions > 0 &&
      active_after_accept > config_.max_active_sessions) {
    session_state_->active_sessions.fetch_sub (1, std::memory_order_acq_rel);
    log_message (
        log_fn_, LogLevel::Warn,
        std::format ("Rejecting connection: active sessions {} exceeds "
                     "max_active_sessions {}",
                     active_after_accept, config_.max_active_sessions));
    reject_overloaded_connection (std::move (socket), log_fn_);
    if (!stopped_.load ()) {
      accept ();
    }
    return;
  }

  std::make_shared<HttpSession> (
      std::move (socket), requestHandler_, log_fn_, session_state_,
      SessionConfig {config_.max_body_bytes, config_.max_header_bytes,
                     config_.request_read_timeout,
                     config_.response_write_timeout})
      ->run ();

  if (!stopped_.load ()) {
    accept ();
  }
}

}  // namespace web
