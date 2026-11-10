// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mlurker@gmail.com
#include "blindspot.h"

namespace hs {

std::vector<uint8_t> tasu_read_request(uint8_t ecu, uint16_t did)
{
    return {0xF2, ecu, 0x22, 0x00,           /* source, target, service, subroutine */
            0x00, 0x02,                      /* udsDataLength */
            0x00, 0x00, 0x00, 0x02,          /* udsData: ByteBuffer, 32-bit length */
            (uint8_t)(did >> 8), (uint8_t)did,
            0x00};                           /* deadlockTime */
}

namespace {

/* The payload is udsResponse (a 32-bit big-endian length, then the bytes), udsResponseLength (2) and errorCode (an enum,
 * 32 bits: the method's own answer is the same enum, 00 00 00 01 = REQUEST_PENDING). */
bool split(const std::vector<uint8_t> &p, std::vector<uint8_t> *frame)
{
    const size_t prefix = 4, trailer = 2 + 4;
    if (p.size() < prefix + trailer)
        return false;
    const size_t n = ((size_t)p[0] << 24) | ((size_t)p[1] << 16) | ((size_t)p[2] << 8) | p[3];
    if (prefix + n + trailer != p.size())
        return false;
    if (p[p.size() - 4] | p[p.size() - 3] | p[p.size() - 2] | p[p.size() - 1])   /* errorCode: NO_ERROR */
        return false;
    frame->assign(p.begin() + prefix, p.begin() + prefix + n);
    return true;
}

}  // namespace

bool parse_tasu_response(const std::vector<uint8_t> &payload, uint8_t *ecu, std::vector<uint8_t> *uds)
{
    std::vector<uint8_t> f;
    if (!split(payload, &f))
        return false;
    /* a 9-byte header: [2] 0x7F = rejected by the transport, [6] the unit's address; its UDS reply from [9] */
    if (f.size() < 10 || f[2] == 0x7F)
        return false;
    *ecu = f[6];
    uds->assign(f.begin() + 9, f.end());
    return true;
}

bool lamp_reply(const std::vector<uint8_t> &uds, uint8_t *left, uint8_t *right)
{
    if (uds.size() < 5 || uds[0] != 0x62 || uds[1] != (kLampDid >> 8) || uds[2] != (kLampDid & 0xFF))
        return false;
    *left = uds[3];
    *right = uds[4];
    return true;
}

void MirrorLamps::reading(uint8_t left, uint8_t right, int64_t now_ms)
{
    const int nl = decode(left), nr = decode(right);
    if (nl != l_.state)
        l_.since = now_ms;
    if (nr != r_.state)
        r_.since = now_ms;
    l_.state = nl;
    r_.state = nr;
    last_ = now_ms;
    any_ = true;
}

int MirrorLamps::state(bool left, int64_t now_ms) const
{
    if (!any_ || now_ms - last_ >= kStaleMs)
        return 0;
    return (left ? l_ : r_).state;
}

bool MirrorLamps::lit(bool left, int64_t now_ms) const
{
    const int s = state(left, now_ms);
    if (s == 1)
        return true;
    if (s != 2)
        return false;
    return ((now_ms - (left ? l_ : r_).since) / kFlashMs) % 2 == 0;
}

int64_t MirrorLamps::next_change(int64_t now_ms) const
{
    int64_t next = -1;
    for (int i = 0; i < 2; i++) {
        const bool left = i == 0;
        const int s = state(left, now_ms);
        if (s == 0)
            continue;
        const Side &sd = left ? l_ : r_;
        int64_t t = last_ + kStaleMs;
        if (s == 2) {
            const int64_t edge = sd.since + ((now_ms - sd.since) / kFlashMs + 1) * kFlashMs;
            if (edge < t)
                t = edge;
        }
        if (next < 0 || t < next)
            next = t;
    }
    return next;
}

}  // namespace hs
