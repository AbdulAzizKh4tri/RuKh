@page build Building RuKh

## Dependencies

- Linux (epoll + io_uring required)
- C++23 compiler (GCC 13+ or Clang 17+)
- CMake 3.28+
- OpenSSL
- [spdlog](https://github.com/gabime/spdlog)
- [nlohmann/json](https://github.com/nlohmann/json)
- [liburing](https://github.com/axboe/liburing) - built automatically via CMake `ExternalProject`. You don't need to install it separately.

## Steps

### Install dependencies
```bash
sudo apt update && sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  libssl-dev \
  zlib1g-dev \
  libbrotli-dev \
  libspdlog-dev \
  nlohmann-json3-dev
```

### Debug Build
```bash
cmake -S . -B build/debug\
  -DCMAKE_C_COMPILER= YOUR_C_COMPILER \
  -DCMAKE_CXX_COMPILER= YOUR_CXX_COMPILER \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

The `-DCMAKE_BUILD_TYPE` is the only important setting here. The others can be omitted.

```bash
cmake --build build/debug -j$(nproc)
```

## TLS

Generate a self-signed certificate for local development:

```bash
openssl req -x509 -newkey rsa:2048 -keyout app/key.pem -out app/cert.pem -days 365 -nodes
```

TLS certificate paths are configured with @rhttp{HttpServer::setTlsContext, setTlsContext} and is required for @rhttp{HttpServer::addTlsListener, addTlsListener}

