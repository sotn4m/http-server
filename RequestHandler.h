#pragma once

#include "RequestContext.h"

#include <boost/beast/http.hpp>
#include <functional>

namespace web {

namespace beast = boost::beast;
namespace http = beast::http;

/// Called exactly once per request with the HTTP response (sync or async).
using RequestCompletion =
    std::function<void (http::message_generator&& response)>;

/// Primary handler type: receive request and context, invoke `complete` when
/// ready.
using RequestHandler =
    std::function<void (http::request<http::string_body>&& request,
                        RequestContext context,
                        RequestCompletion complete)>;

/// Convenience type for synchronous handlers (adapted via `make_sync_handler`).
using SyncRequestHandler = std::function<http::message_generator (
    http::request<http::string_body>&& request,
    const RequestContext& context)>;

/// Wraps a synchronous handler for use with `HttpServer` / `HttpSession`.
[[nodiscard]] inline RequestHandler make_sync_handler (
    SyncRequestHandler handler) {
  return [handler = std::move (handler)] (
             http::request<http::string_body>&& request, RequestContext context,
             RequestCompletion complete) {
    complete (handler (std::move (request), context));
  };
}

}  // namespace web
