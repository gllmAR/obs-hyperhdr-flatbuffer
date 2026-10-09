// SPDX-License-Identifier: GPL-2.0-or-later

#include "hhd_encoder.h"

#include <algorithm>
#include <cstring>
#include <type_traits>

namespace hhd {
namespace {

// Union discriminants and vtable slots (research/01 sections 3.1 and 3.2).
constexpr uint8_t kCmdImage = 2;
constexpr uint8_t kCmdClear = 3;
constexpr uint8_t kCmdRegister = 4;
constexpr uint8_t kImageRaw = 1;

constexpr uint16_t kRegisterOrigin = 4;
constexpr uint16_t kRegisterPriority = 6;
constexpr uint16_t kRawImageData = 4;
constexpr uint16_t kRawImageWidth = 6;
constexpr uint16_t kRawImageHeight = 8;
constexpr uint16_t kImageDataType = 4;
constexpr uint16_t kImageData = 6;
constexpr uint16_t kImageDuration = 8;
constexpr uint16_t kClearPriority = 4;
constexpr uint16_t kRequestCommandType = 4;
constexpr uint16_t kRequestCommand = 6;

template <typename T>
void writeLE(uint8_t* dst, T value) {
    using U = typename std::make_unsigned<T>::type;
    const U u = static_cast<U>(value);
    for (size_t i = 0; i < sizeof(T); ++i) {
        dst[i] = static_cast<uint8_t>(u >> (8 * i));
    }
}

template <typename T>
T readLE(const uint8_t* src) {
    typename std::make_unsigned<T>::type u = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        u |= static_cast<typename std::make_unsigned<T>::type>(src[i]) << (8 * i);
    }
    return static_cast<T>(u);
}

// Back-to-front FlatBuffers builder. Offsets are measured from the end of the
// buffer, as in the official builder, so the emitted bytes match it.
class Builder {
public:
    explicit Builder(size_t reserveBytes) {
        buf_.resize(std::max<size_t>(reserveBytes, 64));
        cur_ = buf_.size();
    }

    size_t size() const { return buf_.size() - cur_; }
    const uint8_t* data() const { return buf_.data() + cur_; }

    uint32_t createString(const std::string& s) {
        preAlign(s.size() + 1, sizeof(uint32_t));
        fill(1);  // NUL terminator, not counted in the length
        pushBytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
        return pushElement<uint32_t>(static_cast<uint32_t>(s.size()));
    }

    uint32_t createByteVector(const uint8_t* bytes, size_t count) {
        preAlign(count, sizeof(uint32_t));
        preAlign(count, 1);
        pushBytes(bytes, count);
        return pushElement<uint32_t>(static_cast<uint32_t>(count));
    }

    uint32_t startTable() const { return static_cast<uint32_t>(size()); }

    void addUint8(uint16_t slot, uint8_t value, uint8_t defaultValue) {
        if (value == defaultValue) return;
        pushElement<uint8_t>(value);
        trackField(slot);
    }

    void addInt32(uint16_t slot, int32_t value, int32_t defaultValue) {
        if (value == defaultValue) return;
        pushElement<int32_t>(value);
        trackField(slot);
    }

    void addOffset(uint16_t slot, uint32_t offset) {
        if (offset == 0) return;
        pushElement<uint32_t>(referTo(offset));
        trackField(slot);
    }

    uint32_t endTable(uint32_t start) {
        const uint32_t tableLoc = pushElement<int32_t>(0);  // soffset to vtable

        const uint16_t vtableBytes =
            std::max<uint16_t>(static_cast<uint16_t>(maxSlot_ + sizeof(uint16_t)), 4);
        fill(vtableBytes);
        uint8_t* vt = buf_.data() + cur_;
        writeLE<uint16_t>(vt, vtableBytes);
        writeLE<uint16_t>(vt + sizeof(uint16_t), static_cast<uint16_t>(tableLoc - start));
        for (const FieldLoc& f : fields_) {
            writeLE<uint16_t>(vt + f.slot, static_cast<uint16_t>(tableLoc - f.off));
        }
        fields_.clear();
        maxSlot_ = 0;

        // Reuse an identical vtable already in the buffer.
        uint32_t vtUse = static_cast<uint32_t>(size());
        for (uint32_t candidate : vtables_) {
            const uint8_t* other = buf_.data() + buf_.size() - candidate;
            if (readLE<uint16_t>(other) == vtableBytes &&
                std::memcmp(vt, other, vtableBytes) == 0) {
                cur_ += vtableBytes;  // discard the copy just written
                vtUse = candidate;
                break;
            }
        }
        if (vtUse == static_cast<uint32_t>(size())) {
            vtables_.push_back(vtUse);
        }

        writeLE<int32_t>(buf_.data() + buf_.size() - tableLoc,
                         static_cast<int32_t>(vtUse) - static_cast<int32_t>(tableLoc));
        return tableLoc;
    }

    void finish(uint32_t root) {
        preAlign(sizeof(uint32_t), minAlign_);
        pushElement<uint32_t>(referTo(root));
    }

private:
    struct FieldLoc {
        uint32_t off;
        uint16_t slot;
    };

