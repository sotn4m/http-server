#include "RestTypes.h"

#include <Json.h>

#include <cctype>
#include <stdexcept>
#include <string_view>

namespace web {
namespace {

bool has_invalid_header_character (std::string_view value) {
  return value.find ('\r') != std::string_view::npos ||
         value.find ('\n') != std::string_view::npos ||
         value.find ('\0') != std::string_view::npos;
}

std::string to_lower_ascii (std::string_view input) {
  std::string output;
  output.reserve (input.size ());
  for (const char character : input) {
    output.push_back (static_cast<char> (
        std::tolower (static_cast<unsigned char> (character))));
  }
  return output;
}

std::string error_payload (std::string_view error_code) {
  return json::serialize (json::object ({{"error", std::string {error_code}}}));
}

}  // namespace

RestRequest::RestRequest (
    HttpMethod method,
    std::string path,
    std::string body,
    std::unordered_map<std::string, std::string> headers,
    std::unordered_map<std::string, std::string> query,
    std::unordered_map<std::string, std::string> path_params,
    unsigned http_version,
    bool keep_alive,
    std::string request_id,
    std::string remote_address)
    : method_ {method},
      path_ {std::move (path)},
      body_ {std::move (body)},
      headers_ {std::move (headers)},
      query_ {std::move (query)},
      path_params_ {std::move (path_params)},
      http_version_ {http_version},
      keep_alive_ {keep_alive},
      request_id_ {std::move (request_id)},
      remote_address_ {std::move (remote_address)} {}

std::optional<std::string> RestRequest::header (std::string_view key) const {
  const auto iterator = headers_.find (to_lower_ascii (key));
  if (iterator != headers_.end ()) {
    return iterator->second;
  }
  return std::nullopt;
}

std::optional<std::string> RestRequest::query_param (
    std::string_view key) const {
  for (const auto& [name, value] : query_) {
    if (name == key) {
      return value;
    }
  }
  return std::nullopt;
}

std::optional<std::string> RestRequest::path_param (
    std::string_view key) const {
  for (const auto& [name, value] : path_params_) {
    if (name == key) {
      return value;
    }
  }
  return std::nullopt;
}

RestResponse::RestResponse (int status) : status_ {status} {}

void RestResponse::set_body (std::string body) {
  body_ = std::move (body);
}

void RestResponse::set_header (std::string key, std::string value) {
  if (has_invalid_header_character (key) ||
      has_invalid_header_character (value)) {
    throw std::invalid_argument (
        "Response header name/value must not contain CR, LF, or NUL");
  }
  headers_[std::move (key)] = std::move (value);
}

RestResponse RestResponse::json (int status, std::string body) {
  RestResponse response {status};
  response.set_header ("Content-Type", "application/json; charset=utf-8");
  response.set_body (std::move (body));
  return response;
}

RestResponse RestResponse::ok_json (std::string body) {
  return json (200, std::move (body));
}

RestResponse RestResponse::not_found () {
  return json (404, error_payload ("not_found"));
}

RestResponse RestResponse::method_not_allowed () {
  return json (405, error_payload ("method_not_allowed"));
}

RestResponse RestResponse::service_unavailable () {
  return json (503, error_payload ("service_unavailable"));
}

RestResponse RestResponse::redirect (std::string location, bool permanent) {
  RestResponse response {permanent ? 301 : 302};
  response.set_header ("Location", std::move (location));
  return response;
}

}  // namespace web
