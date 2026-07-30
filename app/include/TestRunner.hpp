#pragma once

#include <expected>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <rukh/Task.hpp>
#include <rukh/db/DbTypes.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace testutil {

struct TestStepResult {
  std::string name;
  bool passed;
  std::string detail;
};

class TestRunner {
public:
  // Runs one named step. Step failures (thrown exceptions) are caught here so one
  // failing step never aborts the rest of the suite.
  template <typename Fn> rukh::Task<void> run(std::string name, Fn &&step) {
    try {
      co_await step();
      results_.push_back({std::move(name), true, ""});
    } catch (const std::exception &e) {
      results_.push_back({std::move(name), false, e.what()});
    } catch (...) {
      results_.push_back({std::move(name), false, "unknown exception"});
    }
  }

  bool allPassed() const {
    for (auto &r : results_)
      if (not r.passed)
        return false;
    return true;
  }

  json toJson() const {
    json steps = json::array();
    for (auto &r : results_)
      steps.push_back({{"name", r.name}, {"passed", r.passed}, {"detail", r.detail}});
    return json{{"allPassed", allPassed()}, {"steps", steps}};
  }

private:
  std::vector<TestStepResult> results_;
};

// Throws on failure; use inside a step lambda passed to TestRunner::run.
inline void expect(std::expected<rukh::db::QueryResult, rukh::db::DatabaseError> cond, const std::string &msg) {

  if (not cond) {
    SPDLOG_ERROR("{}", cond.error().message);
    throw std::runtime_error(msg);
  }
}

inline void expect(bool cond, const std::string &msg) {
  if (not cond)
    throw std::runtime_error(msg);
}

// Unwraps std::expected<T, DatabaseError>, throwing with context on failure.
template <typename T> T unwrap(std::expected<T, rukh::db::DatabaseError> result, const std::string &context) {
  if (not result)
    throw std::runtime_error(context + " failed: " + result.error().message);
  return std::move(*result);
}

} // namespace testutil
