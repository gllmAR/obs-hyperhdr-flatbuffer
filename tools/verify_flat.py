#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Print golden HyperHDR Request frames built with the official flatbuffers builder.

Requires: pip install flatbuffers
The C++ tests in tests/unit/test_core.cpp embed these hex strings. Re-run this
script and update the constants if the schema or the builder order changes.
"""

import flatbuffers

CMD_IMAGE = 2
CMD_CLEAR = 3
CMD_REGISTER = 4
IMAGE_RAW = 1


def _frame(builder, root):
    builder.Finish(root)
    body = bytes(builder.Output())
    return len(body).to_bytes(4, "big") + body


def register(origin, priority):
    b = flatbuffers.Builder(0)
    origin_off = b.CreateString(origin)
    b.StartObject(2)
    b.PrependUOffsetTRelativeSlot(0, origin_off, 0)
    b.PrependInt32Slot(1, priority, 0)
    reg = b.EndObject()
    b.StartObject(2)
    b.PrependUint8Slot(0, CMD_REGISTER, 0)
    b.PrependUOffsetTRelativeSlot(1, reg, 0)
    return _frame(b, b.EndObject())


def clear(priority):
    b = flatbuffers.Builder(0)
    b.StartObject(1)
    b.PrependInt32Slot(0, priority, 0)
    clr = b.EndObject()
    b.StartObject(2)
    b.PrependUint8Slot(0, CMD_CLEAR, 0)
    b.PrependUOffsetTRelativeSlot(1, clr, 0)
    return _frame(b, b.EndObject())


def image_rgb(rgb, width, height, duration=-1):
    b = flatbuffers.Builder(0)
    data_off = b.CreateByteVector(rgb)
    b.StartObject(3)
    b.PrependUOffsetTRelativeSlot(0, data_off, 0)
    b.PrependInt32Slot(1, width, -1)
    b.PrependInt32Slot(2, height, -1)
    raw = b.EndObject()
    b.StartObject(3)
    b.PrependUint8Slot(0, IMAGE_RAW, 0)
    b.PrependUOffsetTRelativeSlot(1, raw, 0)
    b.PrependInt32Slot(2, duration, -1)
    img = b.EndObject()
    b.StartObject(2)
    b.PrependUint8Slot(0, CMD_IMAGE, 0)
    b.PrependUOffsetTRelativeSlot(1, img, 0)
    return _frame(b, b.EndObject())


if __name__ == "__main__":
    print("register_obs_150", register("obs", 150).hex())
    print("clear_150", clear(150).hex())
    # 2x1 RGB: red, green
    print("image_2x1", image_rgb(bytes([255, 0, 0, 0, 255, 0]), 2, 1).hex())
