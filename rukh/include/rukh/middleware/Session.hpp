#pragma once

#include <string>
#include <unordered_map>

namespace rukh {

class Session {
public:
  std::optional<std::string> get(const std::string &key) const {
    auto it = data_.find(key);
    if (it == data_.end()) {
      return std::nullopt;
    }
    return it->second;
  };

  void set(const std::string &key, const std::string &value) {
    data_[key] = value;
    isDirty_ = true;
  };

  void remove(const std::string &key) {
    data_.erase(key);
    isDirty_ = true;
  };

  const std::unordered_map<std::string, std::string> &getAll() const { return data_; }

  bool has(const std::string &key) const { return data_.find(key) != data_.end(); };

  void invalidate() { isInvalidated_ = true; }

  void markLoaded() { isNew_ = false; }

  bool isNew() const { return isNew_; };
  bool isDirty() const { return isDirty_; };
  bool isInvalidated() const { return isInvalidated_; };

private:
  std::unordered_map<std::string, std::string> data_;
  bool isNew_ = true;
  bool isDirty_ = false;
  bool isInvalidated_ = false;
};
} // namespace rukh
