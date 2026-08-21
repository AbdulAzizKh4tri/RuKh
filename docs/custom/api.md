@page api API

# The Server Object

Let's start with the most barebones example:

```cpp
HttpServer server(yourErrorFactory);
server.setRouter(yourRouter);

server.setTlsContext(yourCertFilePath, yourKeyFilePath);
server.addTlsListener("0.0.0.0", "8443");

server.addListener("0.0.0.0", "8080");
server.run();
```

@rhttp{ErrorFactory} helps format and customize error responses. The default constructor for it comes with a generic json formatter.<br>
We'll look at @rhttp{Router} in the next section.

The listener methods are self explanatory. `TLS` stuff is entirely optional.

The @rhttp{HttpServer::run, run(size_t N)} method is what actually starts the server.

See @rhttp{HttpServer}, @rhttp{ErrorFactory}, @rhttp{Router}.

---
# Routing

```cpp
router.get("/path", handler);
router.post("/path", handler);
router.put("/path", handler);
router.patch("/path", handler);
router.delete_("/path", handler);
```

Handlers have the signature:

```cpp
[](const HttpRequest& req) -> Task<Response> {
    co_return HttpResponse(200, "body");
}
```

`Response` is `variant<HttpResponse, HttpStreamResponse>`.

See @rhttp{Router}, @rhttp{Response}, @rhttp{HttpResponse}, @rhttp{HttpStreamResponse}, @rc{Task}.

## Path Parameters

```cpp
router.get("/users/<id>", [](const HttpRequest& req) -> Task<Response> {
    auto id = req.getPathParam("id");
    co_return HttpResponse(200, "User: " + id);
});
```

## Wildcards

```cpp
router.get("/files/*",  handler); // matches one segment
router.get("/files/**", handler); // matches any depth
```

---
# Request

## Request Body

```cpp
const string body = co_await req.consumeBody();
const nlohmann::json json = co_await req.jsonBody();
const unordered_map<string, vector<string>> form = co_await req.getFormData();
BodyStream bodyStream = req.bodyStream();
```

See @rreq{consumeBody}, @rreq{jsonBody}, @rreq{getFormData}, @rreq{bodyStream}.

## Path Parameters

```cpp
const string name = req.getPathParam("name", "default value for name");
```

See @rreq{getPathParam}.

## Query Parameters

```cpp
const string name = req.getQueryParam("name");
const vector<string> tags = req.getQueryParams("tag");
const vector<pair<string, string>> all  = req.getAllQueryParams();
```

See @rreq{getQueryParam}, @rreq{getQueryParams}, @rreq{getAllQueryParams}.

## Headers

```cpp
string ct = request.getHeader("Content-Type");
vector<string> &acceptVector = request.getHeaders("Accept");
```

See @rreq{getHeader}, @rreq{getHeaders}.

## Cookies

```cpp
optional<string> cookieValue = req.getCookie("name");
vector<pair<string,string>> cookieValues = req.getCookies();
```

**Note:** Cookies work differently in requests and responses.

See @rreq{getCookie}, @rreq{getCookies}.

## Attributes

Attributes allow us to attach arbitrary data to a request. 
This can be useful - for example - if you have already consumed the request body, but a middleware in the chain needs it.

```cpp
req.setAttribute("key", "value");
```
**Note:** `key` in @rreq{setAttribute} is case-sensitive.

---
# Response

## The 'Normal' Response

```cpp
HttpResponse res(200);
HttpResponse res(200, "bodyText");
HttpResponse res(200, "contentType", "bodyText");
```

See @rhttp{HttpResponse}.

## Streaming Response

```cpp
HttpStreamResponse(200);
HttpStreamResponse(200, [i = 0]() mutable -> Task<optional<string>> {
    if (i >= 3) co_return nullopt;   // signals end of stream
    co_return "chunk-" + to_string(++i);
});
HttpStreamResponse(200, "contentType", NextChunkFn lambda);
```

Note that the lambda is `mutable`. <br>
Returning `nullopt` from the lambda signals the end of the stream.<br>
I do plan on adding a `co_yield`-ing generator type in place of @rc{Task} for streaming responses, but it's not a priority right now.

See @rhttp{HttpStreamResponse}, @rhttp{NextChunkFn}, @rhttp{HttpStreamResponse::setChunked}.

## Response Headers and Cookies

Response headers and cookies are handled by the @rhttp{HeaderStore} and @rhttp{CookieStore} respectively.

---
# Static Files

Static file serving can be done with the help of @rmw{StaticMiddleware}.

**Note:** Static Middleware must be the first middleware after CORS, it has it's own compression and caching logic. 
It also short-circuits the middleware chain if a matching static file is found.

See @rmw{StaticMiddleware}, @rmw{StaticConfig}.

---
# Middleware

```cpp
router.use([](HttpRequest& req, Next next) -> Task<Response> {
    // pre-processing
    auto res = co_await next();
    // post-processing
    co_return res;
});
```

Middleware runs in registration order. `next()` advances to the next middleware or the terminal handler.

See @rhttp{Middleware}, @rhttp{Next}, @rmw{CorsMiddleware}, @rmw{StaticMiddleware}, @rmw{CompressionMiddleware}, @rmw{CacheControlMiddleware}, @rmw{SessionMiddleware}.

---

# Sessions

Sessions aren't loaded until the first @rreq{getSession()} call. Calling it again is a no-op.

The API is quite self explanatory so here's an example:

```cpp
auto session = co_await request.getSession();
session->set(key, value);
auto val = session->get(key);
for (const auto &[k, v] : session->getAll()) {
  body[k] = v;
}
bool exists = session->has(key);
session->remove(key);
session->invalidate();
```

See @rhttp{Session}, @rhttp{SessionHandle}, @rhttp{ISessionStore}.

---

# Database

See @rdb{IDatabase}, @rdb{ITransaction}, @rdb{ScopedTransaction}. 

# ORM and Relations

Intentionally undocumented due to incomplete implementation.

\todo Document ORM in api.md
