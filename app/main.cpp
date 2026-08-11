#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <rukh/HttpResponse.hpp>
#include <rukh/HttpServer.hpp>
#include <rukh/Router.hpp>
#include <rukh/ThreadPool.hpp>
#include <rukh/db/Sqlite3Db.hpp>
#include <rukh/logUtils.hpp>
#include <rukh/orm/OrmConfig.hpp>

#include "include/errors.hpp"
#include "include/middlewares.hpp"
#include "include/routes.hpp"
#include "routes/testRoutes.hpp"

// #include "tetherIP.hpp"

using json = nlohmann::json;
using namespace rukh;
int main() {

  int N;
  std::string logging, middleware;
  std::string host = "0.0.0.0";

  std::cout << "Do we want logging? (y/n)" << std::endl;
  std::cin >> logging;
  std::cout << "Do we want middleware? (y/n)" << std::endl;
  std::cin >> middleware;
  std::cout << "How many threads?" << std::endl;
  std::cin >> N;

  configureLog(logging.contains('y'), "");
  SPDLOG_DEBUG("C++ standard: {}", __cplusplus);

  Router router(getErrorFactory());
  if (middleware.contains('y'))
    registerMiddlewares(router, host);

  HttpServer server(getErrorFactory());
  size_t threadPoolSize = N * 2;
  ThreadPool threadPool(threadPoolSize);

  size_t connectionPoolSize = threadPoolSize;

  auto db_path = std::filesystem::path(__FILE__).parent_path() / "test.db";
  db::IDatabase *db = new db::Sqlite3Db(db_path, &threadPool, connectionPoolSize);

  orm::OrmConfig::db = db;

  registerRoutes(router, getErrorFactory(), &threadPool, db);
  registerAllTestRoutes(router, getErrorFactory(), &threadPool, db);

  auto cert_path = std::filesystem::path(__FILE__).parent_path() / "cert.pem";
  auto key_path = std::filesystem::path(__FILE__).parent_path() / "key.pem";
  server.setTlsContext(cert_path, key_path);
  server.setRouter(router);
  server.addListener(host, "8080");
  server.addTlsListener(host, "8443");

  server.run(N);

  return 0;
}
