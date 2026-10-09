// SPDX-License-Identifier: GPL-2.0-or-later

#include "hhd_sink.h"

#include "hhd_encoder.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace hhd {
namespace {

constexpr int kBackoffStartMs = 250;
constexpr int kBackoffCapMs = 2000;
// Worst-case stop: one in-flight connect or write (300 ms), plus the bounded
// teardown reconnect and Clear (100 ms each) when the link is down. Register is
// skipped when a stop arrives during connect, so the bound stays near 500 ms.
constexpr int kConnectTimeoutMs = 300;
constexpr int kWriteTimeoutMs = 300;
constexpr int kStopConnectTimeoutMs = 100;
constexpr int kStopWriteTimeoutMs = 100;
constexpr int kIdleTickMs = 4;
constexpr double kMaxFpsCap = 60.0;

int clampInt(int value, int lo, int hi) { return std::max(lo, std::min(hi, value)); }

}  // namespace

std::string defaultDomainSocketPath() {
    const char* tmp = std::getenv("TMPDIR");
    std::string dir = (tmp != nullptr && *tmp != '\0') ? tmp : "/tmp";
    // Strip trailing slashes so "/run/user/1000/" and "/" both join cleanly.
    while (!dir.empty() && dir.back() == '/') dir.pop_back();
    return dir + "/hyperhdr-domain";
}

Sink::Sink(SinkConfig cfg) : cfg_(std::move(cfg)) {
    cfg_.width = clampInt(cfg_.width, 8, 256);
    cfg_.height = clampInt(cfg_.height, 8, 256);
    cfg_.maxFps = std::max(0.0, std::min(cfg_.maxFps, kMaxFpsCap));
    expectedBytes_ = static_cast<size_t>(cfg_.width) * static_cast<size_t>(cfg_.height) * 3;
    intervalSec_ = cfg_.maxFps > 0.0 ? 1.0 / cfg_.maxFps : 0.0;
}

Sink::~Sink() { stop(); }

bool Sink::start() {
    std::lock_guard<std::mutex> lock(mu_);
    if (started_ || stopRequested_) return false;
    started_ = true;
    thread_ = std::thread(&Sink::run, this);
    return true;
}

bool Sink::submit(const uint8_t* rgb, size_t bytes) {
    if (rgb == nullptr || bytes != expectedBytes_) return false;
    box_.publish(rgb, bytes);
    return true;
}

void Sink::stop() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stopRequested_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    state_.store(SinkState::Stopped);
}

std::string Sink::lastError() const {
    std::lock_guard<std::mutex> lock(mu_);
    return lastError_;
}

bool Sink::stopRequested() const {
    std::lock_guard<std::mutex> lock(mu_);
    return stopRequested_;
}

void Sink::setError(const std::string& message) {
    std::lock_guard<std::mutex> lock(mu_);
    lastError_ = message;
}

bool Sink::sleepFor(int ms) {
    std::unique_lock<std::mutex> lock(mu_);
    // wait_for returns the predicate: true means a stop arrived during the sleep.
    return !cv_.wait_for(lock, std::chrono::milliseconds(ms),
                         [this] { return stopRequested_; });
}

bool Sink::backoff() {
    state_.store(SinkState::Backoff);
    const int delay = backoffMs_;
    backoffMs_ = std::min(backoffMs_ * 2, kBackoffCapMs);
    return sleepFor(delay);
}

void Sink::waitTick() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait_for(lock, std::chrono::milliseconds(kIdleTickMs),
                 [this] { return stopRequested_; });
}

void Sink::failConnection(const std::string& message) {
    setError(message);
    conn_.close();
}

bool Sink::openConnection(int timeoutMs) {
    if (cfg_.endpoint.preferDomainSocket) {
        const std::string path = cfg_.endpoint.domainSocketPath.empty()
                                     ? defaultDomainSocketPath()
                                     : cfg_.endpoint.domainSocketPath;
        if (conn_.openUnix(path)) return true;
    }
    if (conn_.openTcp(cfg_.endpoint.host, cfg_.endpoint.port, timeoutMs)) return true;
    failConnection(conn_.lastError());
    return false;
}

bool Sink::connectAndRegister() {
    state_.store(SinkState::Connecting);
    if (!openConnection(kConnectTimeoutMs)) return false;

    // A stop that arrives while connecting needs no Clear for this attempt:
    // the link is closed before Register, and a previous registration is
    // released by the teardown Clear in run(). This keeps stop() bounded.
    if (stopRequested()) {
        conn_.close();
        return false;
    }

    // HyperHDR does not release a slot on disconnect, so Register on every connect.
    if (!encodeRegister(frame_, cfg_.origin, cfg_.priority)) {
        failConnection("register frame rejected");
        return false;
    }
    if (!conn_.writeAll(frame_.data(), frame_.size(), kWriteTimeoutMs)) {
        failConnection(conn_.lastError());
        return false;
    }
    registeredOnce_ = true;
    return true;
}

bool Sink::sendImage(const uint8_t* rgb) {
    const uint8_t* data = rgb;
    if (cfg_.flipVertical) {
        const size_t row = static_cast<size_t>(cfg_.width) * 3;
        flipped_.resize(expectedBytes_);
        for (int y = 0; y < cfg_.height; ++y) {
            std::memcpy(flipped_.data() + static_cast<size_t>(y) * row,
                        rgb + static_cast<size_t>(cfg_.height - 1 - y) * row, row);
        }
        data = flipped_.data();
    }
    if (!encodeImageRgb(frame_, data, expectedBytes_, cfg_.width, cfg_.height)) {
        // Cannot happen for validated dimensions. Drop the frame; the link is fine.
        setError("image frame rejected");
        return true;
    }
    return conn_.writeAll(frame_.data(), frame_.size(), kWriteTimeoutMs);
}

void Sink::run() {
    using clock = std::chrono::steady_clock;
    clock::time_point lastSent{};
    bool haveSent = false;

    while (!stopRequested()) {
        if (!conn_.isOpen()) {
            if (!connectAndRegister()) {
                if (!backoff()) break;
                continue;
            }
            backoffMs_ = kBackoffStartMs;
            state_.store(SinkState::Registered);
        }

        // Throttle: at most one Image per interval. Frames that are not due stay in
        // the mailbox, so the newest frame is always the one that goes out.
        const clock::time_point now = clock::now();
        const bool due = !haveSent ||
                         std::chrono::duration<double>(now - lastSent).count() >= intervalSec_;
        if (due && box_.take(pending_)) {
            haveSent = true;
            lastSent = now;
            if (!sendImage(pending_.data())) {
                failConnection(conn_.lastError());
                if (!backoff()) break;
                continue;
            }
            state_.store(SinkState::Streaming);
        }

        // HyperHDR replies; an unread reply buffer wedges the socket.
        conn_.drain();
        if (!conn_.isOpen()) {
            setError(conn_.lastError());
            if (!backoff()) break;
            continue;
        }

        waitTick();
    }

    // Teardown: release the priority slot. Reconnect once (bounded) if the link is down.
    if (registeredOnce_) {
        if (!conn_.isOpen()) openConnection(kStopConnectTimeoutMs);
        if (conn_.isOpen()) {
            if (encodeClear(frame_, cfg_.priority)) {
                if (!conn_.writeAll(frame_.data(), frame_.size(), kStopWriteTimeoutMs)) {
                    setError(conn_.lastError());
                }
            }
        }
        conn_.close();
    }
}

}  // namespace hhd
