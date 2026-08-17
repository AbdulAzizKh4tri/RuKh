#pragma once

#include <rukh/ErrorFactory.hpp>
#include <rukh/Router.hpp>
#include <rukh/ThreadPool.hpp>

void registerBasicTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                             rukh::ThreadPool *threadPool);

void registerCookieTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                              rukh::ThreadPool *threadPool);

void registerSessionTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                               rukh::ThreadPool *threadPool);

void registerUrlPathTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                               rukh::ThreadPool *threadPool);

void registerStreamTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                              rukh::ThreadPool *threadPool);

void registerThreadPoolTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                                  rukh::ThreadPool *threadPool);

void registerFormTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory, rukh::ThreadPool *threadPool);

void registerOrmTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory, rukh::ThreadPool *threadPool);

inline void registerAllTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                                  rukh::ThreadPool *threadPool) {
  registerBasicTestRoutes(router, errorFactory, threadPool);
  registerCookieTestRoutes(router, errorFactory, threadPool);
  registerSessionTestRoutes(router, errorFactory, threadPool);
  registerUrlPathTestRoutes(router, errorFactory, threadPool);
  registerStreamTestRoutes(router, errorFactory, threadPool);
  registerThreadPoolTestRoutes(router, errorFactory, threadPool);
  registerFormTestRoutes(router, errorFactory, threadPool);
  registerOrmTestRoutes(router, errorFactory, threadPool);
}
