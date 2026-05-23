#include "http_client.hpp"
#include "test_server.hpp"

#include <HttpServer.h>
#include <Logging.h>
#include <RequestHandler.h>
#include <ServerConfig.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace web;
using namespace web::test;

http::message_generator make_ok (http::request<http::string_body> request,
                                 std::string body) {
  http::response<http::string_body> response {http::status::ok,
                                              request.version ()};
  response.set (http::field::content_type, "text/plain");
  response.keep_alive (request.keep_alive ());
  response.body () = std::move (body);
  response.prepare_payload ();
  return response;
}

RequestHandler echo_handler () {
  return make_sync_handler ([] (http::request<http::string_body> request,
                                const RequestContext&) {
    if (request.method () == http::verb::get && request.target () == "/hello") {
      return make_ok (std::move (request), "hello");
    }
    if (request.method () == http::verb::get && request.target () == "/boom") {
      throw std::runtime_error ("handler failure");
    }
    http::response<http::string_body> response {http::status::not_found,
                                                request.version ()};
    response.keep_alive (false);
    response.prepare_payload ();
    return http::message_generator {std::move (response)};
  });
}

}  // namespace

TEST_CASE ("GET /hello returns 200", "[integration]") {
  TestServer server {echo_handler ()};

  http::request<http::string_body> request {http::verb::get, "/hello", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
  REQUIRE (response.body () == "hello");
}

TEST_CASE ("oversized body returns 413", "[integration]") {
  ServerConfig config {};
  config.max_body_bytes = 256;
  TestServer server {echo_handler (), config};

  std::string payload (512, 'x');
  http::request<http::string_body> request {http::verb::post, "/hello", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::content_type, "text/plain");
  request.set (http::field::connection, "close");
  request.body () = std::move (payload);
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::payload_too_large);
}

TEST_CASE ("oversized header returns 431", "[integration]") {
  ServerConfig config {};
  config.max_header_bytes = 512;
  TestServer server {echo_handler (), config};

  http::request<http::string_body> request {http::verb::get, "/hello", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.set (http::field::cookie, std::string (1024, 'c'));
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::request_header_fields_too_large);
}

TEST_CASE ("keep-alive serves sequential requests", "[integration]") {
  TestServer server {echo_handler ()};

  http::request<http::string_body> request1 {http::verb::get, "/hello", 11};
  request1.set (http::field::host, "127.0.0.1");
  request1.keep_alive (true);
  request1.prepare_payload ();

  http::request<http::string_body> request2 {http::verb::get, "/hello", 11};
  request2.set (http::field::host, "127.0.0.1");
  request2.keep_alive (false);
  request2.prepare_payload ();

  std::vector<http::response<http::string_body>> responses;
  http_requests_keep_alive (server.port (), {request1, request2}, responses);

  REQUIRE (responses.size () == 2);
  REQUIRE (responses[0].result () == http::status::ok);
  REQUIRE (responses[0].body () == "hello");
  REQUIRE (responses[1].result () == http::status::ok);
  REQUIRE (responses[1].body () == "hello");
}

TEST_CASE ("handler exception becomes 500", "[integration]") {
  TestServer server {echo_handler ()};

  http::request<http::string_body> request {http::verb::get, "/boom", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::internal_server_error);
}

TEST_CASE ("stop cancels accept loop", "[integration]") {
  std::atomic<int> handler_calls {0};
  const RequestHandler handler = make_sync_handler (
      [&handler_calls] (http::request<http::string_body> request,
                        const RequestContext&) {
        handler_calls.fetch_add (1, std::memory_order_relaxed);
        return make_ok (std::move (request), "ok");
      });

  ServerConfig config {};
  TestServer server {handler, config};
  const auto port = server.port ();

  http::request<http::string_body> request {http::verb::get, "/hello", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto ok_response = http_request (port, request);
  REQUIRE (ok_response.result () == http::status::ok);
  REQUIRE (handler_calls.load () == 1);

  server.shutdown ();

  const auto connect_error =
      tcp_connect_error (port, std::chrono::milliseconds {500});
  REQUIRE (connect_error == net::error::connection_refused);
  REQUIRE (handler_calls.load () == 1);
}

TEST_CASE ("wait_for_sessions returns after active requests finish",
           "[integration]") {
  ServerConfig config {};
  config.log_fn = null_logger ();

  std::mutex mutex;
  std::condition_variable request_done;
  bool handler_may_finish = false;

  const RequestHandler handler = make_sync_handler (
      [&] (http::request<http::string_body> request, const RequestContext&) {
        std::unique_lock lock {mutex};
        request_done.wait (lock, [&] { return handler_may_finish; });
        return make_ok (std::move (request), "done");
      });

  boost::asio::io_context io_context;
  HttpServer server {io_context.get_executor (), config, handler};
  server.start ();

  std::thread io_thread {[&io_context] { io_context.run (); }};

  const auto port = server.local_endpoint ().port ();
  std::thread client {[port] {
    http::request<http::string_body> request {http::verb::get, "/hello", 11};
    request.set (http::field::host, "127.0.0.1");
    request.set (http::field::connection, "close");
    request.prepare_payload ();
    const auto response = http_request (port, std::move (request));
    REQUIRE (response.result () == http::status::ok);
  }};

  std::this_thread::sleep_for (std::chrono::milliseconds {30});
  REQUIRE (server.active_sessions () >= 1);

  server.stop ();
  {
    std::lock_guard lock {mutex};
    handler_may_finish = true;
  }
  request_done.notify_all ();

  REQUIRE (server.wait_for_sessions (std::chrono::seconds {2}));

  io_context.stop ();
  client.join ();
  io_thread.join ();
}
