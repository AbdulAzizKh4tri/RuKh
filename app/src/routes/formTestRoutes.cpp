#include <nlohmann/json.hpp>

#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpResponse.hpp>
#include <rukh/http/MultipartParser.hpp>
#include <rukh/core/AsyncFileWriter.hpp>
#include <rukh/core/Task.hpp>

#include "routes/testRoutes.hpp"

// TODO: Add tests for these
void registerFormTestRoutes(rukh::http::Router &router, const rukh::http::ErrorFactory &errorFactory,
                            rukh::pool::ThreadPool *threadPool) {
  using namespace rukh;
  using namespace rukh::http;
  using namespace nlohmann;

  // POST /tests/forms/urlencoded
  // Accepts URL-encoded form data, returns it as JSON.
  // Response: { "username": "alice", "password": "secret" }
  router.post("/tests/forms/urlencoded", [](HttpRequest &request) -> core::Task<Response> {
    auto formData = co_await request.getFormData();
    auto res =
        HttpResponse(200, json{{"username", formData["username"][0]}, {"password", formData["password"][0]}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // POST /tests/forms/urlencoded/checkboxes
  // Accepts URL-encoded form data, returns it as JSON.
  // Response: { "username": "alice", "password": "secret", "terms": ["1", "2", "3"] }
  router.post("/tests/forms/urlencoded/checkboxes", [](HttpRequest &request) -> core::Task<Response> {
    auto formData = co_await request.getFormData();
    auto terms = formData["terms"];
    auto res = HttpResponse(
        200,
        json{{"username", formData["username"][0]}, {"password", formData["password"][0]}, {"terms", terms}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // POST /tests/forms/json
  // Accepts JSON form data, returns it as JSON.
  // Response: { "username": "alice", "password": "secret" }
  router.post("/tests/forms/json", [](HttpRequest &request) -> core::Task<Response> {
    auto body = co_await request.jsonBody();
    auto res = HttpResponse(200, json{{"username", body["username"]}, {"password", body["password"]}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // POST /tests/forms/multipart
  // Accepts multipart form data, returns it as JSON.
  // Response: { "username": "alice", "password": "secret" }
  router.post("/tests/forms/multipart", [](HttpRequest &request) -> core::Task<Response> {
    MultipartParser mp(request);
    std::string username, password;
    std::vector<std::string> terms;
    mp.onField("username", [&username](std::string v) -> core::Task<void> {
      username = v;
      co_return;
    });
    mp.storeFieldValue("password", password);
    mp.storeFieldValues("terms", terms);

    std::optional<core::AsyncFileWriter> writerOpt = core::AsyncFileWriter::open("./app/public/test.bin");
    if (not writerOpt)
      throw std::runtime_error("File issue");
    mp.onFile("file", [&writerOpt](std::span<unsigned char> data) -> core::Task<void> {
      co_await writerOpt->writeChunk(std::string_view(reinterpret_cast<char *>(data.data()), data.size()));
    });

    co_await mp.go();

    HttpResponse res(200, json{{"username", username}, {"password", password}, {"terms", terms}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });
}
