#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

namespace rukh::core {

struct CachedFileMmap {
  void *mmappedData = nullptr;
  size_t size = 0;
  int fd = -1;

  ~CachedFileMmap() {
    if (mmappedData && mmappedData != MAP_FAILED) {
      ::munmap(mmappedData, size);
    }
    if (fd != -1) {
      ::close(fd);
    }
  }
};

class MmapCache {
private:
  // Maps a file path to its shared cache mapping
  std::unordered_map<std::string, std::shared_ptr<CachedFileMmap>> cache_;

public:
  std::shared_ptr<CachedFileMmap> get(const std::string &path) {
    auto it = cache_.find(path);
    if (it != cache_.end()) {
      return it->second;
    }

    // Cache miss: Open and map the file
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
      return nullptr;

    struct stat st;
    if (::fstat(fd, &st) < 0) {
      ::close(fd);
      return nullptr;
    }

    // Optional optimization: For tiny files, you could choose to heap allocate
    // instead of mmap, but keeping mmap simple for now.
    void *mmapped_data = ::mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (mmapped_data == MAP_FAILED) {
      ::close(fd);
      return nullptr;
    }

    auto cached_file = std::make_shared<CachedFileMmap>();
    cached_file->mmappedData = mmapped_data;
    cached_file->size = st.st_size;
    cached_file->fd = fd; // Keep fd open to pin the file descriptor safely

    // In production, you would add an LRU eviction size limit check here
    cache_[path] = cached_file;
    return cached_file;
  }
};

inline thread_local MmapCache tl_mmap_cache;

} // namespace rukh::core
