@page quickstart Quick Start

This page is the shortest path to a working RuKh application.

## Hello, World!

```cpp
// app/main.cpp
#include <rukh/http/ErrorFactory.hpp>
#include <rukh/http/HttpServer.hpp>
#include <rukh/http/Router.hpp>
#include <rukh/core/Task.hpp>

using namespace rukh::core;
using namespace rukh::http;

int main() {
    ErrorFactory errorFactory;
    Router router(errorFactory);

    router.get("/hello", [](const HttpRequest& req) -> Task<Response> {
        co_return HttpResponse(200, "Hello, World!");
    });

    HttpServer server(errorFactory);
    server.setRouter(router);
    server.addListener("localhost", "8080");
    size_t threadCount = 4; // 4 executor threads
    server.run(threadCount);
}
```

### Add to your CMakeLists:

```
target_link_libraries(server PRIVATE 
  rukh::rukh
  ...
)
```

### Test your RuKh app

```text
curl http://localhost:8080/hello
```

**Next Step** - @ref api "API" **or** @ref build "Build"
