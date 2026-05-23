# Using HttpServer in another project

Reusable **CMake library** for async HTTP/1.1 and REST routing. Your service owns routes, JSON, auth, databases, and deployment.

**Target:** `HttpServer::http_server`  
**Requires:** C++23, CMake 3.24+, Boost 1.81+ (`Boost::headers`)

---

## Pin a version

```bash
cd third_party/http_server
git fetch --tags
git checkout v0.8.0
```

---

## Submodule (recommended)

### 1. Add

```bash
git submodule add <REPO_URL> third_party/http_server
git submodule update --init --recursive
```

### 2. CMake

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyService LANGUAGES CXX)

find_package(Boost 1.81 REQUIRED CONFIG COMPONENTS headers)

set(HTTP_SERVER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HTTP_SERVER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HTTP_SERVER_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/http_server)

add_executable(my_service src/main.cpp)
target_compile_features(my_service PRIVATE cxx_std_23)
target_link_libraries(my_service PRIVATE HttpServer::http_server)
```

### 3. Minimal `main.cpp`

```cpp
#include <HttpServer.h>
#include <RestRouter.h>
#include <ServerConfig.h>
#include <WorkPool.h>

#include <boost/asio.hpp>
#include <chrono>
#include <csignal>
#include <iostream>
#include <print>

int main() {
  boost::asio::io_context io;
  web::WorkPool pool;

  web::RestRouter router;
  router.get("/api/health", [](web::RestRequest&&) {
    return web::RestResponse::ok_json(R"({"status":"ok"})");
  });

  web::ServerConfig config{};
  config.host = "0.0.0.0";
  config.port = "8080";

  web::HttpServer server{io.get_executor(), std::move(config),
                         router.request_handler()};

  boost::asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const boost::system::error_code& ec, int) {
    if (ec) return;
    server.stop();
    (void)server.wait_for_sessions(std::chrono::seconds{10});
    pool.stop();
    io.stop();
  });

  server.start();
  io.run();
  return 0;
}
```

Reference: `examples/minimal_server/main.cpp`.

---

## Installed package

```bash
cmake -S third_party/http_server -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix "$HOME/.local"
```

App:

```cmake
find_package(Boost 1.81 REQUIRED CONFIG COMPONENTS headers)
find_package(HttpServer CONFIG REQUIRED)
target_link_libraries(my_service PRIVATE HttpServer::http_server)
```

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
```

---

## Public headers

| Header | Purpose |
|--------|---------|
| `HttpServer.h` | Acceptor, `start`, `stop`, `wait_for_sessions`, `active_sessions` |
| `ServerConfig.h` | Listen address, limits, timeouts, `log_fn` |
| `RequestHandler.h` | Async handler, `make_sync_handler` |
| `RequestContext.h` | Per-request `request_id`, `remote_address` |
| `RestRouter.h` | Route registration, `request_handler()` |
| `RestTypes.h` | `RestRequest`, `RestResponse`, handler aliases |
| `Middleware.h` | `chain_middleware` |
| `WorkPool.h` | `post_rest_work` for blocking handlers |
| `Logging.h` | `LogFn`, `null_logger`, `default_stderr_logger` |
| `CorsConfig.h` | Optional browser CORS |

Internal (not installed as API): `RestAdapter.h` — Beast conversion.

---

## Patterns

### Async / database work

```cpp
web::WorkPool pool{8, 64};  // threads, max in-flight jobs (0 = unlimited)

router.get_async("/api/item", [&pool](web::RestRequest&& req,
                                      web::RestCompletion complete) {
  web::post_rest_work(pool, [&] { return load_item(req); }, std::move(complete));
});
```

Call `pool.stop()` during shutdown (see `main` above). When the pool is saturated, clients receive `503` with `{"error":"service_unavailable"}`.

### Overload limits

```cpp
config.max_active_sessions = 1000;  // 0 = unlimited
```

When exceeded, new TCP connections get HTTP **503** and close (see `active_sessions()` for monitoring).

### Middleware

```cpp
#include <Middleware.h>

web::Middleware log = [](web::RequestHandler next) {
  return [next = std::move(next)](auto req, web::RequestContext ctx,
                                  web::RequestCompletion done) {
    next(std::move(req), std::move(ctx), std::move(done));
  };
};
const web::RequestHandler handler =
    web::chain_middleware({log}, router.request_handler());
```

### Logging

```cpp
config.log_fn = web::null_logger();           // tests
router.set_log(web::default_stderr_logger()); // optional; default if empty
```

### Low-level HTTP (no `RestRouter`)

Implement `RequestHandler` directly or wrap with `make_sync_handler`. You must call `complete` exactly once.

---

## Contracts

| Rule | Detail |
|------|--------|
| `complete` | Invoke once per request; safe from `WorkPool` threads |
| `RestRouter` lifetime | `request_handler()` holds shared route state; router object may be destroyed if handler is kept |
| `CorsConfig` | `enabled=true` requires non-empty `allowed_origins`; wildcard `*` cannot be combined with `allow_credentials=true` |
| `WorkPool` | Not stopped by `HttpServer::stop()`; call `pool.stop()` before exit |
| `max_active_sessions` | Admission control at accept; `0` disables the cap |
| `WorkPool` second arg | `max_in_flight_work`; `0` disables the cap |
| `io_context` | Typically one thread running `io.run()`; scale via `WorkPool` |
| Exceptions in handlers | Mapped to HTTP 500 where caught |

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| Boost not found | `-DBoost_DIR=<dir with BoostConfig.cmake>` |
| HttpServer not found | `-DCMAKE_PREFIX_PATH=<install prefix>` |
| Examples build in CI | Keep `HTTP_SERVER_BUILD_EXAMPLES OFF` in consumers |

---

## Verify this repository

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DHTTP_SERVER_BUILD_TESTS=ON -DHTTP_SERVER_BUILD_EXAMPLES=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/minimal_server &
curl -s http://127.0.0.1:6969/api/health
```
