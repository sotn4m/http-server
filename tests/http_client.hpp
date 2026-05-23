#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <string>
#include <vector>

namespace web::test {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

inline http::response<http::string_body> http_request (
    std::uint16_t port,
    http::request<http::string_body> request,
    std::chrono::milliseconds timeout = std::chrono::seconds {5}) {
  net::io_context io_context;
  tcp::resolver resolver (io_context);
  beast::tcp_stream stream (io_context);

  stream.expires_after (timeout);
  const auto endpoints = resolver.resolve ("127.0.0.1", std::to_string (port));
  stream.connect (endpoints);

  stream.expires_after (timeout);
  http::write (stream, request);

  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  stream.expires_after (timeout);
  http::read (stream, buffer, response);

  beast::error_code errorCode;
  stream.socket ().shutdown (tcp::socket::shutdown_both, errorCode);

  return response;
}

/// Sends multiple requests on one connection (keep-alive).
inline void http_requests_keep_alive (
    std::uint16_t port,
    const std::vector<http::request<http::string_body>>& requests,
    std::vector<http::response<http::string_body>>& responses,
    std::chrono::milliseconds timeout = std::chrono::seconds {5}) {
  responses.clear ();
  responses.reserve (requests.size ());

  net::io_context io_context;
  tcp::resolver resolver (io_context);
  beast::tcp_stream stream (io_context);

  stream.expires_after (timeout);
  const auto endpoints = resolver.resolve ("127.0.0.1", std::to_string (port));
  stream.connect (endpoints);

  for (const auto& request : requests) {
    stream.expires_after (timeout);
    http::write (stream, request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    stream.expires_after (timeout);
    http::read (stream, buffer, response);
    responses.push_back (std::move (response));
  }

  beast::error_code errorCode;
  stream.socket ().shutdown (tcp::socket::shutdown_both, errorCode);
}

/// Returns the error code from a TCP connect attempt (no HTTP).
inline boost::system::error_code tcp_connect_error (
    std::uint16_t port,
    std::chrono::milliseconds timeout = std::chrono::seconds {5}) {
  net::io_context io_context;
  beast::tcp_stream stream (io_context);
  stream.expires_after (timeout);
  boost::system::error_code error_code;
  const auto endpoint =
      tcp::endpoint {net::ip::make_address ("127.0.0.1"), port};
  stream.connect (endpoint, error_code);
  return error_code;
}

}  // namespace web::test
