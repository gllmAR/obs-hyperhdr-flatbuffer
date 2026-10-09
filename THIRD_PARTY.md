# Third-party notices

## HyperHDR FlatBuffers schema (MIT)

The wire format implemented by `src/core/hhd_encoder.cpp` follows the HyperHDR
FlatBuffers schema (`hyperhdr_request.fbs`), Copyright (c) awawa-dev, licensed
under the MIT License. The schema is a data-format definition; no HyperHDR
source code is included in this repository.

## ofxhyperhdr (owner-authored)

The encoder and transport design are ported from files of
[ofxhyperhdr](https://github.com/gllmAR/ofxhyperhdr) authored by the owner
(Guillaume Arseneault). The relicensing permission for those owner-authored
files is recorded in `docs/SDD.md`, question Q-1.

## Qt6 (LGPL-3.0)

The optional Tools dialog links Qt6 dynamically. Qt is provided by OBS at
runtime and is not bundled with this plugin.
