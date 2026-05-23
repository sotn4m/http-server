#include "RestRouter.h"

#include "RestAdapter.h"

#include "Logging.h"

#include <boost/beast/http.hpp>
#include <format>
#include <stdexcept>
#include <string_view>

namespace web {

namespace http = beast::http;

namespace {

http::message_generator make_response (
    RestResponse rest_response,
    const http::request<http::string_body>& request,
    const CorsConfig& cors,
    std::string_view request_id) {
  if (!request_id.empty ()) {
    rest_response.set_header ("X-Request-ID", std::string {request_id});
  }
  auto response = to_http_response (std::move (rest_response), request);
  if (cors.enabled) {
    detail::apply_cors_headers (response, request, cors);
  }
  return response;
}

AsyncRestHandler make_async_handler (RestHandler handler) {
  return [handler = std::move (handler)] (RestRequest&& request,
                                          RestCompletion complete) {
    complete (handler (std::move (request)));
  };
}

bool is_cors_origin_allowed (std::string_view origin, const CorsConfig& cors) {
  if (origin.empty ()) {
    return true;
  }
  for (const auto& allowed : cors.allowed_origins) {
    if (allowed == origin) {
      return true;
    }
  }
  return false;
}

std::vector<std::string_view> split_path_view (std::string_view path) {
  std::vector<std::string_view> segments;
  if (path.empty ()) {
    return segments;
  }

  std::size_t start = path.front () == '/' ? 1 : 0;
  while (start <= path.size ()) {
    const auto slash = path.find ('/', start);
    if (slash == start) {
      start = slash + 1;
      continue;
    }
    if (slash == std::string_view::npos) {
      if (start < path.size ()) {
        segments.push_back (path.substr (start));
      }
      break;
    }
    segments.push_back (path.substr (start, slash - start));
    start = slash + 1;
  }
  return segments;
}

}  // namespace

std::vector<RestRouter::RouteSegment> RestRouter::parse_route_segments (
    std::string_view path) {
  std::vector<RouteSegment> parsed;
  for (const auto segment : split_path_view (path)) {
    if (segment.size () >= 2 && segment.front () == '{' &&
        segment.back () == '}') {
      parsed.push_back (RouteSegment {
          true, std::string {segment.substr (1, segment.size () - 2)}});
      continue;
    }
    parsed.push_back (RouteSegment {false, std::string {segment}});
  }
  return parsed;
}

int RestRouter::route_score (const std::vector<RouteSegment>& segments) {
  int score = 0;
  for (const auto& segment : segments) {
    score += segment.is_param ? 1 : 1000;
  }
  return score;
}

bool RestRouter::match_route_segments (
    const std::vector<RouteSegment>& route_segments,
    const std::vector<std::string_view>& path_segments,
    std::unordered_map<std::string, std::string>& path_params) {
  if (route_segments.size () != path_segments.size ()) {
    return false;
  }

  for (std::size_t index = 0; index < route_segments.size (); ++index) {
    const auto& segment = route_segments[index];
    const auto path_segment = path_segments[index];
    if (segment.is_param) {
      path_params.emplace (segment.value, std::string {path_segment});
      continue;
    }
    if (segment.value != path_segment) {
      return false;
    }
  }
  return true;
}

const RestRouter::RouteEntry* RestRouter::find_best_route (
    const SharedState& state,
    HttpMethod method,
    std::string_view path,
    std::unordered_map<std::string, std::string>& path_params) {
  const auto path_segments = split_path_view (path);
  const RestRouter::RouteEntry* best_match = nullptr;
  int best_score = -1;

  const auto routes_iterator = state.routes_by_method.find (method);
  if (routes_iterator == state.routes_by_method.end ()) {
    return nullptr;
  }
  for (const auto& route : routes_iterator->second) {
    std::unordered_map<std::string, std::string> candidate_params;
    if (!RestRouter::match_route_segments (route.segments, path_segments,
                                           candidate_params)) {
      continue;
    }

    if (route.score > best_score) {
      best_score = route.score;
      best_match = &route;
      path_params = std::move (candidate_params);
    }
  }

  return best_match;
}

bool RestRouter::path_exists_in_state (const SharedState& state,
                                       std::string_view path) {
  const auto path_segments = split_path_view (path);
  std::unordered_map<std::string, std::string> unused;
  for (const auto& [_, routes] : state.routes_by_method) {
    for (const auto& route : routes) {
      unused.clear ();
      if (RestRouter::match_route_segments (route.segments, path_segments,
                                            unused)) {
        return true;
      }
    }
  }
  return false;
}

RestRouter::RestRouter () : state_ {std::make_shared<SharedState> ()} {}

void RestRouter::set_cors (CorsConfig config) {
  if (state_->frozen) {
    throw std::logic_error (
        "RestRouter: cannot modify CORS config after request_handler()");
  }
  if (config.enabled && config.allowed_origins.empty ()) {
    throw std::invalid_argument (
        "CorsConfig: allowed_origins must not be empty when enabled=true");
  }
  if (config.allow_credentials) {
    for (const auto& origin : config.allowed_origins) {
      if (origin == "*") {
        throw std::invalid_argument (
            "CorsConfig: allow_credentials=true is incompatible with "
            "allowed_origins containing '*'");
      }
    }
  }
  state_->cors = std::move (config);
}

void RestRouter::set_log (LogFn log_fn) {
  if (state_->frozen) {
    throw std::logic_error (
        "RestRouter: cannot modify logger after request_handler()");
  }
  state_->log_fn = std::move (log_fn);
}

void RestRouter::add_route (HttpMethod method,
                            std::string path,
                            AsyncRestHandler handler) {
  if (state_->frozen) {
    throw std::logic_error (
        "RestRouter: cannot register routes after request_handler()");
  }
  auto segments = RestRouter::parse_route_segments (path);
  const auto score = RestRouter::route_score (segments);
  state_->routes_by_method[method].push_back (RouteEntry {
      std::move (path),
      std::move (segments),
      score,
      std::move (handler),
  });
}

void RestRouter::get (std::string path, RestHandler handler) {
  add_route (HttpMethod::GET, std::move (path),
             make_async_handler (std::move (handler)));
}

void RestRouter::post (std::string path, RestHandler handler) {
  add_route (HttpMethod::POST, std::move (path),
             make_async_handler (std::move (handler)));
}

void RestRouter::put (std::string path, RestHandler handler) {
  add_route (HttpMethod::PUT, std::move (path),
             make_async_handler (std::move (handler)));
}

void RestRouter::patch (std::string path, RestHandler handler) {
  add_route (HttpMethod::PATCH, std::move (path),
             make_async_handler (std::move (handler)));
}

void RestRouter::delete_ (std::string path, RestHandler handler) {
  add_route (HttpMethod::DELETE_, std::move (path),
             make_async_handler (std::move (handler)));
}

void RestRouter::head (std::string path, RestHandler handler) {
  add_route (HttpMethod::HEAD, std::move (path),
             make_async_handler (std::move (handler)));
}

void RestRouter::get_async (std::string path, AsyncRestHandler handler) {
  add_route (HttpMethod::GET, std::move (path), std::move (handler));
}

void RestRouter::post_async (std::string path, AsyncRestHandler handler) {
  add_route (HttpMethod::POST, std::move (path), std::move (handler));
}

void RestRouter::put_async (std::string path, AsyncRestHandler handler) {
  add_route (HttpMethod::PUT, std::move (path), std::move (handler));
}

void RestRouter::patch_async (std::string path, AsyncRestHandler handler) {
  add_route (HttpMethod::PATCH, std::move (path), std::move (handler));
}

void RestRouter::delete_async (std::string path, AsyncRestHandler handler) {
  add_route (HttpMethod::DELETE_, std::move (path), std::move (handler));
}

RequestHandler RestRouter::request_handler () const {
  const auto state = state_;
  state->frozen = true;

  return [state] (http::request<http::string_body>&& request,
                  RequestContext context, RequestCompletion complete) {
    const auto http_request =
        std::make_shared<http::request<http::string_body>> (
            std::move (request));

    auto base_request = to_rest_request (*http_request, context);

    if (state->cors.enabled && http_request->method () == http::verb::options) {
      if (!RestRouter::path_exists_in_state (*state, base_request.path ())) {
        complete (make_response (RestResponse::not_found (), *http_request,
                                 state->cors, base_request.request_id ()));
        return;
      }
      if (!is_cors_origin_allowed ((*http_request)[http::field::origin],
                                   state->cors)) {
        complete (make_response (
            RestResponse::json (403, R"({"error":"origin_not_allowed"})"),
            *http_request, state->cors, base_request.request_id ()));
        return;
      }
      http::response<http::string_body> response {http::status::no_content,
                                                  http_request->version ()};
      response.keep_alive (http_request->keep_alive ());
      response.set ("X-Request-ID", base_request.request_id ());
      detail::apply_cors_headers (response, *http_request, state->cors);
      complete (http::message_generator {std::move (response)});
      return;
    }

    const auto method = base_request.method ();

    if (method == HttpMethod::UNKNOWN) {
      complete (make_response (
          RestResponse::json (400, R"({"error":"unsupported_method"})"),
          *http_request, state->cors, context.request_id));
      return;
    }

    std::unordered_map<std::string, std::string> path_params;
    const RouteEntry* route = RestRouter::find_best_route (
        *state, method, base_request.path (), path_params);
    bool used_get_fallback = false;

    if (route == nullptr && method == HttpMethod::HEAD) {
      route = RestRouter::find_best_route (*state, HttpMethod::GET,
                                           base_request.path (), path_params);
      used_get_fallback = route != nullptr;
    }

    if (route == nullptr) {
      if (RestRouter::path_exists_in_state (*state, base_request.path ())) {
        complete (make_response (RestResponse::method_not_allowed (),
                                 *http_request, state->cors,
                                 base_request.request_id ()));
        return;
      }
      complete (make_response (RestResponse::not_found (), *http_request,
                               state->cors, base_request.request_id ()));
      return;
    }

    RestRequest rest_request = std::move (base_request);
    rest_request.set_path_params (std::move (path_params));
    const auto request_id = rest_request.request_id ();

    const bool strip_body_for_head =
        rest_request.method () == HttpMethod::HEAD || used_get_fallback;

    try {
      route->handler (
          std::move (rest_request),
          [complete = std::move (complete), http_request, state,
           strip_body_for_head,
           request_id] (RestResponse&& rest_response) mutable {
            if (strip_body_for_head) {
              rest_response.set_body ({});
            }
            complete (make_response (std::move (rest_response), *http_request,
                                     state->cors, request_id));
          });
    } catch (const std::exception& exception) {
      const auto logger = resolve_log_fn (state->log_fn);
      log_message (logger, LogLevel::Error,
                   std::format ("REST handler threw: {}", exception.what ()));
      complete (make_response (
          RestResponse::json (500, R"({"error":"internal_server_error"})"),
          *http_request, state->cors, base_request.request_id ()));
    } catch (...) {
      const auto logger = resolve_log_fn (state->log_fn);
      log_message (logger, LogLevel::Error,
                   "REST handler threw an unknown exception");
      complete (make_response (
          RestResponse::json (500, R"({"error":"internal_server_error"})"),
          *http_request, state->cors, base_request.request_id ()));
    }
  };
}

}  // namespace web
