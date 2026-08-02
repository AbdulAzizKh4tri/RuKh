#pragma once

#include <optional>
#include <type_traits>
#include <utility>

namespace rukh {

template <typename T>
concept OptionalT = requires { typename T::value_type; } and std::same_as<T, std::optional<typename T::value_type>>;

template <typename T> struct remove_optional {
  using type = T;
};
template <typename T> struct remove_optional<std::optional<T>> {
  using type = T;
};
template <typename T> using remove_optional_t = typename remove_optional<T>::type;

template <typename Model, auto MemPtr> using field_t = std::remove_cvref_t<decltype(std::declval<Model>().*MemPtr)>;
template <typename Model, auto MemPtr> using raw_field_t = remove_optional_t<field_t<Model, MemPtr>>;

template <typename T> inline constexpr bool always_false_v = false;

template <typename T> struct remove_member_pointer {
  using type = T;
};
template <typename C, typename T> struct remove_member_pointer<T C::*> {
  using type = T;
};
template <typename T> using remove_member_pointer_t = typename remove_member_pointer<T>::type;

template <typename Tuple>
concept AllOptionalFieldPtrs = []<std::size_t... I>(std::index_sequence<I...>) consteval {
  return (... && OptionalT<remove_member_pointer_t<std::tuple_element_t<I, Tuple>>>);
}(std::make_index_sequence<std::tuple_size_v<Tuple>>{});

} // namespace rukh
