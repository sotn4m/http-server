#pragma once

#include <string>

namespace web {

/// Per-connection metadata attached to each HTTP request before routing.
struct RequestContext {
  std::string request_id;
  std::string remote_address;
};

}  // namespace web
