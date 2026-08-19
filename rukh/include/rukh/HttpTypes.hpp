/**
 * @file HttpTypes.hpp
 * @brief HTTP Response, Handler, Middleware types
 */

#pragma once

#include <functional>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpResponse.hpp>
#include <rukh/HttpStreamResponse.hpp>
#include <rukh/core/Task.hpp>

namespace rukh {

/// varaint containing both HttpResponse types
using Response = std::variant<HttpResponse, HttpStreamResponse>;

/// The function to be set when defining routes in the @c Router
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
} // namespace rukh
