# Changelog

All notable changes to this project are documented here.

## [Unreleased]

### Fixed

- Drop `<print>` / `std::println` in `Logging.h` and the minimal example so the library builds on GCC 13 and other C++23 toolchains without the C++23 formatting library

## [1.0.1] - 2026-05-23

### Changed

- CMake library target renamed to `http-server` (`HttpServer::http-server`); install headers under `include/http-server/`
- Documentation and examples use submodule path `third_party/http-server`

### Fixed

- Segfault on empty-body responses (e.g. redirects): `to_http_response()` no longer reads `RestResponse` after moving its body
- CI install smoke test checks `include/http-server/` and `libhttp-server.a`
- `HttpServer::stop()` closes the listen acceptor so the bound port is released promptly
- `TestServer` shutdown stops the I/O thread before destroying the server (ephemeral listen port reusable)

## [0.8.0] - 2026-05-23

### Added

- CI builds on **GCC and Clang** (ubuntu-latest matrix)
- Tests for query `+` decoding (space in query, literal `+` in path), case-insensitive `RestRequest::header`, and CORS validation when `enabled=true` with no origins

### Changed

- Routes are indexed by `HttpMethod` so matching scans only routes for the request verb
- Incoming header names are stored lowercase; `RestRequest::header()` does direct map lookup (case-insensitive for callers)
- Query values use `decode_query_value` (`+` → space per `application/x-www-form-urlencoded`); path segments keep `+` literal
- `HttpServer::wait_for_sessions()` waits on a condition variable instead of polling every 5 ms
- Internal `SessionConfig` / `SessionState` replace long `HttpSession` constructor argument lists
- Empty non-204 REST responses set `Content-Length: 0` instead of calling `prepare_payload()` on an empty body
- Keep-alive connections reset read buffer capacity at the start of each request (`clear` + `shrink_to_fit`)
- Request ID RNG uses a `random_device` seed with a steady-clock/thread-id fallback on failure
- Internal adapter header renamed: `RestAdapter.hpp` → `RestAdapter.h`

### Fixed

- `HEAD` fallback to `GET` routing after method-bucketed route storage (no reliance on removed per-route method field)

### Migration

- `RestRouter::set_cors()` now throws if `enabled=true` and `allowed_origins` is empty — configure at least one origin before enabling CORS

## [0.7.0] - 2026-05-23

### Added

- `RestRouter::{put_async, patch_async, delete_async}` — async handler parity for PUT, PATCH, DELETE
- `CorsConfig::allow_credentials` — opt-in with `Access-Control-Allow-Credentials` response header
- `X-Request-ID` response header automatically set on all REST responses for correlation
- CORS preflight now validates origin against `allowed_origins` (403 on mismatch, 404 on unknown path)

### Changed

- Path normalization now follows RFC 3986 (decode only unreserved octets; preserve reserved octets percent-encoded with uppercase hex)
- `HttpSession::sendError` uses the request's negotiated HTTP version instead of hardcoded 1.1
- `resolve_log_fn` returns by value (eliminates dangling reference hazard)
- Route patterns are pre-parsed once at registration, not on every request
- REST response bodies are moved (not copied) into Beast responses

### Fixed

- `WorkPool::stopped_` data race replaced with `std::atomic<bool>`
- `RestResponse::set_header` rejects values containing CR, LF, or NUL (response-splitting guard)
- `RestRouter` throws `std::logic_error` if routes/CORS/logger are mutated after `request_handler()` is called
- Path param injection no longer copies the full `RestRequest` payload

## [0.6.0] - 2026-05-21

### Added

- `ServerConfig::max_active_sessions` — reject new connections with HTTP 503 when exceeded (`0` = unlimited)
- `WorkPool` `max_in_flight_work` — cap queued/running `post_rest_work` jobs; overflow returns 503
- `RestResponse::service_unavailable()`
- Admission control integration tests

## [0.5.0] - 2026-05-21

### Added

- Async `RequestHandler` with `RequestCompletion` and `make_sync_handler`
- `RequestContext` (`request_id`, `remote_address`)
- `Middleware.h` — `chain_middleware`
- `WorkPool` and `post_rest_work` for blocking handlers
- `Logging.h` — injectable `LogFn`
- `HttpServer::active_sessions`, `wait_for_sessions`
- Route specificity, HEAD→GET fallback, `RestResponse::redirect`
- `RestRouter::get_async` / `post_async`, shared route lifetime

### Changed

- CMake package version 0.5.0; consumer docs updated
- Transport and router errors use `LogFn` instead of hard-coded logging only

### Removed

- Unused example static assets (`examples/minimal_server/assets/`)

## [0.4.0]

- Logging hooks, graceful session wait, WorkPool integration

## [0.3.0]

- REST router, CORS, CMake install/export, Catch2 tests

## [0.2.0]

- Library/app split, `ServerConfig`, `RequestHandler` abstraction

### Migration (pre-0.3 sync handlers)

Handlers must use the async signature or `make_sync_handler`:

```cpp
// Before
RequestHandler h = [](http::request<...> req) { return response; };

// After
RequestHandler h = make_sync_handler(
    [](http::request<...> req, const RequestContext&) { return response; });
```
