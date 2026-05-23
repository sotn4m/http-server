#pragma once

#include "RequestHandler.h"

#include <functional>
#include <initializer_list>
#include <span>

namespace web {

/// Wraps an inner `RequestHandler`. Receives `next` and returns the handler
/// that should run outward-in (first middleware in the span runs first).
using Middleware = std::function<RequestHandler (RequestHandler next)>;

/// Composes middleware around `inner`. Empty `middleware` returns `inner`.
[[nodiscard]] inline RequestHandler chain_middleware (
    std::span<const Middleware> middleware,
    RequestHandler inner) {
  RequestHandler chained = std::move (inner);
  for (auto iterator = middleware.rbegin (); iterator != middleware.rend ();
       ++iterator) {
    chained = (*iterator) (std::move (chained));
  }
  return chained;
}

[[nodiscard]] inline RequestHandler chain_middleware (
    std::initializer_list<Middleware> middleware,
    RequestHandler inner) {
  return chain_middleware (std::span<const Middleware> {middleware},
                           std::move (inner));
}

}  // namespace web
