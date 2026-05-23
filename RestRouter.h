#pragma once

#include "CorsConfig.h"
#include "Logging.h"
#include "RequestHandler.h"
#include "RestTypes.h"

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace web {

/// Registers REST routes and produces a `RequestHandler` for `HttpServer`.
///
/// Route matching prefers more specific patterns (literal segments over
/// `{param}`). `HEAD` requests fall back to a matching `GET` route when no
/// `HEAD` route exists.
///
/// The returned handler captures internal shared state so it remains valid
/// after the `RestRouter` object is destroyed, as long as the handler itself is
/// kept alive.
class RestRouter {
 public:
  RestRouter ();

  void set_cors (CorsConfig config);

  /// Empty `log_fn` uses `default_stderr_logger()` for router-level errors.
  void set_log (LogFn log_fn);

  void get (std::string path, RestHandler handler);
  void post (std::string path, RestHandler handler);
  void put (std::string path, RestHandler handler);
  void patch (std::string path, RestHandler handler);
  void delete_ (std::string path, RestHandler handler);
  void head (std::string path, RestHandler handler);

  void get_async (std::string path, AsyncRestHandler handler);
  void post_async (std::string path, AsyncRestHandler handler);
  void put_async (std::string path, AsyncRestHandler handler);
  void patch_async (std::string path, AsyncRestHandler handler);
  void delete_async (std::string path, AsyncRestHandler handler);

  [[nodiscard]] RequestHandler request_handler () const;

 private:
  struct RouteSegment {
    bool is_param {false};
    std::string value;
  };

  struct RouteEntry {
    std::string pattern;
    std::vector<RouteSegment> segments;
    int score {0};
    AsyncRestHandler handler;
  };

  struct HttpMethodHash {
    std::size_t operator() (HttpMethod method) const noexcept {
      using Underlying = std::underlying_type_t<HttpMethod>;
      return std::hash<Underlying> {}(static_cast<Underlying> (method));
    }
  };

  struct SharedState {
    std::unordered_map<HttpMethod, std::vector<RouteEntry>, HttpMethodHash>
        routes_by_method;
    CorsConfig cors;
    LogFn log_fn;
    bool frozen {false};
  };

  void add_route (HttpMethod method,
                  std::string path,
                  AsyncRestHandler handler);

  [[nodiscard]] static std::vector<RouteSegment> parse_route_segments (
      std::string_view path);
  [[nodiscard]] static int route_score (
      const std::vector<RouteSegment>& segments);
  [[nodiscard]] static bool match_route_segments (
      const std::vector<RouteSegment>& route_segments,
      const std::vector<std::string_view>& path_segments,
      std::unordered_map<std::string, std::string>& path_params);

  [[nodiscard]] static const RouteEntry* find_best_route (
      const SharedState& state,
      HttpMethod method,
      std::string_view path,
      std::unordered_map<std::string, std::string>& path_params);

  [[nodiscard]] static bool path_exists_in_state (const SharedState& state,
                                                  std::string_view path);

  std::shared_ptr<SharedState> state_;
};

}  // namespace web
