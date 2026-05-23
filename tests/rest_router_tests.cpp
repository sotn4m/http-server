#include "http_client.hpp"
#include "test_server.hpp"

#include <RestRouter.h>

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <stdexcept>

using namespace web;
using namespace web::test;

TEST_CASE ("RestRouter serves registered JSON route", "[rest]") {
  RestRouter router {};
  router.get ("/api/health", [] (RestRequest&& /*request*/) {
    return RestResponse::ok_json (R"({"status":"ok"})");
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::get, "/api/health", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
  REQUIRE (response.body () == R"({"status":"ok"})");
}

TEST_CASE ("RestRouter path parameters", "[rest]") {
  RestRouter router {};
  router.get ("/api/users/{id}", [] (RestRequest&& request) {
    const auto id = request.path_param ("id");
    REQUIRE (id.has_value ());
    return RestResponse::ok_json (std::format (R"({{"id":"{}"}})", *id));
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::get, "/api/users/42",
                                            11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
  REQUIRE (response.body () == R"({"id":"42"})");
}

TEST_CASE ("RestRouter decodes query '+' but preserves path '+'", "[rest]") {
  RestRouter router {};
  router.get ("/api/hello+world", [] (RestRequest&& request) {
    REQUIRE (request.path () == "/api/hello+world");
    REQUIRE (request.query_param ("name") ==
             std::optional<std::string> {"hello world"});
    return RestResponse::ok_json (R"({"ok":true})");
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {
      http::verb::get, "/api/hello+world?name=hello+world", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
}

TEST_CASE ("RestRequest header lookup is case-insensitive", "[rest]") {
  RestRouter router {};
  router.get ("/api/auth", [] (RestRequest&& request) {
    const auto authorization = request.header ("Authorization");
    REQUIRE (authorization.has_value ());
    REQUIRE (*authorization == "Bearer token");
    return RestResponse::ok_json (R"({"ok":true})");
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::get, "/api/auth", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set ("authorization", "Bearer token");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
}

TEST_CASE ("RestRouter returns 404 JSON for unknown route", "[rest]") {
  RestRouter router {};
  router.get ("/api/health",
              [] (RestRequest&&) { return RestResponse::ok_json ("{}"); });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::get, "/api/missing",
                                            11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::not_found);
  REQUIRE (response.body ().find ("not_found") != std::string::npos);
}

TEST_CASE ("RestRouter CORS preflight", "[rest]") {
  RestRouter router {};
  CorsConfig cors {};
  cors.enabled = true;
  cors.allowed_origins = {"http://localhost:5173"};
  router.set_cors (cors);
  router.get ("/api/health", [] (RestRequest&&) {
    return RestResponse::ok_json (R"({"status":"ok"})");
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::options, "/api/health",
                                            11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::origin, "http://localhost:5173");
  request.set (http::field::access_control_request_method, "GET");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::no_content);
  REQUIRE (response[http::field::access_control_allow_origin] ==
           "http://localhost:5173");
  REQUIRE_FALSE (response["X-Request-ID"].empty ());
}

TEST_CASE ("RestRouter adds request id response header", "[rest]") {
  RestRouter router {};
  router.get ("/api/health", [] (RestRequest&&) {
    return RestResponse::ok_json (R"({"status":"ok"})");
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::get, "/api/health", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE_FALSE (response["X-Request-ID"].empty ());
}

TEST_CASE ("RestRouter normalizes path segments", "[rest]") {
  RestRouter router {};
  router.get ("/api/health", [] (RestRequest&&) {
    return RestResponse::ok_json (R"({"status":"ok"})");
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::get,
                                            "/api/./users/../health", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
}

TEST_CASE ("RestRouter decodes unreserved percent-encoding", "[rest]") {
  RestRouter router {};
  router.get ("/api/~users", [] (RestRequest&&) {
    return RestResponse::ok_json (R"({"status":"ok"})");
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::get, "/api/%7eusers",
                                            11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
}

TEST_CASE ("RestRouter keeps reserved percent-encoding in paths", "[rest]") {
  RestRouter router {};
  router.get ("/api/%40users", [] (RestRequest&&) {
    return RestResponse::ok_json (R"({"status":"ok"})");
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::get, "/api/%40users",
                                            11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
}

TEST_CASE ("RestRouter does not decode encoded slash in path segment",
           "[rest]") {
  RestRouter router {};
  router.get ("/api/a%2Fb", [] (RestRequest&&) {
    return RestResponse::ok_json (R"({"status":"ok"})");
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::get, "/api/a%2Fb", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
}

TEST_CASE ("RestRouter preserves malformed percent-encoding", "[rest]") {
  RestRouter router {};
  router.get ("/api/%GG", [] (RestRequest&&) {
    return RestResponse::ok_json (R"({"status":"ok"})");
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::get, "/api/%GG", 11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::connection, "close");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::ok);
}

TEST_CASE ("RestRouter rejects wildcard CORS with credentials", "[rest]") {
  RestRouter router {};
  CorsConfig cors {};
  cors.enabled = true;
  cors.allow_credentials = true;
  cors.allowed_origins = {"*"};

  REQUIRE_THROWS_AS (router.set_cors (cors), std::invalid_argument);
}

TEST_CASE ("RestRouter rejects enabled CORS without origins", "[rest]") {
  RestRouter router {};
  CorsConfig cors {};
  cors.enabled = true;
  REQUIRE_THROWS_AS (router.set_cors (cors), std::invalid_argument);
}

TEST_CASE ("RestRouter preflight rejects unknown path", "[rest]") {
  RestRouter router {};
  CorsConfig cors {};
  cors.enabled = true;
  cors.allowed_origins = {"http://localhost:5173"};
  router.set_cors (cors);
  router.get ("/api/health", [] (RestRequest&&) {
    return RestResponse::ok_json (R"({"status":"ok"})");
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::options, "/api/missing",
                                            11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::origin, "http://localhost:5173");
  request.set (http::field::access_control_request_method, "GET");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::not_found);
}

TEST_CASE ("RestRouter preflight rejects disallowed origin", "[rest]") {
  RestRouter router {};
  CorsConfig cors {};
  cors.enabled = true;
  cors.allowed_origins = {"http://localhost:5173"};
  router.set_cors (cors);
  router.get ("/api/health", [] (RestRequest&&) {
    return RestResponse::ok_json (R"({"status":"ok"})");
  });

  TestServer server {router.request_handler ()};

  http::request<http::string_body> request {http::verb::options, "/api/health",
                                            11};
  request.set (http::field::host, "127.0.0.1");
  request.set (http::field::origin, "http://evil.example");
  request.set (http::field::access_control_request_method, "GET");
  request.prepare_payload ();

  const auto response = http_request (server.port (), std::move (request));
  REQUIRE (response.result () == http::status::forbidden);
}

TEST_CASE ("RestRouter cannot mutate after request_handler", "[rest]") {
  RestRouter router {};
  auto handler = router.request_handler ();
  (void)handler;

  REQUIRE_THROWS_AS (
      router.get ("/api/health",
                  [] (RestRequest&&) { return RestResponse::ok_json ("{}"); }),
      std::logic_error);
}
