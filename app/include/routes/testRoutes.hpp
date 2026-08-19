#pragma once

#include <rukh/http/ErrorFactory.hpp>
#include <rukh/http/Router.hpp>
#include <rukh/pool/ThreadPool.hpp>

void registerBasicTestRoutes(rukh::http::Router &router, const rukh::http::ErrorFactory &errorFactory,
                             rukh::pool::ThreadPool *threadPool);

void registerCookieTestRoutes(rukh::http::Router &router, const rukh::http::ErrorFactory &errorFactory,
                              rukh::pool::ThreadPool *threadPool);

void registerSessionTestRoutes(rukh::http::Router &router, const rukh::http::ErrorFactory &errorFactory,
                               rukh::pool::ThreadPool *threadPool);

void registerUrlPathTestRoutes(rukh::http::Router &router, const rukh::http::ErrorFactory &errorFactory,
                               rukh::pool::ThreadPool *threadPool);

void registerStreamTestRoutes(rukh::http::Router &router, const rukh::http::ErrorFactory &errorFactory,
                              rukh::pool::ThreadPool *threadPool);

void registerThreadPoolTestRoutes(rukh::http::Router &router, const rukh::http::ErrorFactory &errorFactory,
                                  rukh::pool::ThreadPool *threadPool);

void registerFormTestRoutes(rukh::http::Router &router, const rukh::http::ErrorFactory &errorFactory,
                            rukh::pool::ThreadPool *threadPool);

void registerOrmTestRoutes(rukh::http::Router &router, const rukh::http::ErrorFactory &errorFactory,
                           rukh::pool::ThreadPool *threadPool);

inline void registerAllTestRoutes(rukh::http::Router &router, const rukh::http::ErrorFactory &errorFactory,
                                  rukh::pool::ThreadPool *threadPool) {
  registerBasicTestRoutes(router, errorFactory, threadPool);
  registerCookieTestRoutes(router, errorFactory, threadPool);
  registerSessionTestRoutes(router, errorFactory, threadPool);
  registerUrlPathTestRoutes(router, errorFactory, threadPool);
  registerStreamTestRoutes(router, errorFactory, threadPool);
  registerThreadPoolTestRoutes(router, errorFactory, threadPool);
  registerFormTestRoutes(router, errorFactory, threadPool);
  registerOrmTestRoutes(router, errorFactory, threadPool);
}
