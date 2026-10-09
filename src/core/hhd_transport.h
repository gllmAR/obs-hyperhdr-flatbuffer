// SPDX-License-Identifier: GPL-2.0-or-later
// POSIX socket transport (Unix domain or TCP). The Winsock path is not written yet.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace hhd {

class Connection {
public:
    Connection() = default;
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    bool openUnix(const std::string& path);
    bool openTcp(const std::string& host, int port, int timeoutMs);
    void close();
    bool isOpen() const { return fd_ >= 0; }

    // Writes every byte or fails. Fails after timeoutMs of waiting for buffer space.
    bool writeAll(const uint8_t* data, size_t size, int timeoutMs);
    // Reads and discards pending replies without blocking.
    void drain();

    const std::string& lastError() const { return lastError_; }

private:
    bool finishOpen(int fd);

    int fd_ = -1;
    std::string lastError_;
};

}  // namespace hhd
