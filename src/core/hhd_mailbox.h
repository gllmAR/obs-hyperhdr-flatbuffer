// SPDX-License-Identifier: GPL-2.0-or-later
// Latest-wins single-frame hand-off from the OBS thread to the sink thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace hhd {

class Mailbox {
public:
    // Copies the frame in. A pending frame that was not taken is replaced.
    void publish(const uint8_t* rgb, size_t bytes) {
        std::lock_guard<std::mutex> lock(mu_);
        pending_.assign(rgb, rgb + bytes);
        fresh_ = true;
    }

    // Moves the pending frame into out. Returns false if nothing new arrived.
    bool take(std::vector<uint8_t>& out) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!fresh_) return false;
        out.swap(pending_);
        fresh_ = false;
        return true;
    }

private:
    std::mutex mu_;
    std::vector<uint8_t> pending_;
    bool fresh_ = false;
};

}  // namespace hhd
