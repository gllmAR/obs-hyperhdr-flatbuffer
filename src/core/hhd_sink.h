// SPDX-License-Identifier: GPL-2.0-or-later
// One outgoing HyperHDR connection: owns a thread, a mailbox and the reconnect policy.
#pragma once

#include "hhd_mailbox.h"
#include "hhd_transport.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hhd {

struct Endpoint {
    bool preferDomainSocket = true;
    std::string domainSocketPath;  // empty = defaultDomainSocketPath()
    std::string host = "127.0.0.1";
    int port = 19400;
};

struct SinkConfig {
    std::string origin;
    int priority = 150;      // lower wins
    int width = 64;          // 8..256
    int height = 36;         // 8..256
    double maxFps = 30.0;    // 0 = unthrottled, clamped to 60
    bool flipVertical = false;
    Endpoint endpoint;
};

enum class SinkState { Idle, Connecting, Registered, Streaming, Backoff, Stopped };

// $TMPDIR/hyperhdr-domain, or /tmp/hyperhdr-domain when TMPDIR is unset or empty.
std::string defaultDomainSocketPath();

class Sink {
public:
    explicit Sink(SinkConfig cfg);
    ~Sink();
    Sink(const Sink&) = delete;
    Sink& operator=(const Sink&) = delete;

    // Starts the sink thread. Returns false if already started.
    bool start();
    // Takes width * height * 3 RGB bytes and never waits on the network.
    bool submit(const uint8_t* rgb, size_t bytes);
    // Sends Clear if a Register was ever acknowledged, then joins the thread. Idempotent.
    void stop();
    SinkState state() const { return state_.load(); }
    std::string lastError() const;

private:
    void run();
    bool openConnection(int timeoutMs);
    bool connectAndRegister();
    bool sendImage(const uint8_t* rgb);
    void failConnection(const std::string& message);
    bool backoff();
    bool sleepFor(int ms);
    void waitTick();
    bool stopRequested() const;
    void setError(const std::string& message);

    SinkConfig cfg_;
    size_t expectedBytes_ = 0;
    double intervalSec_ = 0.0;

    Mailbox box_;
    std::thread thread_;
    bool started_ = false;

    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool stopRequested_ = false;
    std::string lastError_;

    std::atomic<SinkState> state_{SinkState::Idle};

    // Touched only by the sink thread (and by stop() after join).
    Connection conn_;
    bool registeredOnce_ = false;
    int backoffMs_ = 250;
    std::vector<uint8_t> frame_;
    std::vector<uint8_t> flipped_;
    std::vector<uint8_t> pending_;
};

}  // namespace hhd
