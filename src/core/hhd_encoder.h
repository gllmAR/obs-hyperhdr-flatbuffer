// SPDX-License-Identifier: GPL-2.0-or-later
// HyperHDR FlatBuffers encoder (namespace hyperhdrnet, research/01). Ported from
// the owner-authored ofxhyperhdr encoder under SDD Q-1.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hhd {

constexpr size_t kMaxFrameBytes = 10000000;

// Each function writes one complete frame: 4-byte big-endian length, then the
// FlatBuffers Request root. Returns false and leaves out empty when the payload
// is zero-length or larger than kMaxFrameBytes.
bool encodeRegister(std::vector<uint8_t>& out, const std::string& origin, int32_t priority);
bool encodeImageRgb(std::vector<uint8_t>& out, const uint8_t* rgb, size_t rgbBytes,
                    int32_t width, int32_t height, int32_t duration = -1);
bool encodeClear(std::vector<uint8_t>& out, int32_t priority);

}  // namespace hhd
