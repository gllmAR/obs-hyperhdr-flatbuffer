// SPDX-License-Identifier: GPL-2.0-or-later
// Sink tests against an in-process fake HyperHDR on loopback TCP. Frames are
// compared byte-for-byte with the encoder, which test_core checks against the
// official flatbuffers builder.

#include "hhd_encoder.h"
#include "hhd_sink.h"
#include "test_net.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

using Frame = std::vector<uint8_t>;

constexpr int kW = 8;
constexpr int kH = 8;
constexpr size_t kRgbBytes = kW * kH * 3;

Frame makeRgb(uint8_t seed) {
    Frame rgb(kRgbBytes);
    for (size_t i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<uint8_t>(i + seed);
    return rgb;
}

bool waitUntil(const std::function<bool()>& pred, int timeoutMs) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

hhd::SinkConfig makeConfig(uint16_t port) {
    hhd::SinkConfig cfg;
    cfg.origin = "obs";
    cfg.priority = 150;
    cfg.width = kW;
    cfg.height = kH;
    cfg.maxFps = 0.0;  // unthrottled unless a test sets it
    cfg.endpoint.preferDomainSocket = false;  // TCP only: the domain socket is POSIX
    cfg.endpoint.host = "127.0.0.1";
    cfg.endpoint.port = port;
    return cfg;
}

// Accepts loopback TCP connections and records every complete length-prefixed frame.
// One thread does accept, read and parse, so no per-client threads are needed.
class FakeServer {
public:
    // Port 0 picks a free port; port() reports the one in use.
    explicit FakeServer(uint16_t port) {
        uint16_t bound = port;
        listen_ = testnet::listenLoopback(bound);
        port_ = bound;
        ok_ = listen_ != testnet::kInvalid && testnet::setNonBlocking(listen_);
        thread_ = std::thread([this] { serve(); });
    }

    ~FakeServer() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        testnet::closeSocket(listen_);
    }

    FakeServer(const FakeServer&) = delete;
    FakeServer& operator=(const FakeServer&) = delete;

    bool ok() const { return ok_; }
    uint16_t port() const { return port_; }
    int connections() const { return connections_.load(); }
    void dropClients() { dropAll_ = true; }

    std::vector<Frame> frames() const {
        std::lock_guard<std::mutex> lock(mu_);
        return frames_;
    }

    size_t frameCount() const {
        std::lock_guard<std::mutex> lock(mu_);
        return frames_.size();
    }

    size_t countEqual(const Frame& want) const {
        std::lock_guard<std::mutex> lock(mu_);
        size_t n = 0;
        for (const Frame& f : frames_) n += (f == want) ? 1 : 0;
        return n;
    }

private:
    struct Client {
        testnet::Socket fd;
        Frame buf;
    };

    void splitFrames(Frame& buf) {
        while (buf.size() >= 4) {
            const size_t len = (static_cast<size_t>(buf[0]) << 24) |
                               (static_cast<size_t>(buf[1]) << 16) |
                               (static_cast<size_t>(buf[2]) << 8) | static_cast<size_t>(buf[3]);
            const size_t total = 4 + len;
            if (buf.size() < total) break;
            Frame frame(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(total));
            {
                std::lock_guard<std::mutex> lock(mu_);
                frames_.push_back(std::move(frame));
            }
            buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(total));
        }
    }

    void serve() {
        std::vector<Client> clients;
        while (running_.load()) {
            if (dropAll_.exchange(false)) {
                for (const Client& c : clients) testnet::closeSocket(c.fd);
                clients.clear();
            }
            const testnet::Socket fd = testnet::acceptOne(listen_);
            if (fd != testnet::kInvalid) {
                testnet::setNonBlocking(fd);
                clients.push_back(Client{fd, Frame()});
                ++connections_;
            }
            for (size_t i = 0; i < clients.size();) {
                uint8_t scratch[4096];
                const int n = testnet::recvSome(clients[i].fd, scratch, sizeof(scratch));
                if (n > 0) {
                    clients[i].buf.insert(clients[i].buf.end(), scratch, scratch + n);
                    splitFrames(clients[i].buf);
                    ++i;
                } else if (n == testnet::kWouldBlock) {
                    ++i;
                } else {
                    testnet::closeSocket(clients[i].fd);
                    clients.erase(clients.begin() + static_cast<std::ptrdiff_t>(i));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (const Client& c : clients) testnet::closeSocket(c.fd);
    }

    uint16_t port_ = 0;
    testnet::Socket listen_ = testnet::kInvalid;
    bool ok_ = false;
    std::atomic<bool> running_{true};
    std::atomic<bool> dropAll_{false};
    std::atomic<int> connections_{0};
    mutable std::mutex mu_;
    std::vector<Frame> frames_;
    std::thread thread_;
};

void testSubmitRulesAndRestart() {
    hhd::Sink sink(makeConfig(1));
    const Frame rgb = makeRgb(5);
    CHECK(!sink.submit(nullptr, kRgbBytes));
    CHECK(!sink.submit(rgb.data(), kRgbBytes - 1));
    CHECK(sink.submit(rgb.data(), kRgbBytes));
    CHECK(sink.state() == hhd::SinkState::Idle);
    sink.stop();  // never started: no thread, no Clear
    CHECK(sink.state() == hhd::SinkState::Stopped);
    CHECK(!sink.start());  // a stopped sink does not restart
}

void testDefaultDomainSocketPath() {
    testnet::setEnv("TMPDIR", "/run/user/1000/");
    CHECK(hhd::defaultDomainSocketPath() == "/run/user/1000/hyperhdr-domain");
    testnet::setEnv("TMPDIR", "/");
    CHECK(hhd::defaultDomainSocketPath() == "/hyperhdr-domain");
    testnet::setEnv("TMPDIR", "");
    CHECK(hhd::defaultDomainSocketPath() == "/tmp/hyperhdr-domain");
    testnet::unsetEnv("TMPDIR");
    CHECK(hhd::defaultDomainSocketPath() == "/tmp/hyperhdr-domain");
}

void testRegisterImageClear() {
    FakeServer server(0);
    CHECK(server.ok());

    const Frame rgb = makeRgb(1);
    Frame reg, img, clr;
    CHECK(hhd::encodeRegister(reg, "obs", 150));
    CHECK(hhd::encodeImageRgb(img, rgb.data(), rgb.size(), kW, kH));
    CHECK(hhd::encodeClear(clr, 150));

    hhd::Sink sink(makeConfig(server.port()));
    CHECK(sink.submit(rgb.data(), rgb.size()));
    CHECK(sink.start());
    CHECK(waitUntil([&] { return server.frameCount() >= 2; }, 3000));
    CHECK(waitUntil([&] { return sink.state() == hhd::SinkState::Streaming; }, 1000));

    std::vector<Frame> frames = server.frames();
    CHECK(frames.size() >= 2 && frames[0] == reg);  // Register precedes Image
    CHECK(frames.size() >= 2 && frames[1] == img);

    sink.stop();
    CHECK(sink.state() == hhd::SinkState::Stopped);
    CHECK(waitUntil([&] { return server.frameCount() >= 3; }, 2000));
    frames = server.frames();
    CHECK(frames.size() == 3 && frames[2] == clr);  // Clear is the last frame on stop
}

void testBackoffThenConnect() {
    const uint16_t port = testnet::freePort();
    CHECK(port != 0);
    hhd::Sink sink(makeConfig(port));
    CHECK(sink.start());
    CHECK(waitUntil([&] { return sink.state() == hhd::SinkState::Backoff; }, 2000));
    CHECK(!sink.lastError().empty());

    Frame reg, clr;
    CHECK(hhd::encodeRegister(reg, "obs", 150));
    CHECK(hhd::encodeClear(clr, 150));
    {
        FakeServer server(port);  // HyperHDR appears after the sink started
        CHECK(server.ok());
        CHECK(waitUntil([&] { return server.countEqual(reg) >= 1; }, 3000));
        sink.stop();
        CHECK(waitUntil([&] { return server.countEqual(clr) >= 1; }, 2000));
        const std::vector<Frame> frames = server.frames();
        CHECK(!frames.empty() && frames.front() == reg);
        CHECK(!frames.empty() && frames.back() == clr);
    }
}

void testReRegisterAfterDrop() {
    FakeServer server(0);
    CHECK(server.ok());

    Frame reg, clr;
    CHECK(hhd::encodeRegister(reg, "obs", 150));
    CHECK(hhd::encodeClear(clr, 150));

    hhd::Sink sink(makeConfig(server.port()));
    CHECK(sink.start());
    CHECK(waitUntil([&] { return server.countEqual(reg) >= 1; }, 2000));

    // HyperHDR restarts: its side of the connection goes away.
    server.dropClients();
    const Frame rgb = makeRgb(2);
    // Keep submitting so the sink notices the drop and reconnects.
    CHECK(waitUntil(
        [&] {
            sink.submit(rgb.data(), rgb.size());
            return server.countEqual(reg) >= 2;
        },
        5000));
    CHECK(server.connections() >= 2);

    sink.stop();
    CHECK(waitUntil([&] { return server.countEqual(clr) >= 1; }, 2000));
    const std::vector<Frame> frames = server.frames();
    CHECK(!frames.empty() && frames.back() == clr);
}

void testThrottle() {
    FakeServer server(0);
    CHECK(server.ok());

    hhd::SinkConfig cfg = makeConfig(server.port());
    cfg.maxFps = 10.0;  // one Image per 100 ms
    const Frame rgb = makeRgb(3);
    Frame img, clr;
    CHECK(hhd::encodeImageRgb(img, rgb.data(), rgb.size(), kW, kH));
    CHECK(hhd::encodeClear(clr, 150));

    hhd::Sink sink(cfg);
    CHECK(sink.start());
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(350);
    while (std::chrono::steady_clock::now() < until) {
        sink.submit(rgb.data(), rgb.size());  // ~1 kHz submit rate
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    sink.stop();
    CHECK(waitUntil([&] { return server.countEqual(clr) >= 1; }, 2000));

    const size_t images = server.countEqual(img);
    // Sends at about 0, 100, 200 and 300 ms, so 4. Allow scheduling jitter.
    CHECK(images >= 2 && images <= 5);
}

void testFlipVertical() {
    FakeServer server(0);
    CHECK(server.ok());

    hhd::SinkConfig cfg = makeConfig(server.port());
    cfg.flipVertical = true;
    const Frame rgb = makeRgb(4);
    const size_t row = static_cast<size_t>(kW) * 3;
    Frame flipped(kRgbBytes);
    for (int y = 0; y < kH; ++y) {
        std::memcpy(&flipped[static_cast<size_t>(y) * row],
                    &rgb[static_cast<size_t>(kH - 1 - y) * row], row);
    }
    Frame want, unflipped;
    CHECK(hhd::encodeImageRgb(want, flipped.data(), flipped.size(), kW, kH));
    CHECK(hhd::encodeImageRgb(unflipped, rgb.data(), rgb.size(), kW, kH));

    hhd::Sink sink(cfg);
    CHECK(sink.start());
    CHECK(sink.submit(rgb.data(), rgb.size()));
    CHECK(waitUntil([&] { return server.countEqual(want) >= 1; }, 3000));
    CHECK(server.countEqual(unflipped) == 0);
    sink.stop();
}

}  // namespace

int main() {
    testSubmitRulesAndRestart();
    testDefaultDomainSocketPath();
    testRegisterImageClear();
    testBackoffThenConnect();
    testReRegisterAfterDrop();
    testThrottle();
    testFlipVertical();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("sink tests passed\n");
    return 0;
}
