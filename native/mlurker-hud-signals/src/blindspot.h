// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mlurker@gmail.com
/* The lane change warning lamps in the exterior mirrors, read from the body domain controller (BDC, 0x40) by a
 * diagnostic read (UDS 22 D347, "AUSSENSPIEGEL_HC2", through the head unit's TASU service, InternalTasu 0xF8A6). The
 * answer is two bytes, left and right mirror: 0 off, 1 on steady, 2 to 4 on flashing (levels 1 to 3), 0x0D display
 * error, 0xFF invalid (BDC_G05 description, scripts/car/hud-and-signal-utils/HUD-SIGNALS.md). The same identifier takes a write (2E) that drives
 * the lamp: this code builds only the 22 read.
 *
 * Pure logic, no clock of its own: now_ms is passed in. */
#ifndef HS_BLINDSPOT_H
#define HS_BLINDSPOT_H

#include <stdint.h>

#include <vector>

namespace hs {

constexpr uint8_t kBdcEcu = 0x40;
constexpr uint16_t kLampDid = 0xD347;
constexpr int64_t kPollMs = 300;     /* one read this often */
constexpr int64_t kStaleMs = 3000;   /* no valid read for this long puts the marks out */
constexpr int64_t kFlashMs = 200;    /* a flashing lamp is shown on for this long, then off for this long */

/* The payload of InternalTasu.sendTasuRequest for "22 <did>" to <ecu>: source 0xF2 (the head unit's tester address),
 * target, service 0x22, subroutine 0, udsDataLength 2, udsData (a ByteBuffer: 32-bit big-endian length, bytes), deadlock 0. */
std::vector<uint8_t> tasu_read_request(uint8_t ecu, uint16_t did);

/* The payload of the sendTasuResponse event (udsResponse ByteBuffer, 16-bit length, 32-bit error code) -> the unit's
 * address and its UDS reply (62 .., or 7F 22 ..). False for a rejected frame, an error code or a payload that does
 * not fit. Measured on a G05. */
bool parse_tasu_response(const std::vector<uint8_t> &payload, uint8_t *ecu, std::vector<uint8_t> *uds);

/* A reply to 22 D347: true when it is a 62 D3 47 answer with both bytes. */
bool lamp_reply(const std::vector<uint8_t> &uds, uint8_t *left, uint8_t *right);

/* What the two lamps show now, from the last read. */
class MirrorLamps {
public:
    /* A valid read. */
    void reading(uint8_t left, uint8_t right, int64_t now_ms);
    /* 0 off, 1 steady, 2 flashing (invalid or error values are off), by the last read that is not stale. */
    int state(bool left, int64_t now_ms) const;
    /* Whether the mark is drawn now: steady is lit, flashing alternates every kFlashMs from its start. */
    bool lit(bool left, int64_t now_ms) const;
    /* When the lit state next changes by itself (a flash edge, or the read going stale), or -1. */
    int64_t next_change(int64_t now_ms) const;

private:
    struct Side {
        int state = 0;
        int64_t since = 0;
    };
    static int decode(uint8_t v) { return v == 1 ? 1 : (v >= 2 && v <= 4) ? 2 : 0; }
    Side l_, r_;
    int64_t last_ = 0;
    bool any_ = false;
};

}  // namespace hs

#endif
