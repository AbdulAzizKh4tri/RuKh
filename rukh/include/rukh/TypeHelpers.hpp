/**
 * @file TypeHelpers.hpp
 * @brief Utility type traits and helpers
 */

#pragma once

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace rukh {

/// Optional type
template <typename T>
concept OptionalT = requires { typename T::value_type; } and std::same_as<T, std::optional<typename T::value_type>>;

/// Field Pointer type: someClass::*
template <class T>
concept FieldPointer = std::is_member_object_pointer_v<std::remove_cvref_t<T>>;

/// Tuple type
template <typename T>
concept Tuple = requires { std::tuple_size<T>::value; };

//=============
template <typename T> struct remove_optional {
  using type = T;
};
template <typename T> struct remove_optional<std::optional<T>> {
  using type = T;
};

/// Strip optional<> if present and get the value type.
template <typename T> using remove_optional_t = typename remove_optional<T>::type;

//=============
/// Useful for checking the type of a variable at compile time with a static_assert.
template <typename... T> inline constexpr bool always_false_v = false;

//=============
template <typename T> struct remove_member_pointer {
  using type = T;
};
template <typename C, typename T> struct remove_member_pointer<T C::*> {
  using type = T;
};

/// Get the type of the field from a fieldPtr (including optional<>)
template <typename T> using get_field_t = typename remove_member_pointer<T>::type;

/// Get the type of the raw field from a fieldPtr (stripping optional<>)
template <typename T> using get_raw_field_t = remove_optional_t<typename remove_member_pointer<T>::type>;

//=============
template <typename T> struct get_class {
  using type = T;
};
template <typename C, typename T> struct get_class<T C::*> {
  using type = C;
};

/// Get the class from the fieldPtr
template <typename T> using get_class_t = typename get_class<T>::type;

//=============
/// Check whether all the fieldPtrs in a given tuple point to an optional<>
template <typename TupleType>
concept AllOptionalFieldPtrs = []<std::size_t... I>(std::index_sequence<I...>) consteval {
  return (... && OptionalT<get_field_t<std::tuple_element_t<I, TupleType>>>);
}(std::make_index_sequence<std::tuple_size_v<TupleType>>{});

//=============
/// Pass strings at compile time via templates
template <std::size_t N> struct FixedString {
  char data[N]{};
  constexpr FixedString(const char (&str)[N]) { std::copy_n(str, N, data); }
  constexpr std::string_view view() const { return {data, N - 1}; }
};

//=============

template <template <typename...> class X, typename TupleType> struct unpack;

template <template <typename...> class X, typename... Ts> struct unpack<X, std::tuple<Ts...>> {
  using type = X<Ts...>;
};

/**
 * @brief unpack a tuple of types into a template
 * example:
 * using typeTuple = std::tuple<int, std::string, double>;
 * using specializedMyClassType = unpack_tuple_t<MyClass, typeTuple>; <=== MyClass<int, std::string, double>
 */
template <template <typename...> class X, typename TupleType>
using unpack_tuple_t = typename unpack<X, TupleType>::type;

//=============
template <typename T, typename TupleType> struct is_in_tuple;

template <typename T, typename... Ts>
struct is_in_tuple<T, std::tuple<Ts...>> : std::disjunction<std::is_same<T, Ts>...> {};

/// Check whether a type T exists in a tuple of types
template <typename T, typename TupleType> inline constexpr bool is_in_tuple_v = is_in_tuple<T, TupleType>::value;
//=============
template <typename T, typename TupleType> struct get_index_of;

template <typename T, typename... Rest>
struct get_index_of<T, std::tuple<T, Rest...>> : std::integral_constant<std::size_t, 0> {};

template <typename T, typename First, typename... Rest>
struct get_index_of<T, std::tuple<First, Rest...>>
    : std::integral_constant<std::size_t, 1 + get_index_of<T, std::tuple<Rest...>>::value> {};

template <typename T> struct get_index_of<T, std::tuple<>> {
  static_assert(sizeof(T) == 0, "Type not found in tuple");
};

// The difference between the found case and not found case is: <std::tuple<HERE, Rest...>>
/// Find the index of a type in a tuple
template <typename T, typename TupleType>
inline constexpr std::size_t get_index_of_v = get_index_of<T, TupleType>::value;

//=============
} // namespace rukh
