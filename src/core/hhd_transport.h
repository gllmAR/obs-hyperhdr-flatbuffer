// SPDX-License-Identifier: GPL-2.0-or-later
// Socket transport. POSIX (Unix domain or TCP) is in hhd_transport.cpp; Winsock TCP
// is in hhd_transport_win.cpp. CMake compiles exactly one of them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace hhd {

#ifdef _WIN32
using NativeSocket = std::uintptr_t;  // SOCKET
inline constexpr NativeSocket kInvalidSocket = ~static_cast<NativeSocket>(0);
#else
using NativeSocket = int;
inline constexpr NativeSocket kInvalidSocket = -1;
#endif

class Connection {
public:
    Connection() = default;
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    bool openUnix(const std::string& path);
    bool openTcp(const std::string& host, int port, int timeoutMs);
    void close();
    bool isOpen() const { return fd_ != kInvalidSocket; }

    // Writes every byte or fails. Fails after timeoutMs of waiting for buffer space.
    bool writeAll(const uint8_t* data, size_t size, int timeoutMs);
    // Reads and discards pending replies without blocking.
    void drain();

    const std::string& lastError() const { return lastError_; }

private:
    bool finishOpen(NativeSocket fd);

    NativeSocket fd_ = kInvalidSocket;
    std::string lastError_;
};

}  // namespace hhd
