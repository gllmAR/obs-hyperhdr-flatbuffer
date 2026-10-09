// SPDX-License-Identifier: GPL-2.0-or-later
// Live-sink registry, duplicate-priority warning and shutdown ordering (SDD 12).

#include "hhd_glue.h"

#include <obs-module.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace {

std::mutex g_mu;
std::vector<hhd_registry_entry> g_entries;
std::atomic<bool> g_shutdown{false};

}  // namespace

bool hhd_shutting_down() { return g_shutdown.load(); }

void hhd_registry_add(const hhd_registry_entry& entry) {
    std::lock_guard<std::mutex> lock(g_mu);
    for (auto& e : g_entries) {
        if (e.owner == entry.owner) {
            e = entry;
            return;
        }
    }
    g_entries.push_back(entry);
}

void hhd_registry_remove(void* owner) {
    std::lock_guard<std::mutex> lock(g_mu);
    for (size_t i = 0; i < g_entries.size(); ++i) {
        if (g_entries[i].owner == owner) {
            g_entries.erase(g_entries.begin() + i);
            return;
        }
    }
}

void hhd_registry_warn_duplicate(void* owner, int priority, const char* name) {
    std::lock_guard<std::mutex> lock(g_mu);
    for (const auto& e : g_entries) {
        if (e.owner != owner && e.priority == priority) {
            blog(LOG_WARNING,
                 "[hyperhdr] %s shares priority %d with another active sink; "
                 "HyperHDR picks one of them",
                 name, priority);
            return;
        }
    }
}

void hhd_registry_shutdown() {
    std::vector<hhd_registry_entry> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (g_shutdown.exchange(true)) return;  // already done
        snapshot = g_entries;
    }

    // SDD 12.1: render callbacks off first, then stop every sink (bounded Clear).
    for (const auto& e : snapshot) {
        if (e.remove_render) e.remove_render(e.owner);
    }
    for (const auto& e : snapshot) {
        e.stop(e.owner);
    }

    std::lock_guard<std::mutex> lock(g_mu);
    g_entries.clear();
}
