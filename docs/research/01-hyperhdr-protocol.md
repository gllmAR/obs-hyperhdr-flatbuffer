# Research 01: HyperHDR FlatBuffers protocol

Status: research complete for the scope of v1 (RGB image push, one priority slot per sink). Facts below come from reading the reference code in `inspiration/`. The live-path claims come from the HIVE RFC, which records a live run on a HyperHDR host. This document did not re-run those checks.

## 1. Summary

HyperHDR accepts a length-prefixed FlatBuffers `Request` over a local domain socket or TCP. A client claims a priority slot with `Register`, streams `Image` frames, and releases the slot with `Clear`. HyperHDR does not release a slot when the client disconnects, so the client must send `Clear` on every teardown path.

## 2. Sources

| Source | What it gives us |
|---|---|
| [ofxHyperHDRFlat.h](../../inspiration/ofxhyperhdr/src/ofxHyperHDRFlat.h) | Schema excerpt, union IDs, vtable slot constants, frame limit |
| [ofxHyperHDRFlat.cpp](../../inspiration/ofxhyperhdr/src/ofxHyperHDRFlat.cpp) | Hand-rolled builder: `encodeRegister`, `encodeImage`, `encodeClear`, `encodeColor` |
| [ofxHyperHDRConnection.cpp](../../inspiration/ofxhyperhdr/src/ofxHyperHDRConnection.cpp) | Socket open, `writeAll`, `drain`, timeouts |
| [ofxHyperHDR.h](../../inspiration/ofxhyperhdr/src/ofxHyperHDR.h) | Defaults: priority 150, domain path, TCP 19400, maxFps |
| [ofxhyperhdr README](../../inspiration/ofxhyperhdr/README.md) | Socket-path behaviour (TMPDIR, PrivateTmp), JSON API check |
| [HIVE RFC-hyperhdr-flatbuffer.md](../../inspiration/HIVE/docs/RFC-hyperhdr-flatbuffer.md) | Framing rules, priority policy, 64x36 RGB choice, Clear-on-switch rule |

The upstream schema is `include/flatbuffers/parser/hyperhdr_request.fbs` in HyperHDR (MIT, awawa-dev). The schema file itself is not in the workspace. The excerpt below is quoted from the ofxhyperhdr header.

## 3. Schema (namespace `hyperhdrnet`)

```text
table Register  { origin:string (required); priority:int; }
table RawImage  { data:[ubyte]; width:int = -1; height:int = -1; }
table Image     { data:ImageType (required); duration:int = -1; }
table Clear     { priority:int; }
table Color     { data:int = -1; duration:int = -1; }
union Command   { Color, Image, Clear, Register }
table Request   { command:Command (required); }
```

`ImageType` is a union of `RawImage` (RGB, width x height) and `NV12Image`. The NV12 variant exists in the schema but is not used in v1.

### 3.1 Union discriminants

| Name | Value |
|---|---|
| `Command_NONE` | 0 |
| `Command_Color` | 1 |
| `Command_Image` | 2 |
| `Command_Clear` | 3 |
| `Command_Register` | 4 |
| `ImageType_NONE` | 0 |
| `ImageType_RawImage` | 1 |
| `ImageType_NV12Image` | 2 |

### 3.2 Vtable slots

The ofxhyperhdr header states that slot numbers are `4 + 2 * fieldIndex`, following the FlatBuffers vtable layout.

| Table | Field | Slot |
|---|---|---|
| `Register` | `origin` | 4 |
| `Register` | `priority` | 6 |
| `RawImage` | `data` | 4 |
| `RawImage` | `width` | 6 |
| `RawImage` | `height` | 8 |
| `Image` | `dataType` | 4 |
| `Image` | `data` | 6 |
| `Image` | `duration` | 8 |
| `Clear` | `priority` | 4 |
| `Color` | `data` | 4 |
| `Color` | `duration` | 6 |
| `Request` | `commandType` | 4 |
| `Request` | `command` | 6 |

