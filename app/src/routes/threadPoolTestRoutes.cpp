#include <nlohmann/json.hpp>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpResponse.hpp>

#include "routes/testRoutes.hpp"

void registerThreadPoolTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                                  rukh::ThreadPool *threadPool) {
  using namespace rukh;
  using namespace nlohmann;
  // -- Thread pool test routes -------------------------------
  // All live under /tests/pool/*

  // WARNING: BLOCKS THE ENTIRE THREAD FOR 5 SECONDS!
  router.get("/tests/slow", [](const HttpRequest &) -> Task<Response> {
    // simulate slow work -- busy wait for 5 seconds
    auto end = now() + std::chrono::seconds(5);
    while (now() < end) {
    }
    co_return HttpResponse(200, "slow response");
  });

  router.get("/tests/slowpool", [threadPool](const HttpRequest &) -> Task<Response> {
    co_await threadPool->submit([]() {
      auto end = now() + std::chrono::seconds(5);
      while (now() < end) {
      }
      return 0;
    });
    co_return HttpResponse(200, "slow pooled response");
  });

  // GET /tests/pool/basic
  // Submits a task that returns a value, awaits it immediately.
  // Verifies: submit, return value propagation, basic await.
  // Response: { "result": 42 }
  router.get("/tests/pool/basic", [threadPool](const HttpRequest &) -> Task<Response> {
    int result = co_await threadPool->submit([]() { return 42; });
    auto res = HttpResponse(200, json{{"result", result}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // GET /tests/pool/exception
  // Submits a task that throws. Verifies exception propagates correctly
  // to the handler, gets caught by HttpConnection, returns 500.
  router.get("/tests/pool/exception", [threadPool](const HttpRequest &) -> Task<Response> {
    int result = co_await threadPool->submit([]() -> int {
      throw std::runtime_error("pool task exploded");
      return 0;
    });
    (void)result;
    co_return HttpResponse(200, "unreachable");
  });

  // GET /tests/pool/deferred
  // Submits a task, logs that it's been submitted (proving coroutine
  // continued past submit before awaiting), then awaits the result.
  // Verifies: deferred await, return value used in response.
  // Response: { "result": 99, "note": "task was submitted before this line ran" }
  router.get("/tests/pool/deferred", [threadPool](const HttpRequest &) -> Task<Response> {
    auto handle = threadPool->submit([]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      SPDLOG_INFO("TASK DONE");
      return 99;
    });

    co_await threadPool->submit([]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      return;
    });

    SPDLOG_INFO("co_await-ing now");
    int result = co_await handle;
    auto res = HttpResponse(200, json{{"result", result}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // GET /tests/pool/concurrent
  // Fires n tasks simultaneously, awaits all of them.
  // Each task sleeps for 500ms and returns its index.
  // Total wall time should be ~500ms regardless of n (up to pool size).
  // Verifies: actual parallelism.
  // Response: { "results": [0, 1, 2], "note": "should take ~500ms not ~1500ms" }
  router.get("/tests/pool/concurrent", [threadPool](const HttpRequest &request) -> Task<Response> {
    int n = 3;
    auto start = now();
    auto nParam = request.getQueryParam("n");
    if (!nParam.empty())
      n = std::clamp(std::stoi(nParam), 1, 16);

    std::vector<PoolTaskAwaitable<int>> handles;
    handles.reserve(n);

    for (int i = 0; i < n; i++) {
      handles.push_back(threadPool->submit([i]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return i;
      }));
    }

    json results = json::array();
    for (auto &handle : handles)
      results.push_back(co_await handle);

    auto end = now();

    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    auto res = HttpResponse(200, json{{"results", results}, {"time", time}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // GET /tests/pool/forget
  // Fires a task with fireAndForget — no await, no result.
  // Task logs a message server-side after 200ms.
  // Verifies: fire and forget doesn't crash, response returns immediately.
  // Response: { "note": "task is running in background, check server logs" }
  router.get("/tests/pool/forget", [threadPool](const HttpRequest &) -> Task<Response> {
    threadPool->fireAndForget([]() noexcept {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      SPDLOG_INFO("fireAndForget task completed");
    });

    auto res = HttpResponse(200, json{{"note", "task is running in background, check server logs"}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // GET /tests/pool/unawaited
  // Constructs a PoolAwaitable via submit() but never co_awaits it.
  // Response: 200 immediately.
  router.get("/tests/pool/unawaited", [threadPool](const HttpRequest &) -> Task<Response> {
    {
      auto handle = threadPool->submit([]() {
        SPDLOG_INFO("Submitted task running, there should be a warning for not awaiting it");
        return 0;
      });
    }
    co_return HttpResponse(200, "no crash, check logs for unawaited warning");
  });
}
