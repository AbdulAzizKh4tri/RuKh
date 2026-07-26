#pragma once

#include <string>

namespace rukh::orm {

template <typename FieldT, typename Model> struct Column {
  std::string name;
  FieldT Model::*fieldPtr;
};

} // namespace rukh::orm
