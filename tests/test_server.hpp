#pragma once

#include <HttpServer.h>
#include <Logging.h>
#include <ServerConfig.h>

#include <boost/asio.hpp>
#include <thread>
#include <utility>

namespace web::test {

/// Runs `HttpServer` on a background `io_context` thread (default:
/// `127.0.0.1:0`).
class TestServer {
 public:
  explicit TestServer (RequestHandler handler, ServerConfig config = {}) {
    config.host = "127.0.0.1";
    if (config.port.empty ()) {
      config.port = "0";
    }
    if (!config.log_fn) {
      config.log_fn = null_logger ();
    }

    server_ = std::make_unique<HttpServer> (
        io_context_.get_executor (), std::move (config), std::move (handler));
    port_ = server_->local_endpoint ().port ();
    server_->start ();

    thread_ = std::thread ([this] { io_context_.run (); });
  }

  TestServer (const TestServer&) = delete;
  TestServer& operator= (const TestServer&) = delete;

  ~TestServer () { shutdown (); }

  void shutdown () {
    if (server_) {
      server_->stop ();
      server_.reset ();
    }
    io_context_.stop ();
    if (thread_.joinable ()) {
      thread_.join ();
    }
  }

  [[nodiscard]] std::uint16_t port () const noexcept { return port_; }

 private:
  boost::asio::io_context io_context_;
  std::unique_ptr<HttpServer> server_;
  std::uint16_t port_ {0};
  std::thread thread_;
};

}  // namespace web::test
