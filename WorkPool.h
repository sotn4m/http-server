#pragma once

#include "RestTypes.h"

#include <atomic>
#include <boost/asio.hpp>
#include <cstddef>
#include <functional>
#include <thread>

namespace web {

/// Thread pool for blocking work (database, filesystem, CPU-heavy tasks).
/// Call `stop()` (or destroy) before the process exits; not stopped
/// automatically by `HttpServer::stop()`.
///
/// `max_in_flight_work` limits how many `post_rest_work` jobs may be queued or
/// running at once (`0` = unlimited). When exceeded, callers receive HTTP 503.
class WorkPool {
 public:
  explicit WorkPool (std::size_t thread_count = 0,
                     std::size_t max_in_flight_work = 0)
      : pool_ {thread_count == 0 ? default_thread_count () : thread_count},
        max_in_flight_work_ {max_in_flight_work} {}

  WorkPool (const WorkPool&) = delete;
  WorkPool& operator= (const WorkPool&) = delete;

  ~WorkPool () { stop (); }

  [[nodiscard]] boost::asio::thread_pool& pool () noexcept { return pool_; }

  [[nodiscard]] bool running () const noexcept {
    return !stopped_.load (std::memory_order_acquire);
  }

  [[nodiscard]] std::size_t max_in_flight_work () const noexcept {
    return max_in_flight_work_;
  }

  [[nodiscard]] std::size_t in_flight_work () const noexcept {
    return in_flight_work_.load (std::memory_order_relaxed);
  }

  void stop () {
    bool expected = false;
    if (!stopped_.compare_exchange_strong (expected, true,
                                           std::memory_order_acq_rel)) {
      return;
    }
    pool_.stop ();
    pool_.join ();
  }

  [[nodiscard]] static std::size_t default_thread_count () noexcept {
    const auto hardware = std::thread::hardware_concurrency ();
    return hardware == 0 ? 4u : hardware;
  }

 private:
  friend void post_rest_work (WorkPool& pool,
                              std::function<RestResponse ()> work,
                              RestCompletion complete);

  [[nodiscard]] bool try_acquire_in_flight () noexcept {
    if (max_in_flight_work_ == 0) {
      return true;
    }
    const std::size_t in_flight =
        in_flight_work_.fetch_add (1, std::memory_order_acq_rel) + 1;
    if (in_flight > max_in_flight_work_) {
      in_flight_work_.fetch_sub (1, std::memory_order_acq_rel);
      return false;
    }
    return true;
  }

  void release_in_flight () noexcept {
    if (max_in_flight_work_ > 0) {
      in_flight_work_.fetch_sub (1, std::memory_order_acq_rel);
    }
  }

  boost::asio::thread_pool pool_;
  std::size_t max_in_flight_work_ {0};
  std::atomic<std::size_t> in_flight_work_ {0};
  std::atomic<bool> stopped_ {false};
};

/// Runs `work` on `pool`, then invokes `complete` with the result. Exceptions
/// become HTTP 500 JSON. `complete` is thread-safe (same as `RestCompletion`).
/// If `pool` is stopped or at in-flight capacity, completes with 503/500 JSON.
inline void post_rest_work (WorkPool& pool,
                            std::function<RestResponse ()> work,
                            RestCompletion complete) {
  if (!pool.running ()) {
    complete (RestResponse::json (500, R"({"error":"internal_server_error"})"));
    return;
  }

  if (!pool.try_acquire_in_flight ()) {
    complete (RestResponse::service_unavailable ());
    return;
  }

  boost::asio::post (
      pool.pool (), [&pool, work = std::move (work),
                     complete = std::move (complete)] () mutable {
        try {
          complete (work ());
        } catch (...) {
          complete (
              RestResponse::json (500, R"({"error":"internal_server_error"})"));
        }
        pool.release_in_flight ();
      });
}

}  // namespace web
