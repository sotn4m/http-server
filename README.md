# HttpServer

Async HTTP/REST library (C++23, Boost.Asio/Beast) for backend services.

**Version:** 1.0.1 — pin `v1.0.1` (or your tag) when using as a submodule.

## Requirements

- C++23, CMake 3.24+, Boost 1.81+ (CMake config)

## Build library

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Target: `HttpServer::http-server`.

With tests and example:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DHTTP_SERVER_BUILD_TESTS=ON -DHTTP_SERVER_BUILD_EXAMPLES=ON
cmake --build build -j && ctest --test-dir build --output-on-failure
```

## Use in a new service

See **[docs/CONSUMING.md](docs/CONSUMING.md)** for submodule CMake, `main` template, `WorkPool`, middleware, and install layout.

Quick link:

```cmake
set(HTTP_SERVER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HTTP_SERVER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HTTP_SERVER_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/http-server)
target_link_libraries(my_service PRIVATE HttpServer::http-server)
```

## Install (optional)

```bash
cmake --install build --prefix "$HOME/.local"
# Consumer: find_package(HttpServer CONFIG REQUIRED)
```

## Example

```bash
cmake -S . -B build -DHTTP_SERVER_BUILD_EXAMPLES=ON
cmake --build build --target minimal_server
./build/minimal_server
curl http://127.0.0.1:6969/api/health
```

## Overload protection

- `ServerConfig::max_active_sessions` — excess connections get HTTP 503 (`0` = off)
- `WorkPool{threads, max_in_flight}` — caps `post_rest_work` backlog (`0` = off)

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

## License

MIT — see [LICENSE](LICENSE).
