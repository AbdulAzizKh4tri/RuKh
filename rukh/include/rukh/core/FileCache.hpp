/**
 * @file FileCache.hpp
 * \todo docs
 */
#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

#include <unistd.h>

namespace rukh::core {

struct CachedFile {
  int fd = -1;
  uintmax_t size = 0;
  std::filesystem::file_time_type mtime;
  std::string etag;
  std::string mime;
  std::filesystem::path resolvedPath;
};

class FileCache {
public:
  FileCache() = default;
  ~FileCache() {
    for (auto &[key, entry] : map_)
      if (entry.fd != -1)
        ::close(entry.fd);
  }
  FileCache(const FileCache &) = delete;
  FileCache &operator=(const FileCache &) = delete;
  FileCache(FileCache &&) = delete;
  FileCache &operator=(FileCache &&) = delete;

  std::optional<CachedFile> get(const std::string &key) {
    if (auto it = map_.find(key); it != map_.end())
      return it->second;
    return std::nullopt;
  };

  std::expected<CachedFile, int> getOrInsert(const std::string &key,
                                             const std::function<std::expected<CachedFile, int>()> &populate) {
    if (auto it = map_.find(key); it != map_.end())
      return it->second;

    if (auto it = map_.find(key); it != map_.end())
      return it->second;

    auto result = populate();
    if (not result)
      return std::unexpected(result.error());

    auto [it, ok] = map_.emplace(key, std::move(*result));
    return it->second;
  }

  void evict(const std::string &key) {
    if (auto it = map_.find(key); it != map_.end()) {
      if (it->second.fd != -1)
        ::close(it->second.fd);
      map_.erase(it);
    }
  }

  size_t size() const { return map_.size(); }

private:
  std::unordered_map<std::string, CachedFile> map_;
};

} // namespace rukh::core
