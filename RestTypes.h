#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace web {

enum class HttpMethod {
  GET,
  POST,
  PUT,
  PATCH,
  DELETE_,
  HEAD,
  OPTIONS,
  UNKNOWN
};

class RestRequest {
 public:
  RestRequest (HttpMethod method,
               std::string path,
               std::string body,
               std::unordered_map<std::string, std::string> headers,
               std::unordered_map<std::string, std::string> query,
               std::unordered_map<std::string, std::string> path_params,
               unsigned http_version,
               bool keep_alive,
               std::string request_id,
               std::string remote_address);

  [[nodiscard]] HttpMethod method () const noexcept { return method_; }
  [[nodiscard]] const std::string& path () const noexcept { return path_; }
  [[nodiscard]] const std::string& body () const& noexcept { return body_; }
  [[nodiscard]] const std::unordered_map<std::string, std::string>& headers ()
      const noexcept {
    return headers_;
  }

  [[nodiscard]] std::optional<std::string> header (std::string_view key) const;
  [[nodiscard]] const std::unordered_map<std::string, std::string>& query ()
      const noexcept {
    return query_;
  }

  [[nodiscard]] std::optional<std::string> query_param (
      std::string_view key) const;
  [[nodiscard]] std::optional<std::string> path_param (
      std::string_view key) const;

  [[nodiscard]] unsigned http_version () const noexcept {
    return http_version_;
  }
  [[nodiscard]] bool keep_alive () const noexcept { return keep_alive_; }

  [[nodiscard]] const std::string& request_id () const noexcept {
    return request_id_;
  }
  [[nodiscard]] const std::string& remote_address () const noexcept {
    return remote_address_;
  }

 private:
  void set_path_params (
      std::unordered_map<std::string, std::string> path_params) {
    path_params_ = std::move (path_params);
  }

  HttpMethod method_ = HttpMethod::UNKNOWN;
  std::string path_;
  std::string body_;
  std::unordered_map<std::string, std::string> headers_;
  std::unordered_map<std::string, std::string> query_;
  std::unordered_map<std::string, std::string> path_params_;
  unsigned http_version_ {11};
  bool keep_alive_ {true};
  std::string request_id_;
  std::string remote_address_;

  friend class RestRouter;
};

class RestResponse {
 public:
  explicit RestResponse (int status);

  void set_body (std::string body);
  void set_header (std::string key, std::string value);

  [[nodiscard]] int status () const noexcept { return status_; }
  [[nodiscard]] const std::string& body () const& noexcept { return body_; }
  [[nodiscard]] std::string&& body () && noexcept { return std::move (body_); }
  [[nodiscard]] const std::unordered_map<std::string, std::string>& headers ()
      const noexcept {
    return headers_;
  }

  static RestResponse json (int status, std::string body);
  static RestResponse ok_json (std::string body);
  static RestResponse not_found ();
  static RestResponse method_not_allowed ();
  static RestResponse service_unavailable ();
  static RestResponse redirect (std::string location, bool permanent = false);

 private:
  int status_ {200};
  std::string body_;
  std::unordered_map<std::string, std::string> headers_;
};

using RestCompletion = std::function<void (RestResponse&&)>;

/// Synchronous REST handler (runs on the connection executor unless offloaded).
using RestHandler = std::function<RestResponse (RestRequest&&)>;

/// Asynchronous REST handler; must call `complete` exactly once.
using AsyncRestHandler =
    std::function<void (RestRequest&&, RestCompletion complete)>;

}  // namespace web
