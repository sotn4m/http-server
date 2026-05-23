#pragma once

#include <string>
#include <vector>

namespace web {

/// Optional CORS policy for `RestRouter` (browser clients). Set
/// `allowed_origins` explicitly in production; nothing is allowed until
/// configured.
struct CorsConfig {
  bool enabled {false};
  std::vector<std::string> allowed_origins {};
  std::string allow_methods {"GET, POST, PUT, PATCH, DELETE, OPTIONS"};
  std::string allow_headers {"Content-Type, Authorization"};
  bool allow_credentials {false};
  std::string max_age {"86400"};
};

}  // namespace web
