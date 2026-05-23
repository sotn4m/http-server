#pragma once

#include "CorsConfig.h"
#include "RequestContext.h"
#include "RestTypes.h"

#include <boost/beast/http.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

namespace web {

namespace beast = boost::beast;
namespace http = beast::http;

RestRequest to_rest_request (const http::request<http::string_body>& request,
                             const RequestContext& context);

http::response<http::string_body> to_http_response (
    RestResponse rest_response,
    const http::request<http::string_body>& request);

namespace detail {

bool match_path_pattern (
    std::string_view pattern,
    std::string_view path,
    std::unordered_map<std::string, std::string>& path_params);

/// Higher score means a more specific (preferred) route pattern.
[[nodiscard]] int route_pattern_score (std::string_view pattern);

void apply_cors_headers (http::response<http::string_body>& response,
                         const http::request<http::string_body>& request,
                         const CorsConfig& config);

}  // namespace detail

}  // namespace web
