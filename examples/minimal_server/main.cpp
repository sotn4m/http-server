#include <HttpServer.h>
#include <RestRouter.h>
#include <ServerConfig.h>

#include <boost/asio.hpp>
#include <chrono>
#include <csignal>
#include <iostream>

namespace net = boost::asio;

int main () {
  try {
    net::io_context io_context;

    web::RestRouter router {};
    router.get ("/api/health", [] (web::RestRequest&& /*request*/) {
      return web::RestResponse::ok_json (R"({"status":"ok"})");
    });

    web::ServerConfig config {};
    web::HttpServer server {io_context.get_executor (), std::move (config),
                            router.request_handler ()};

    const auto bound = server.local_endpoint ();
    std::cout << "Listening on " << bound.address ().to_string () << ':'
              << bound.port () << '\n';

    net::signal_set signals (io_context, SIGINT, SIGTERM);
    signals.async_wait (
        [&server, &io_context] (const boost::system::error_code& ec,
                                int /*signo*/) {
          if (ec) {
            return;
          }
          server.stop ();
          (void)server.wait_for_sessions (std::chrono::seconds {5});
          io_context.stop ();
        });

    server.start ();
    io_context.run ();
  } catch (const std::exception& exception) {
    std::cerr << exception.what () << '\n';
    return 1;
  }

  return 0;
}