    void reserve(size_t extra) {
        if (cur_ >= extra) return;
        const size_t used = size();
        const size_t grown = std::max(buf_.size() * 2, used + extra + 64);
        std::vector<uint8_t> next(grown);
        std::memcpy(next.data() + grown - used, buf_.data() + cur_, used);
        buf_.swap(next);
        cur_ = grown - used;
    }

    void pushBytes(const uint8_t* bytes, size_t count) {
        reserve(count);
        cur_ -= count;
        std::memcpy(buf_.data() + cur_, bytes, count);
    }

    void fill(size_t count) {
        if (count == 0) return;
        reserve(count);
        cur_ -= count;
        std::memset(buf_.data() + cur_, 0, count);
    }

    void align(size_t elementSize) {
        minAlign_ = std::max(minAlign_, elementSize);
        fill(((~size()) + 1) & (elementSize - 1));
    }

    void preAlign(size_t additional, size_t alignment) {
        minAlign_ = std::max(minAlign_, alignment);
        fill(((~(size() + additional)) + 1) & (alignment - 1));
    }

    template <typename T>
    uint32_t pushElement(T value) {
        align(sizeof(T));
        uint8_t tmp[sizeof(T)];
        writeLE<T>(tmp, value);
        pushBytes(tmp, sizeof(T));
        return static_cast<uint32_t>(size());
    }

    uint32_t referTo(uint32_t offset) {
        align(sizeof(uint32_t));
        return static_cast<uint32_t>(size()) - offset + static_cast<uint32_t>(sizeof(uint32_t));
    }

    void trackField(uint16_t slot) {
        fields_.push_back(FieldLoc{static_cast<uint32_t>(size()), slot});
        maxSlot_ = std::max(maxSlot_, slot);
    }

    std::vector<uint8_t> buf_;
    size_t cur_ = 0;
    size_t minAlign_ = 1;
    uint16_t maxSlot_ = 0;
    std::vector<FieldLoc> fields_;
    std::vector<uint32_t> vtables_;
};

// Writes the 4-byte big-endian length and the root into out, or rejects the payload.
bool frame(std::vector<uint8_t>& out, const Builder& b) {
    const size_t n = b.size();
    if (n == 0 || n > kMaxFrameBytes) {
        out.clear();
        return false;
    }
    out.resize(4 + n);
    out[0] = static_cast<uint8_t>((n >> 24) & 0xff);
    out[1] = static_cast<uint8_t>((n >> 16) & 0xff);
    out[2] = static_cast<uint8_t>((n >> 8) & 0xff);
    out[3] = static_cast<uint8_t>(n & 0xff);
    std::memcpy(out.data() + 4, b.data(), n);
    return true;
}

}  // namespace

bool encodeRegister(std::vector<uint8_t>& out, const std::string& origin, int32_t priority) {
    Builder b(256 + origin.size());
    const uint32_t originOff = b.createString(origin);
    const uint32_t regStart = b.startTable();
    b.addOffset(kRegisterOrigin, originOff);
    b.addInt32(kRegisterPriority, priority, 0);
    const uint32_t reg = b.endTable(regStart);

    const uint32_t reqStart = b.startTable();
    b.addUint8(kRequestCommandType, kCmdRegister, 0);
    b.addOffset(kRequestCommand, reg);
    b.finish(b.endTable(reqStart));
    return frame(out, b);
}

bool encodeImageRgb(std::vector<uint8_t>& out, const uint8_t* rgb, size_t rgbBytes,
                    int32_t width, int32_t height, int32_t duration) {
    if (rgb == nullptr || rgbBytes == 0 || rgbBytes > kMaxFrameBytes) {
        out.clear();
        return false;
    }
    Builder b(rgbBytes + 256);
    const uint32_t dataOff = b.createByteVector(rgb, rgbBytes);
    const uint32_t rawStart = b.startTable();
    b.addOffset(kRawImageData, dataOff);
    b.addInt32(kRawImageWidth, width, -1);
    b.addInt32(kRawImageHeight, height, -1);
    const uint32_t raw = b.endTable(rawStart);

    const uint32_t imgStart = b.startTable();
    b.addUint8(kImageDataType, kImageRaw, 0);
    b.addOffset(kImageData, raw);
    b.addInt32(kImageDuration, duration, -1);
    const uint32_t img = b.endTable(imgStart);

    const uint32_t reqStart = b.startTable();
    b.addUint8(kRequestCommandType, kCmdImage, 0);
    b.addOffset(kRequestCommand, img);
    b.finish(b.endTable(reqStart));
    return frame(out, b);
}

bool encodeClear(std::vector<uint8_t>& out, int32_t priority) {
    Builder b(128);
    const uint32_t clrStart = b.startTable();
    b.addInt32(kClearPriority, priority, 0);
    const uint32_t clr = b.endTable(clrStart);

    const uint32_t reqStart = b.startTable();
    b.addUint8(kRequestCommandType, kCmdClear, 0);
    b.addOffset(kRequestCommand, clr);
    b.finish(b.endTable(reqStart));
    return frame(out, b);
}

}  // namespace hhd
