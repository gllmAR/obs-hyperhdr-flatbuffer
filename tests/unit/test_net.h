// SPDX-License-Identifier: GPL-2.0-or-later
// Loopback TCP helpers for the unit tests, with POSIX and Winsock bodies.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace testnet {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalid = INVALID_SOCKET;
#else
using Socket = int;
constexpr Socket kInvalid = -1;
#endif

// recvSome returns the byte count, 0 on peer close, or one of these.
constexpr int kWouldBlock = -1;
constexpr int kRecvError = -2;

inline void closeSocket(Socket s) {
    if (s == kInvalid) return;
#ifdef _WIN32
    ::closesocket(s);
#else
    ::close(s);
#endif
}

inline bool setNonBlocking(Socket s) {
#ifdef _WIN32
    u_long mode = 1;
    return ::ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    return ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK) == 0;
#endif
}

// Binds 127.0.0.1 and listens. Port 0 picks a free port; the chosen port is written back.
inline Socket listenLoopback(uint16_t& port) {
#ifdef _WIN32
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!started) return kInvalid;
#endif
    const Socket s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == kInvalid) return kInvalid;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    socklen_t len = static_cast<socklen_t>(sizeof(addr));
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), len) != 0 || ::listen(s, 4) != 0 ||
        ::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        closeSocket(s);
        return kInvalid;
    }
    port = ntohs(addr.sin_port);
    return s;
}

// A port that nothing listens on right now. Returns 0 if none could be found.
inline uint16_t freePort() {
    uint16_t port = 0;
    closeSocket(listenLoopback(port));
    return port;
}

// Returns kInvalid when no connection is pending on a non-blocking listener.
inline Socket acceptOne(Socket listener) { return ::accept(listener, nullptr, nullptr); }

inline int recvSome(Socket s, uint8_t* buf, size_t size) {
#ifdef _WIN32
    const int n = ::recv(s, reinterpret_cast<char*>(buf), static_cast<int>(size), 0);
    if (n < 0) return WSAGetLastError() == WSAEWOULDBLOCK ? kWouldBlock : kRecvError;
    return n;
#else
    const ssize_t n = ::recv(s, buf, size, 0);
    if (n < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? kWouldBlock
                                                                           : kRecvError;
    }
    return static_cast<int>(n);
#endif
}

// Reads exactly size bytes from a blocking socket. Returns the count actually read.
inline size_t recvExact(Socket s, uint8_t* buf, size_t size) {
    size_t got = 0;
    while (got < size) {
        const int n = recvSome(s, buf + got, size - got);
        if (n <= 0) break;
        got += static_cast<size_t>(n);
    }
    return got;
}

inline void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    ::setenv(name, value, 1);
#endif
}

inline void unsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

}  // namespace testnet
