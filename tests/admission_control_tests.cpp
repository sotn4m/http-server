#include "http_client.hpp"
#include "test_server.hpp"

#include <RestRouter.h>
#include <ServerConfig.h>
#include <WorkPool.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace web;
using namespace web::test;

TEST_CASE ("max_active_sessions rejects excess connections with 503",
           "[admission]") {
  ServerConfig config {};
  config.max_active_sessions = 1;
  config.log_fn = null_logger ();

  std::mutex mutex;
  std::condition_variable gate;
  bool release_first = false;

  const RequestHandler handler = make_sync_handler (
      [&] (http::request<http::string_body> request, const RequestContext&) {
        std::unique_lock lock {mutex};
        gate.wait (lock, [&] { return release_first; });

        http::response<http::string_body> response {http::status::ok,
                                                    request.version ()};
        response.set (http::field::content_type, "text/plain");
        response.keep_alive (false);
        response.body () = "ok";
        response.prepare_payload ();
        return http::message_generator {std::move (response)};
      });

  boost::asio::io_context io_context;
  HttpServer server {io_context.get_executor (), config, handler};
  server.start ();
  std::thread io_thread_a {[&io_context] { io_context.run (); }};
  std::thread io_thread_b {[&io_context] { io_context.run (); }};

  const auto port = server.local_endpoint ().port ();

  std::thread first_client {[port] {
    http::request<http::string_body> request {http::verb::get, "/hold", 11};
    request.set (http::field::host, "127.0.0.1");
    request.set (http::field::connection, "close");
    request.prepare_payload ();
    const auto response = http_request (port, std::move (request));
    REQUIRE (response.result () == http::status::ok);
  }};

  std::this_thread::sleep_for (std::chrono::milliseconds {50});
  REQUIRE (server.active_sessions () >= 1);

  http::request<http::string_body> overload_request {http::verb::get, "/hold",
                                                     11};
  overload_request.set (http::field::host, "127.0.0.1");
  overload_request.set (http::field::connection, "close");
  overload_request.prepare_payload ();

  const auto overload_response =
      http_request (port, std::move (overload_request));
  REQUIRE (overload_response.result () == http::status::service_unavailable);
  REQUIRE (overload_response.body ().find ("service_unavailable") !=
           std::string::npos);

  {
    std::lock_guard lock {mutex};
    release_first = true;
  }
  gate.notify_all ();

  first_client.join ();
  server.stop ();
  (void)server.wait_for_sessions (std::chrono::seconds {2});
  io_context.stop ();
  io_thread_a.join ();
  io_thread_b.join ();
}

TEST_CASE ("WorkPool max_in_flight_work returns 503 when saturated",
           "[admission]") {
  WorkPool pool {1, 1};

  std::mutex mutex;
  std::condition_variable gate;
  bool release_work = false;
  std::atomic<int> started {0};

  RestRouter router {};
  router.get_async ("/api/slow", [&] (RestRequest&&, RestCompletion complete) {
    post_rest_work (
        pool,
        [&] {
          started.fetch_add (1, std::memory_order_relaxed);
          std::unique_lock lock {mutex};
          gate.wait (lock, [&] { return release_work; });
          return RestResponse::ok_json (R"({"ok":true})");
        },
        std::move (complete));
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request1 {http::verb::get, "/api/slow", 11};
  request1.set (http::field::host, "127.0.0.1");
  request1.set (http::field::connection, "close");
  request1.prepare_payload ();

  http::request<http::string_body> request2 {http::verb::get, "/api/slow", 11};
  request2.set (http::field::host, "127.0.0.1");
  request2.set (http::field::connection, "close");
  request2.prepare_payload ();

  std::thread client1 {[&] {
    const auto response = http_request (server.port (), request1);
    REQUIRE (response.result () == http::status::ok);
  }};

  std::this_thread::sleep_for (std::chrono::milliseconds {80});
  REQUIRE (started.load () >= 1);
  REQUIRE (pool.in_flight_work () >= 1);

  const auto response2 = http_request (server.port (), std::move (request2));
  REQUIRE (response2.result () == http::status::service_unavailable);

  {
    std::lock_guard lock {mutex};
    release_work = true;
  }
  gate.notify_all ();
  client1.join ();
  pool.stop ();
}
