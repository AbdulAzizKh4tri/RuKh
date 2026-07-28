#pragma once

#include <string>

namespace rukh::orm {

template <typename Model, typename FieldT> struct Column {
  FieldT Model::*fieldPtr;
  std::string name;
};

} // namespace rukh::orm
