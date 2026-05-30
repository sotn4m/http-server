cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

project(HttpServer VERSION 1.0.1 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(FetchContent)

set(CMAKE_CXX_EXTENSIONS OFF)

# Helps tools (clang-tidy, clangd) match the build (flags, includes).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(HTTP_SERVER_TOPLEVEL TRUE)
else()
  set(HTTP_SERVER_TOPLEVEL FALSE)
endif()

option(HTTP_SERVER_BUILD_EXAMPLES "Build example applications" OFF)
option(HTTP_SERVER_BUILD_TESTS "Build and register integration tests" OFF)
option(HTTP_SERVER_INSTALL
  "Generate install() rules for find_package(HttpServer)"
  ${HTTP_SERVER_TOPLEVEL})

find_package(Boost 1.81 REQUIRED CONFIG COMPONENTS headers)

FetchContent_Declare(
  Json
  GIT_REPOSITORY https://github.com/sotn4m/json.git
  GIT_TAG ac610c988e42ed5be0bfda4f1e90d0f9c1061d41
)
FetchContent_MakeAvailable(Json)
FetchContent_GetProperties(Json)

set(HTTP_SERVER_BOOST_TARGET "")
if(TARGET Boost::headers)
  set(HTTP_SERVER_BOOST_TARGET Boost::headers)
elseif(TARGET Boost::boost)
  set(HTTP_SERVER_BOOST_TARGET Boost::boost)
else()
  message(FATAL_ERROR
    "Boost was found, but no usable imported target is available. "
    "Expected Boost::headers or Boost::boost. "
    "Ensure Boost >= 1.81 provides CMake package targets and set Boost_DIR "
    "to the directory containing BoostConfig.cmake."
  )
endif()

add_library(http-server
  HttpServer.cpp
  HttpSession.cpp
  RestAdapter.cpp
  RestRouter.cpp
  RestTypes.cpp
  "${json_SOURCE_DIR}/lexer/Lexer.cpp"
  "${json_SOURCE_DIR}/parser/Parser.cpp"
  "${json_SOURCE_DIR}/serializer/Serializer.cpp"
)

add_library(HttpServer::http-server ALIAS http-server)

target_compile_features(http-server PUBLIC cxx_std_23)
target_link_libraries(http-server
  PUBLIC
    ${HTTP_SERVER_BOOST_TARGET}
)

target_compile_options(http-server PRIVATE
  $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wall -Wextra -Wpedantic -Wshadow>
)

target_include_directories(http-server
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/http-server>
  PRIVATE
    "${json_SOURCE_DIR}"
)

set(HTTP_SERVER_PUBLIC_HEADERS
  HttpServer.h
  RequestHandler.h
  RequestContext.h
  Middleware.h
  Logging.h
  WorkPool.h
  ServerConfig.h
  RestTypes.h
  RestRouter.h
  CorsConfig.h
)

set_target_properties(http-server PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  CXX_EXTENSIONS OFF
  PUBLIC_HEADER "${HTTP_SERVER_PUBLIC_HEADERS}"
)

if(HTTP_SERVER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(HTTP_SERVER_BUILD_EXAMPLES)
  add_executable(minimal_server examples/minimal_server/main.cpp)
  target_compile_features(minimal_server PRIVATE cxx_std_23)
  set_target_properties(minimal_server PROPERTIES CXX_EXTENSIONS OFF)
  target_link_libraries(minimal_server PRIVATE HttpServer::http-server)
endif()

if(NOT HTTP_SERVER_INSTALL)
  return()
endif()

install(TARGETS http-server
  EXPORT HttpServerTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/http-server
)

install(EXPORT HttpServerTargets
  FILE HttpServerTargets.cmake
  NAMESPACE HttpServer::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HttpServer
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/HttpServerConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/HttpServerConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HttpServer
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/HttpServerConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/HttpServerConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/HttpServerConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HttpServer
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/HttpServer
)
