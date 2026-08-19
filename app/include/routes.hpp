#pragma once

#include <rukh/ErrorFactory.hpp>
#include <rukh/Router.hpp>
#include <rukh/pool/ThreadPool.hpp>

void registerRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory, rukh::pool::ThreadPool *threadPool);
