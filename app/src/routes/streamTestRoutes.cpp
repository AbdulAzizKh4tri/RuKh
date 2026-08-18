#include <nlohmann/json.hpp>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpResponse.hpp>
#include <rukh/core/Task.hpp>

#include "routes/testRoutes.hpp"

void registerStreamTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                              rukh::ThreadPool *threadPool) {
  using namespace rukh;
  using namespace nlohmann;
  // -- Chunked / streaming response test routes -------------------------------
  // All live under /tests/stream/*

  // GET /tests/stream/basic
  // A handful of small named chunks. Assembled body: "Hello, World!"
  // The canonical "does streaming work at all" check.
  router.get("/tests/stream/basic", [](const HttpRequest &) -> core::Task<Response> {
    co_return HttpStreamResponse(200, "text/plain", [i = 0]() mutable -> core::Task<std::optional<std::string>> {
      static constexpr std::array chunks = {"Hello", ", ", "World", "!"};
      if (i >= 4)
        co_return std::nullopt;
      co_return std::string(chunks[i++]);
    });
  });

  // GET /tests/stream/single
  // Exactly one chunk, then done. Assembled body: "hello"
  router.get("/tests/stream/single", [](const HttpRequest &) -> core::Task<Response> {
    co_return HttpStreamResponse(200, [done = false]() mutable -> core::Task<std::optional<std::string>> {
      if (done)
        co_return std::nullopt;
      done = true;
      co_return std::string("hello");
    });
  });

  // GET /tests/stream/empty
  // Returns nullopt on the very first call -- terminal chunk immediately.
  // Assembled body is empty, status still 200.
  router.get("/tests/stream/empty", [](const HttpRequest &) -> core::Task<Response> {
    co_return HttpStreamResponse(200, []() -> core::Task<std::optional<std::string>> { co_return std::nullopt; });
  });

  // GET /tests/stream/count/<n>
  // Streams exactly n chunks: "chunk-1", "chunk-2", ..., "chunk-n".
  // n=0  -> empty body (same as /empty)
  // n<0  -> clamped to 0
  // non-numeric n -> std::stoi throws before the HttpStreamResponse is
  //   constructed, generateResponse() catches it -> plain 500 JSON response.
  router.get("/tests/stream/count/<n>", [](const HttpRequest &request) -> core::Task<Response> {
    int n = std::stoi(request.getPathParam("n"));
    if (n < 0)
      n = 0;
    co_return HttpStreamResponse(200, [i = 0, n]() mutable -> core::Task<std::optional<std::string>> {
      if (i >= n)
        co_return std::nullopt;
      co_return "chunk-" + std::to_string(++i);
    });
  });

  // GET /tests/stream/throw
  // Emits two chunks successfully, then the lambda throws.
  // The error is caught, "Internal Server Error: ..." is appended as a final
  // chunk, the stream is terminated, and the connection closes cleanly.
  // Status is still 200 (headers already sent).
  // Assembled body: "chunk-1chunk-2Internal Server Error: Deliberate stream
  // error"
  router.get("/tests/stream/throw", [](const HttpRequest &) -> core::Task<Response> {
    co_return HttpStreamResponse(200, [i = 0]() mutable -> core::Task<std::optional<std::string>> {
      if (i == 2)
        throw std::runtime_error("Deliberate stream error");
      co_return "chunk-" + std::to_string(++i);
    });
  });

  // POST /tests/stream/echo
  // Reads the request body and streams it back in 4-byte chunks.
  // Empty body -> empty stream (terminal chunk only).
  // Mirrors Content-Type if provided.
  router.post("/tests/stream/echo", [](HttpRequest &request) -> core::Task<Response> {
    std::string body = co_await request.consumeBody();
    auto ct = request.getHeader("Content-Type");
    auto res = HttpStreamResponse(
        200, [offset = size_t(0), body = std::move(body)]() mutable -> core::Task<std::optional<std::string>> {
          if (offset >= body.size())
            co_return std::nullopt;
          auto len = std::min(size_t(4), body.size() - offset);
          auto chunk = body.substr(offset, len);
          offset += len;
          co_return chunk;
        });
    if (not ct.empty())
      res.headers.setHeaderLower("content-type", std::string(ct));
    co_return res;
  });
}
