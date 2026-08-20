#pragma once

#include <string_view>

#include <rukh/TypeHelpers.hpp>
#include <rukh/db/DbTypes.hpp>

namespace rukh::orm {

enum class AutoGenerate { OFF, DB_INCREMENT, DEFAULT, DB_NOW, CUSTOM };
/// \todo Rename to UpdateMode maybe
enum class AutoUpdate { OFF, DB_NOW, CUSTOM, LOCKED };

template <typename Model, typename FieldT> struct Column {
  FieldT Model::*const fieldPtr;
  const std::string_view dbName;
  const bool isPrimaryKey = false;
  const bool isIndexed = false;
  const bool isUnique = false;

  const AutoGenerate autoGenerateMode = AutoGenerate::OFF;
  const db::DbValue defaultValue = nullptr;
  db::DbValue (Model::*const customGenerator)() const = nullptr;

  const AutoUpdate autoUpdateMode = AutoUpdate::OFF;
  db::DbValue (Model::*const customUpdator)() const = nullptr;
};

} // namespace rukh::orm
