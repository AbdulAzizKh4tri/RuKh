#pragma once

#include <rukh/ErrorFactory.hpp>
#include <rukh/Router.hpp>
#include <rukh/ThreadPool.hpp>
#include <rukh/db/IDatabase.hpp>

void registerBasicTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory, rukh::ThreadPool *threadPool,
                             rukh::db::IDatabase *db);

void registerCookieTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                              rukh::ThreadPool *threadPool, rukh::db::IDatabase *db);

void registerSessionTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                               rukh::ThreadPool *threadPool, rukh::db::IDatabase *db);

void registerUrlPathTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                               rukh::ThreadPool *threadPool, rukh::db::IDatabase *db);

void registerStreamTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                              rukh::ThreadPool *threadPool, rukh::db::IDatabase *db);

void registerThreadPoolTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                                  rukh::ThreadPool *threadPool, rukh::db::IDatabase *db);

void registerFormTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory, rukh::ThreadPool *threadPool,
                            rukh::db::IDatabase *db);

void registerOrmTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory, rukh::ThreadPool *threadPool,
                           rukh::db::IDatabase *db);

inline void registerAllTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                                  rukh::ThreadPool *threadPool, rukh::db::IDatabase *db) {
  registerBasicTestRoutes(router, errorFactory, threadPool, db);
  registerCookieTestRoutes(router, errorFactory, threadPool, db);
  registerSessionTestRoutes(router, errorFactory, threadPool, db);
  registerUrlPathTestRoutes(router, errorFactory, threadPool, db);
  registerStreamTestRoutes(router, errorFactory, threadPool, db);
  registerThreadPoolTestRoutes(router, errorFactory, threadPool, db);
  registerFormTestRoutes(router, errorFactory, threadPool, db);
  registerOrmTestRoutes(router, errorFactory, threadPool, db);
}
