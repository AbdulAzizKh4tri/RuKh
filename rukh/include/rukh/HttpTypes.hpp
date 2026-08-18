#pragma once

#include <functional>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpResponse.hpp>
#include <rukh/HttpStreamResponse.hpp>
#include <rukh/core/Task.hpp>

namespace rukh {

using Response = std::variant<HttpResponse, HttpStreamResponse>;

using Handler = std::move_only_function<core::Task<Response>(HttpRequest &)>;

using Next = std::move_only_function<core::Task<Response>()>;
using Middleware = std::move_only_function<core::Task<Response>(HttpRequest &, Next)>;
} // namespace rukh
