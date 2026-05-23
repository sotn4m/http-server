#include "http_client.hpp"
#include "test_server.hpp"

#include <Middleware.h>
#include <RestRouter.h>
#include <WorkPool.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

using namespace web;
using namespace web::test;

TEST_CASE ("async REST handler completes after WorkPool offload", "[async]") {
  RestRouter router {};
  WorkPool pool {2};
  router.get_async (
      "/api/slow", [&pool] (RestRequest&&, RestCompletion complete) {
        post_rest_work (
            pool,
            [] {
              std::this_thread::sleep_for (std::chrono::milliseconds {25});
              return RestResponse::ok_json (R"({"ok":true})");
            },
            std::move (complete));
      });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::get, "/api/slow", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
  REQUIRE (response.body () == R"({"ok":true})");
}

TEST_CASE ("middleware observes request context", "[middleware]") {
  RestRouter router {};
  router.get ("/api/health", [] (RestRequest&& request) {
    REQUIRE (!request.request_id ().empty ());
    REQUIRE (!request.remote_address ().empty ());
    return RestResponse::ok_json (R"({"status":"ok"})");
  });

  std::mutex mutex;
  std::string observed_request_id;

  const Middleware capture_id = [&] (RequestHandler next) {
    return [&, next = std::move (next)] (
               http::request<http::string_body>&& request,
               RequestContext context, RequestCompletion complete) {
      {
        const std::lock_guard lock {mutex};
        observed_request_id = context.request_id;
      }
      next (std::move (request), std::move (context), std::move (complete));
    };
  };

  const RequestHandler handler =
      chain_middleware ({capture_id}, router.request_handler ());

  TestServer server {handler};

  http::request<http::string_body> request {http::verb::get, "/api/health", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);

  const std::lock_guard lock {mutex};
  REQUIRE_FALSE (observed_request_id.empty ());
}

TEST_CASE ("RestResponse redirect sets Location", "[rest]") {
  RestRouter router {};
  router.get ("/go", [] (RestRequest&&) {
    return RestResponse::redirect ("https://example.com/long", false);
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::get, "/go", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::found);
  REQUIRE (response[http::field::location] == "https://example.com/long");
}

TEST_CASE ("HEAD falls back to GET route without body", "[rest]") {
  RestRouter router {};
  router.get ("/resource", [] (RestRequest&&) {
    return RestResponse::ok_json (R"({"hidden":true})");
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::head, "/resource", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
  REQUIRE (response.body ().empty ());
}

TEST_CASE ("specific route wins over param route", "[rest]") {
  RestRouter router {};
  router.get ("/api/health", [] (RestRequest&&) {
    return RestResponse::ok_json (R"({"route":"health"})");
  });
  router.get ("/api/{name}", [] (RestRequest&& request) {
    const auto name = request.path_param ("name");
    REQUIRE (name.has_value ());
    return RestResponse::ok_json (
        std::format (R"({{"route":"param","name":"{}"}})", *name));
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::get, "/api/health", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
  REQUIRE (response.body () == R"({"route":"health"})");
}

TEST_CASE ("put_async registers asynchronous PUT handler", "[async]") {
  RestRouter router {};
  WorkPool pool {1};
  router.put_async (
      "/api/items/1", [&pool] (RestRequest&&, RestCompletion complete) {
        post_rest_work (
            pool, [] { return RestResponse::ok_json (R"({"updated":true})"); },
            std::move (complete));
      });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::put, "/api/items/1",
                                            11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
  REQUIRE (response.body () == R"({"updated":true})");
}

TEST_CASE ("RestResponse rejects invalid header characters", "[rest]") {
  RestResponse response {200};
  REQUIRE_THROWS_AS (response.set_header ("X-Test", "ok\r\nX-Evil: 1"),
                     std::invalid_argument);
}