## 4. Framing

- Each message is a 4-byte big-endian payload length followed by the FlatBuffers root (`Request`).
- Payloads longer than 10,000,000 bytes (`kMaxFrameBytes`) are rejected.
- Zero-length frames are rejected (ofxHyperHDRFlat.h comment; HIVE RFC says "reject <1 byte").

## 5. Transport

| Endpoint | Default | Notes |
|---|---|---|
| Domain socket (POSIX) | `/tmp/hyperhdr-domain` in ofxhyperhdr | HyperHDR uses a Qt `QLocalServer` named `hyperhdr-domain`. Qt places it in the process temp directory, which is `TMPDIR` when set. On macOS this is not `/tmp`. |
| TCP fallback | `127.0.0.1:19400` | Works on all platforms. Used when the socket is absent or the host is remote. |
| JSON API | port 8090, `/json-rpc` | Used only for verification (`serverinfo`). Not part of the stream path. |

Findings:

- The ofxhyperhdr README documents the failure mode. A `PrivateTmp=yes` systemd unit gives HyperHDR a private `/tmp` the client cannot see, and a `TMPDIR` override moves the socket.
- The domain socket path must be derived from `TMPDIR` (fallback `/tmp`), not hardcoded.
- Windows: no evidence in the workspace that HyperHDR exposes a Windows named pipe for this protocol. Treat TCP as the only verified Windows path until a spike proves otherwise.

Socket options used by ofxhyperhdr (keep): `TCP_NODELAY`, `SO_SNDTIMEO` = 1 s, non-blocking after open. The client must also drain replies, since an unread reply buffer wedges the socket (ofxhyperhdr `drain`, HIVE RFC).

## 6. Priority model and teardown

- Lower priority number wins. The USB grabber sits at 240.
- 150 is the conventional slot for external sources (HIVE RFC, ofxhyperhdr default).
- `Register { origin, priority }` claims the slot. `origin` is shown in HyperHDR's priority list, so it must identify the machine or source.
- `Clear { priority }` releases the slot. HyperHDR does not release it on disconnect.
- Rule: send `Clear` on output stop, on source switch, and on application exit. Re-Register after reconnect.

## 7. Verification status

| Claim | Evidence | Re-run here? |
|---|---|---|
| Byte-exact encoding vs official Python `flatbuffers` builder | HIVE RFC and ofxhyperhdr `tests/verify_flat.py` | No |
| Live run against a HyperHDR host | HIVE RFC (`inflo-trx-x`, TCP 19400) | No |
| Domain socket and TMPDIR behaviour | ofxhyperhdr README | No |

Required before implementation: re-run the golden tests and a live smoke test on a reference host. See the SDD test strategy.

## 8. Discrepancies found in the references

| Item | Where | Impact |
|---|---|---|
| `maxFps` default 60 in the header, 30 in the README | [ofxHyperHDR.h](../../inspiration/ofxhyperhdr/src/ofxHyperHDR.h) and [README](../../inspiration/ofxhyperhdr/README.md) | Choose one default in the new design (SDD uses 30) |
| Hardcoded `/tmp/hyperhdr-domain` | ofxHyperHDR.h | Wrong on macOS and under `TMPDIR` overrides |
| `static bool firstFrame` inside `send(ofTexture&)` | ofxHyperHDR.cpp | Shared across instances; per-sink state required |
| Windows unsupported | ofxhyperhdr README | Known gap; v1 of the new plugin targets TCP on Windows |

## 9. Open items

- Confirm the Windows transport (named pipe or TCP only) with a spike against a Windows HyperHDR build.
- Confirm multi-client behaviour: two sinks on the same priority is undefined in the references. The SDD adds a duplicate-priority warning.
- Confirm the recommended image size. The HIVE RFC uses 64x36 RGB.
