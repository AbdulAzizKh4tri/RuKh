#pragma once

#include <optional>

template <typename T>
concept OptionalT = requires { typename T::value_type; } && std::same_as<T, std::optional<typename T::value_type>>;
