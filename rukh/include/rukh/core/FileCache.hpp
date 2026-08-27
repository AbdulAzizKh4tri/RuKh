/**
 * @file FileCache.hpp
 * \todo docs
 */
#pragma once

#include <filesystem>
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

  std::shared_ptr<std::vector<unsigned char>> cachedContent;
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

  bool insert(const std::string &key, const CachedFile cf) {
    auto [_, inserted] = map_.insert_or_assign(key, cf);
    return inserted;
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

inline thread_local FileCache tl_file_cache;

} // namespace rukh::core
