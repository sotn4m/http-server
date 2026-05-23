include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(http_server_integration_tests
  integration_tests.cpp
  rest_router_tests.cpp
  async_and_middleware_tests.cpp
  admission_control_tests.cpp
)

target_compile_features(http_server_integration_tests PRIVATE cxx_std_23)
set_target_properties(http_server_integration_tests PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(http_server_integration_tests
  PRIVATE
    HttpServer::http_server
    Catch2::Catch2WithMain
)

target_include_directories(http_server_integration_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

include(CTest)
include(Catch)
catch_discover_tests(http_server_integration_tests)
