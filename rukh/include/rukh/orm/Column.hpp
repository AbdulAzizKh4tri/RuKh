#pragma once

#include <string>

#include <rukh/db/DbTypes.hpp>

namespace rukh::orm {

enum struct AutoGenerate { OFF, DB_INCREMENT, DEFAULT, DB_NOW, CUSTOM };
enum struct AutoUpdate { OFF, DB_SIDE, CUSTOM };

template <typename Model, typename FieldT> struct Column {
  FieldT Model::*fieldPtr;
  std::string dbName;
  bool isPrimaryKey = false;
  AutoGenerate autoGenerateMode = AutoGenerate::OFF;
  db::DbValue defaultValue = nullptr;
  db::DbValue (Model::*customGenerator)() const = nullptr;

  AutoUpdate autoUpdateMode = AutoUpdate::OFF;
  db::DbValue (Model::*customUpdator)() const = nullptr;
};

} // namespace rukh::orm
