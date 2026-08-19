#pragma once

#include <rukh/http/ErrorFactory.hpp>
#include <rukh/http/Router.hpp>
#include <rukh/pool/ThreadPool.hpp>

void registerRoutes(rukh::http::Router &router, const rukh::http::ErrorFactory &errorFactory, rukh::pool::ThreadPool *threadPool);
