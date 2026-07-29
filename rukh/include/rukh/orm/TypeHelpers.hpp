#pragma once

#include <optional>
#include <type_traits>
#include <utility>

template <typename T> struct remove_optional {
  using type = T;
};
template <typename T> struct remove_optional<std::optional<T>> {
  using type = T;
};
template <typename T> using remove_optional_t = typename remove_optional<T>::type;

template <typename Model, auto MemPtr> using field_t = std::remove_cvref_t<decltype(std::declval<Model>().*MemPtr)>;
template <typename Model, auto MemPtr> using raw_field_t = remove_optional_t<field_t<Model, MemPtr>>;
