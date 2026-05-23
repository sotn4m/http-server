#pragma once

#include "Logging.h"
#include "RequestContext.h"
#include "RequestHandler.h"

#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace web {

namespace net = boost::asio;
using tcp = net::ip::tcp;

struct SessionConfig {
  std::uint64_t max_body_bytes;
  std::size_t max_header_bytes;
  std::chrono::seconds request_read_timeout;
  std::chrono::seconds response_write_timeout;
};

struct SessionState {
  std::atomic<std::size_t> active_sessions {0};
  std::mutex mutex;
  std::condition_variable condition;
};

class HttpSession : public std::enable_shared_from_this<HttpSession> {
 public:
  HttpSession (tcp::socket&& socket,
               RequestHandler requestHandler,
               LogFn log_fn,
               std::shared_ptr<SessionState> session_state,
               SessionConfig config);

  ~HttpSession ();

  void run ();

 private:
  void read ();
  void onRead (beast::error_code ec, std::size_t);
  void write (http::message_generator&& msg);
  void onWrite (bool keepAlive, beast::error_code ec, std::size_t);
  void sendError (http::status status, std::string_view msg);
  void close ();

  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  std::optional<http::request_parser<http::string_body>> requestParser_;
  RequestHandler requestHandler_;
  LogFn log_fn_;
  std::shared_ptr<SessionState> session_state_;
  RequestContext context_;
  SessionConfig config_;
  unsigned current_request_version_ {11};
};
}  // namespace web
