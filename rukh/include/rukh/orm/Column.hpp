#pragma once

#include <string>

namespace rukh::orm {

template <typename Model, typename FieldT> struct Column {
  FieldT Model::*fieldPtr;
  std::string dbName;
  bool isPrimaryKey = false;
};

} // namespace rukh::orm
