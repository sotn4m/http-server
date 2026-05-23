#pragma once
#include <atomic>
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>
#include <chrono>
#include <cstddef>
#include <memory>
#include "Logging.h"
#include "RequestHandler.h"
#include "ServerConfig.h"

namespace web {

namespace net = boost::asio;
using tcp = net::ip::tcp;

struct SessionState;

/// Async HTTP acceptor. Construction performs **blocking** host resolution
/// (`tcp::resolver::resolve`), bind, and listen on the calling thread.
///
/// Shutdown: `stop()` is idempotent and safe from any thread. It cancels the
/// pending accept and closes the listen socket so the bound port is released.
/// No new connections are accepted afterward. Existing sessions are not closed
/// by `stop()` (they run until the peer closes or timeouts/errors on the
/// stream). Use `wait_for_sessions()` after `stop()` to block until active
/// sessions finish or a timeout elapses. Calling `start()` after `stop()` has
/// no effect until a new `HttpServer` is constructed.
class HttpServer {
 public:
  explicit HttpServer (net::io_context::executor_type executor,
                       ServerConfig config,
                       RequestHandler requestHandler);

  HttpServer (const HttpServer&) = delete;
  HttpServer& operator= (const HttpServer&) = delete;

  void start ();

  /// Request shutdown: cancel accept loop. Idempotent; noexcept.
  void stop () noexcept;

  [[nodiscard]] std::size_t active_sessions () const noexcept;

  /// Waits until `active_sessions()` is zero or `timeout` elapses.
  [[nodiscard]] bool wait_for_sessions (
      std::chrono::milliseconds timeout) const;

  /// Bound listen endpoint (use after construction; port `0` in config is
  /// resolved).
  [[nodiscard]] tcp::endpoint local_endpoint () const;

 private:
  void accept ();
  void onAccept (boost::system::error_code ec, tcp::socket socket);

  tcp::acceptor acceptor_;
  ServerConfig config_;
  RequestHandler requestHandler_;
  LogFn log_fn_;
  std::shared_ptr<SessionState> session_state_;
  std::atomic<bool> stopped_ {false};
  std::atomic<int> consecutive_accept_errors_ {0};
};
}  // namespace web
