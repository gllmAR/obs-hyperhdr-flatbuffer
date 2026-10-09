// SPDX-License-Identifier: GPL-2.0-or-later
// Unit tests for the core encoder, mailbox and transport. Golden frames
// come from tools/verify_flat.py (official flatbuffers builder).

#include "hhd_encoder.h"
#include "hhd_mailbox.h"
#include "hhd_transport.h"
#include "test_net.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                             \
            ++failures;                                                \
        }                                                              \
    } while (0)

// A peer close is only visible to the non-blocking drain once the kernel has
// processed the FIN, which is asynchronous. Poll briefly instead of assuming
// a single drain sees it.
void waitPeerClosed(hhd::Connection& conn) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    while (conn.isOpen() && std::chrono::steady_clock::now() < deadline) {
        conn.drain();
        if (conn.isOpen()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

std::string toHex(const std::vector<uint8_t>& bytes) {
    static const char digits[] = "0123456789abcdef";
    std::string s;
    s.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        s.push_back(digits[b >> 4]);
        s.push_back(digits[b & 0x0f]);
    }
    return s;
}

const char* kRegisterObs150 =
    "000000340c00000008000c000b00040008000000100000000000000408000c0008000400080000009600000004000000030000006f627300";
const char* kClear150 =
    "000000240c00000008000a0009000400080000000c00000000030600080004000600000096000000";
const char* kImage2x1 =
    "000000500c00000008000c000b00040008000000100000000000000208000a0009000400080000001000000000010a0010000c00080004000a00000001000000020000000400000006000000ff000000ff000000";

void testEncoderGolden() {
    std::vector<uint8_t> out;

    CHECK(hhd::encodeRegister(out, "obs", 150));
    CHECK(toHex(out) == kRegisterObs150);

    CHECK(hhd::encodeClear(out, 150));
    CHECK(toHex(out) == kClear150);

    const uint8_t rgb[] = {255, 0, 0, 0, 255, 0};
    CHECK(hhd::encodeImageRgb(out, rgb, sizeof(rgb), 2, 1));
    CHECK(toHex(out) == kImage2x1);
}

void testEncoderRejects() {
    std::vector<uint8_t> out{1, 2, 3};
    const uint8_t px[] = {0, 0, 0};

    CHECK(!hhd::encodeImageRgb(out, nullptr, 3, 1, 1));
    CHECK(out.empty());
    CHECK(!hhd::encodeImageRgb(out, px, 0, 1, 1));
    CHECK(out.empty());
    CHECK(!hhd::encodeImageRgb(out, px, hhd::kMaxFrameBytes + 1, 1, 1));
    CHECK(out.empty());
}

void testMailbox() {
    hhd::Mailbox box;
    std::vector<uint8_t> got;

    CHECK(!box.take(got));

    const uint8_t first[] = {1, 1, 1};
    const uint8_t second[] = {2, 2, 2};
    box.publish(first, sizeof(first));
    box.publish(second, sizeof(second));  // replaces the untaken frame

    CHECK(box.take(got));
    CHECK(got.size() == 3 && got[0] == 2);
    CHECK(!box.take(got));  // nothing new since the last take
}

#ifndef _WIN32
void testUnixRoundTrip() {
    char path[108];
    std::snprintf(path, sizeof(path), "/tmp/hhd-test-%d.sock", static_cast<int>(getpid()));
    ::unlink(path);

    const int srv = ::socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(srv >= 0);
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    CHECK(std::strlen(path) < sizeof(addr.sun_path));
    std::memcpy(addr.sun_path, path, std::strlen(path));
    CHECK(::bind(srv, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
    CHECK(::listen(srv, 1) == 0);

    hhd::Connection conn;
    CHECK(conn.openUnix(path));
    CHECK(conn.isOpen());

    const int peer = ::accept(srv, nullptr, nullptr);
    CHECK(peer >= 0);

    std::vector<uint8_t> frame;
    CHECK(hhd::encodeClear(frame, 150));
    CHECK(conn.writeAll(frame.data(), frame.size(), 1000));

    std::vector<uint8_t> received(frame.size());
    size_t got = 0;
    while (got < received.size()) {
        const ssize_t n = ::recv(peer, received.data() + got, received.size() - got, 0);
        if (n <= 0) break;
        got += static_cast<size_t>(n);
    }
    CHECK(got == frame.size());
    CHECK(received == frame);

    // Peer closes: drain must notice and mark the connection closed.
    ::close(peer);
    waitPeerClosed(conn);
    CHECK(!conn.isOpen());

    conn.close();
    ::close(srv);
    ::unlink(path);
}
#endif

void testTcpRoundTrip() {
    uint16_t port = 0;
    const testnet::Socket srv = testnet::listenLoopback(port);
    CHECK(srv != testnet::kInvalid);
    CHECK(port != 0);

    hhd::Connection conn;
    CHECK(conn.openTcp("127.0.0.1", port, 1000));
    CHECK(conn.isOpen());

    const testnet::Socket peer = testnet::acceptOne(srv);
    CHECK(peer != testnet::kInvalid);

    std::vector<uint8_t> frame;
    CHECK(hhd::encodeClear(frame, 150));
    CHECK(conn.writeAll(frame.data(), frame.size(), 1000));

    std::vector<uint8_t> received(frame.size());
    CHECK(testnet::recvExact(peer, received.data(), received.size()) == frame.size());
    CHECK(received == frame);

    // Peer closes: drain must notice and mark the connection closed.
    testnet::closeSocket(peer);
    waitPeerClosed(conn);
    CHECK(!conn.isOpen());

    conn.close();
    testnet::closeSocket(srv);
}

void testTcpOpenFailure() {
    hhd::Connection conn;
    CHECK(!conn.openTcp("127.0.0.1", testnet::freePort(), 200));
    CHECK(!conn.isOpen());
    CHECK(!conn.lastError().empty());
}

void testUnixOpenFailure() {
    hhd::Connection conn;
    CHECK(!conn.openUnix("/tmp/hhd-no-such-socket.sock"));
    CHECK(!conn.isOpen());
    CHECK(!conn.lastError().empty());
}

}  // namespace

int main() {
    testEncoderGolden();
    testEncoderRejects();
    testMailbox();
#ifndef _WIN32
    testUnixRoundTrip();
#endif
    testTcpRoundTrip();
    testTcpOpenFailure();
    testUnixOpenFailure();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("all core tests passed\n");
    return EXIT_SUCCESS;
}
