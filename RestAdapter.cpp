#include "RestAdapter.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace web {
namespace {

constexpr char kHexDigitsUpper[] = "0123456789ABCDEF";

constexpr int hex_to_int (char character) noexcept {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return 10 + character - 'a';
  }
  if (character >= 'A' && character <= 'F') {
    return 10 + character - 'A';
  }
  return -1;
}

constexpr bool is_unreserved (char character) noexcept {
  return (character >= 'A' && character <= 'Z') ||
         (character >= 'a' && character <= 'z') ||
         (character >= '0' && character <= '9') || character == '-' ||
         character == '.' || character == '_' || character == '~';
}

HttpMethod verb_to_method (http::verb verb) {
  switch (verb) {
    case http::verb::get:
      return HttpMethod::GET;
    case http::verb::post:
      return HttpMethod::POST;
    case http::verb::put:
      return HttpMethod::PUT;
    case http::verb::patch:
      return HttpMethod::PATCH;
    case http::verb::delete_:
      return HttpMethod::DELETE_;
    case http::verb::head:
      return HttpMethod::HEAD;
    case http::verb::options:
      return HttpMethod::OPTIONS;
    default:
      return HttpMethod::UNKNOWN;
  }
}

// Query decoding follows x-www-form-urlencoded semantics where '+' means space.
std::string decode_query_value (std::string_view input) {
  std::string output;
  output.reserve (input.size ());
  for (std::size_t index = 0; index < input.size (); ++index) {
    if (input[index] == '%' && index + 2 < input.size ()) {
      const int high = hex_to_int (input[index + 1]);
      const int low = hex_to_int (input[index + 2]);
      if (high >= 0 && low >= 0) {
        output.push_back (
            static_cast<char> (static_cast<unsigned> (high << 4) | low));
        index += 2;
        continue;
      }
    }
    if (input[index] == '+') {
      output.push_back (' ');
      continue;
    }
    output.push_back (input[index]);
  }
  return output;
}

