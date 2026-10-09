// SPDX-License-Identifier: GPL-2.0-or-later

#include "hhd_transport.h"

#include <cerrno>
#include <chrono>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace hhd {

Connection::~Connection() { close(); }

bool Connection::finishOpen(int fd) {
#ifdef SO_NOSIGPIPE
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    // Non-blocking after connect: writes are bounded by writeAll, reads by drain.
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    fd_ = fd;
    lastError_.clear();
    return true;
}

bool Connection::openUnix(const std::string& path) {
    close();
    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        lastError_ = "socket path too long: " + path;
        return false;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        lastError_ = std::string("socket(AF_UNIX): ") + std::strerror(errno);
        return false;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        lastError_ = "connect " + path + ": " + std::strerror(errno);
        ::close(fd);
        return false;
    }
    return finishOpen(fd);
}

bool Connection::openTcp(const std::string& host, int port, int timeoutMs) {
    close();

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string service = std::to_string(port);
    struct addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &res) != 0 || res == nullptr) {
        lastError_ = "cannot resolve " + host;
        return false;
    }

    int fd = -1;
    for (struct addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        lastError_ = "connect " + host + ":" + service + ": " + std::strerror(errno);
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    if (fd < 0) return false;

    // Frames are small and latency-critical; do not let Nagle batch them.
    int on = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    return finishOpen(fd);
}

void Connection::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool Connection::writeAll(const uint8_t* data, size_t size, int timeoutMs) {
    if (fd_ < 0) return false;

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    size_t sent = 0;
    while (sent < size) {
        const ssize_t n = ::send(fd_, data + sent, size - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Wait for buffer room rather than abandon a half-written frame,
            // which would desynchronise the length framing.
            const auto left = std::chrono::duration_cast<std::chrono::microseconds>(
                deadline - std::chrono::steady_clock::now());
            if (left.count() > 0) {
                fd_set wf;
                FD_ZERO(&wf);
                FD_SET(fd_, &wf);
                struct timeval tv;
                tv.tv_sec = static_cast<time_t>(left.count() / 1000000);
                tv.tv_usec = static_cast<suseconds_t>(left.count() % 1000000);
                if (::select(fd_ + 1, nullptr, &wf, nullptr, &tv) > 0) continue;
            }
            lastError_ = "write timed out";
            close();
            return false;
        }
        lastError_ = std::string("write: ") + std::strerror(errno);
        close();
        return false;
    }
    return true;
}

void Connection::drain() {
    if (fd_ < 0) return;

    uint8_t scratch[512];
    for (;;) {
        const ssize_t n = ::recv(fd_, scratch, sizeof(scratch), 0);
        if (n > 0) continue;
        if (n == 0) {
            lastError_ = "peer closed the connection";
            close();
            return;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        lastError_ = std::string("read: ") + std::strerror(errno);
        close();
        return;
    }
}

}  // namespace hhd
