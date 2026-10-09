// SPDX-License-Identifier: GPL-2.0-or-later
// Winsock TCP transport. Unix domain sockets do not exist here (v1 scope).

#include "hhd_transport.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstring>
#include <string>

namespace hhd {
namespace {

// WSAStartup once per process. The matching WSACleanup is left to process exit.
bool ensureWinsock() {
    static const bool ready = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ready;
}

std::string socketErrorText(int code) {
    char* text = nullptr;
    const DWORD n = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&text), 0, nullptr);
    std::string message = (n != 0 && text != nullptr) ? text : "winsock error";
    if (text != nullptr) LocalFree(text);
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' ||
                                message.back() == ' ' || message.back() == '.')) {
        message.pop_back();
    }
    return message + " (" + std::to_string(code) + ")";
}

SOCKET asSocket(NativeSocket fd) { return static_cast<SOCKET>(fd); }
NativeSocket fromSocket(SOCKET s) { return static_cast<NativeSocket>(s); }

// Waits up to timeoutMs for a non-blocking connect to finish. Winsock ignores
// SO_SNDTIMEO for connect(), so the bound has to be enforced here. Returns an
// empty string on success, otherwise the failure text.
std::string awaitConnect(SOCKET s, int timeoutMs) {
    fd_set wf;
    fd_set ef;
    FD_ZERO(&wf);
    FD_ZERO(&ef);
    FD_SET(s, &wf);
    FD_SET(s, &ef);  // Windows reports a failed non-blocking connect as an exception.
    timeval tv;
    tv.tv_sec = static_cast<long>(timeoutMs / 1000);
    tv.tv_usec = static_cast<long>((timeoutMs % 1000) * 1000);

    const int ready = ::select(0, nullptr, &wf, &ef, &tv);
    if (ready == 0) return "connect timed out";
    if (ready < 0) return "select: " + socketErrorText(WSAGetLastError());

    int soError = 0;
    int len = static_cast<int>(sizeof(soError));
    if (::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len) != 0) {
        return "getsockopt: " + socketErrorText(WSAGetLastError());
    }
    return soError == 0 ? std::string() : socketErrorText(soError);
}

}  // namespace

Connection::~Connection() { close(); }

bool Connection::finishOpen(NativeSocket fd) {
    // Non-blocking after connect: writes are bounded by writeAll, reads by drain.
    u_long nonBlocking = 1;
    if (::ioctlsocket(asSocket(fd), FIONBIO, &nonBlocking) != 0) {
        lastError_ = "ioctlsocket: " + socketErrorText(WSAGetLastError());
        ::closesocket(asSocket(fd));
        return false;
    }
    fd_ = fd;
    lastError_.clear();
    return true;
}

bool Connection::openUnix(const std::string& /*path*/) {
    close();
    lastError_ = "Unix domain sockets are not available on Windows";
    return false;
}

bool Connection::openTcp(const std::string& host, int port, int timeoutMs) {
    close();
    if (!ensureWinsock()) {
        lastError_ = "WSAStartup failed";
        return false;
    }

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

    SOCKET s = INVALID_SOCKET;
    for (struct addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET) {
            lastError_ = "socket: " + socketErrorText(WSAGetLastError());
            continue;
        }

        // Non-blocking connect, bounded by awaitConnect. A blocking connect to an
        // unresponsive peer can otherwise hold the sink far past its timeout.
        u_long nonBlocking = 1;
        if (::ioctlsocket(s, FIONBIO, &nonBlocking) != 0) {
            lastError_ = "ioctlsocket: " + socketErrorText(WSAGetLastError());
            ::closesocket(s);
            s = INVALID_SOCKET;
            continue;
        }

        // SO_SNDTIMEO bounds later sends (writeAll enforces its own deadline too).
        const DWORD ms = static_cast<DWORD>(timeoutMs);
        ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));

        std::string failure;
        if (::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) != 0) {
            const int err = WSAGetLastError();
            failure = (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS)
                          ? awaitConnect(s, timeoutMs)
                          : socketErrorText(err);
        }
        if (failure.empty()) break;
        lastError_ = "connect " + host + ":" + service + ": " + failure;
        ::closesocket(s);
        s = INVALID_SOCKET;
    }
    ::freeaddrinfo(res);
    if (s == INVALID_SOCKET) return false;

    // Frames are small and latency-critical; do not let Nagle batch them.
    const BOOL on = TRUE;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
    return finishOpen(fromSocket(s));
}

void Connection::close() {
    if (fd_ != kInvalidSocket) {
        ::closesocket(asSocket(fd_));
        fd_ = kInvalidSocket;
    }
}

bool Connection::writeAll(const uint8_t* data, size_t size, int timeoutMs) {
    if (fd_ == kInvalidSocket) return false;
    const SOCKET s = asSocket(fd_);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    size_t sent = 0;
    while (sent < size) {
        const int n = ::send(s, reinterpret_cast<const char*>(data + sent),
                             static_cast<int>(size - sent), 0);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) {
            lastError_ = "write: connection closed";
            close();
            return false;
        }
        const int err = WSAGetLastError();
        if (err == WSAEINTR) continue;
        if (err == WSAEWOULDBLOCK) {
            // Wait for buffer room rather than abandon a half-written frame,
            // which would desynchronise the length framing.
            const auto left = std::chrono::duration_cast<std::chrono::microseconds>(
                deadline - std::chrono::steady_clock::now());
            if (left.count() > 0) {
                fd_set wf;
                FD_ZERO(&wf);
                FD_SET(s, &wf);
                timeval tv;
                tv.tv_sec = static_cast<long>(left.count() / 1000000);
                tv.tv_usec = static_cast<long>(left.count() % 1000000);
                if (::select(0, nullptr, &wf, nullptr, &tv) > 0) continue;
            }
            lastError_ = "write timed out";
            close();
            return false;
        }
        lastError_ = "write: " + socketErrorText(err);
        close();
        return false;
    }
    return true;
}

void Connection::drain() {
    if (fd_ == kInvalidSocket) return;
    const SOCKET s = asSocket(fd_);

    char scratch[512];
    for (;;) {
        const int n = ::recv(s, scratch, static_cast<int>(sizeof(scratch)), 0);
        if (n > 0) continue;
        if (n == 0) {
            lastError_ = "peer closed the connection";
            close();
            return;
        }
        const int err = WSAGetLastError();
        if (err == WSAEINTR) continue;
        if (err == WSAEWOULDBLOCK) return;
        lastError_ = "read: " + socketErrorText(err);
        close();
        return;
    }
}

}  // namespace hhd
