/**
 * @file HttpTypes.hpp
 * @brief HTTP Response, Handler, Middleware types
 */

#pragma once

#include <functional>

#include <rukh/core/Task.hpp>
#include <rukh/http/HttpFileResponse.hpp>
#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpResponse.hpp>
#include <rukh/http/HttpStreamResponse.hpp>

namespace rukh::http {

/// varaint containing both HttpResponse types
using Response = std::variant<HttpResponse, HttpStreamResponse, HttpFileResponse>;

/// The function to be set when defining routes in the `Router`
using Handler = std::move_only_function<core::Task<Response>(HttpRequest &)>;

/// next item in chain
using Next = std::move_only_function<core::Task<Response>()>;

/**
 * Middleware
 *
 * Example
 * @code
 * class CorsMiddleware {
 * ...
 * core::Task<Response> CorsMiddleware::operator()(const HttpRequest &request, Next next){...}
 * ...
 * }
 * @endcode
 *
 */
using Middleware = std::move_only_function<core::Task<Response>(HttpRequest &, Next)>;
} // namespace rukh::http