std::string decode_path_segment (std::string_view input) {
  std::string output;
  output.reserve (input.size ());
  for (std::size_t index = 0; index < input.size (); ++index) {
    if (input[index] == '%' && index + 2 < input.size ()) {
      const int high = hex_to_int (input[index + 1]);
      const int low = hex_to_int (input[index + 2]);
      if (high >= 0 && low >= 0) {
        const char decoded =
            static_cast<char> (static_cast<unsigned> (high << 4) | low);
        if (is_unreserved (decoded)) {
          output.push_back (decoded);
        } else {
          output.push_back ('%');
          output.push_back (kHexDigitsUpper[high]);
          output.push_back (kHexDigitsUpper[low]);
        }
        index += 2;
        continue;
      }
    }
    output.push_back (input[index]);
  }
  return output;
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

std::unordered_map<std::string, std::string> parse_query_string (
    std::string_view query) {
  std::unordered_map<std::string, std::string> values;
  while (!query.empty ()) {
    const auto ampersand = query.find ('&');
    const auto pair = query.substr (0, ampersand);
    const auto equals = pair.find ('=');
    const auto key = decode_query_value (pair.substr (0, equals));
    const auto value = equals == std::string_view::npos
                           ? std::string_view {}
                           : pair.substr (equals + 1);
    if (!key.empty ()) {
      values.emplace (key, decode_query_value (value));
    }
    if (ampersand == std::string_view::npos) {
      break;
    }
    query.remove_prefix (ampersand + 1);
  }
  return values;
}

std::pair<std::string, std::unordered_map<std::string, std::string>>
split_target (std::string_view target) {
  const auto query_position = target.find ('?');
  if (query_position == std::string_view::npos) {
    return {std::string {target}, {}};
  }
  return {std::string {target.substr (0, query_position)},
          parse_query_string (target.substr (query_position + 1))};
}

std::string normalize_path (std::string_view raw_path) {
  if (raw_path.empty ()) {
    return "/";
  }

  std::vector<std::string> segments;
  std::size_t start = raw_path.front () == '/' ? 1 : 0;
  while (start <= raw_path.size ()) {
    const auto slash = raw_path.find ('/', start);
    const auto segment = slash == std::string_view::npos
                             ? raw_path.substr (start)
                             : raw_path.substr (start, slash - start);

    if (!segment.empty ()) {
      const auto decoded = decode_path_segment (segment);
      if (decoded == ".") {
        // no-op
      } else if (decoded == "..") {
        if (!segments.empty ()) {
          segments.pop_back ();
        }
      } else {
        segments.push_back (decoded);
      }
    }

    if (slash == std::string_view::npos) {
      break;
    }
    start = slash + 1;
  }

  std::string normalized {"/"};
  for (std::size_t index = 0; index < segments.size (); ++index) {
    if (index > 0) {
      normalized.push_back ('/');
    }
    normalized += segments[index];
  }
  return normalized;
}

std::vector<std::string_view> split_path (std::string_view path) {
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

RestRequest to_rest_request (const http::request<http::string_body>& request,
                             const RequestContext& context) {
  auto [path, query] = split_target (request.target ());
  path = normalize_path (path);

  std::unordered_map<std::string, std::string> headers;
  for (const auto& field : request.base ()) {
    headers.emplace (to_lower_ascii (field.name_string ()),
                     std::string {field.value ()});
  }

  return RestRequest {verb_to_method (request.method ()),
                      std::move (path),
                      request.body (),
                      std::move (headers),
                      std::move (query),
                      {},
                      request.version (),
                      request.keep_alive (),
                      context.request_id,
                      context.remote_address};
}

http::response<http::string_body> to_http_response (
    RestResponse rest_response,
    const http::request<http::string_body>& request) {
  http::response<http::string_body> response {
      static_cast<http::status> (rest_response.status ()), request.version ()};
  response.keep_alive (request.keep_alive ());

  bool has_content_type = false;
  for (const auto& [key, value] : rest_response.headers ()) {
    response.set (key, value);
    if (key == "Content-Type") {
      has_content_type = true;
    }
  }
  if (!has_content_type && !rest_response.body ().empty ()) {
    response.set (http::field::content_type, "text/plain; charset=utf-8");
  }

  const bool has_body = !rest_response.body ().empty ();
  response.body () = std::move (rest_response).body ();
  if (has_body) {
    response.prepare_payload ();
  } else if (response.result () != http::status::no_content) {
    response.content_length (0);
  }
  return response;
}

namespace detail {

bool match_path_pattern (
    std::string_view pattern,
    std::string_view path,
    std::unordered_map<std::string, std::string>& path_params) {
  const auto pattern_segments = split_path (pattern);
  const auto path_segments = split_path (path);
  if (pattern_segments.size () != path_segments.size ()) {
    return false;
  }

  for (std::size_t index = 0; index < pattern_segments.size (); ++index) {
    const auto pattern_segment = pattern_segments[index];
    const auto path_segment = path_segments[index];
    if (pattern_segment.size () >= 2 && pattern_segment.front () == '{' &&
        pattern_segment.back () == '}') {
      path_params.emplace (
          std::string {pattern_segment.substr (1, pattern_segment.size () - 2)},
          std::string {path_segment});
      continue;
    }
    if (pattern_segment != path_segment) {
      return false;
    }
  }
  return true;
}

int route_pattern_score (std::string_view pattern) {
  int score = 0;
  for (const auto segment : split_path (pattern)) {
    if (segment.size () >= 2 && segment.front () == '{' &&
        segment.back () == '}') {
      score += 1;
    } else {
      score += 1000;
    }
  }
  return score;
}

void apply_cors_headers (http::response<http::string_body>& response,
                         const http::request<http::string_body>& request,
                         const CorsConfig& config) {
  if (!config.enabled) {
    return;
  }

  const auto origin = request[http::field::origin];
  if (!origin.empty ()) {
    for (const auto& allowed : config.allowed_origins) {
      if (allowed == origin) {
        response.set (http::field::access_control_allow_origin, origin);
        break;
      }
    }
  }

  response.set (http::field::access_control_allow_methods,
                config.allow_methods);
  response.set (http::field::access_control_allow_headers,
                config.allow_headers);
  if (config.allow_credentials) {
    response.set (http::field::access_control_allow_credentials, "true");
  }
  response.set (http::field::access_control_max_age, config.max_age);
}

}  // namespace detail

}  // namespace web
