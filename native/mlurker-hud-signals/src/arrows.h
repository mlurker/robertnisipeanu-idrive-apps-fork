// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mlurker@gmail.com
/* The turn arrow as an RGBA image (straight alpha), drawn by code so the
 * service ships no picture: a triangle head and a tail, anti-aliased, in one
 * colour; right is the left mirrored. */
#ifndef HS_ARROWS_H
#define HS_ARROWS_H

#include <stdint.h>

#include <vector>

namespace hs {

std::vector<uint8_t> arrow_rgba(unsigned w, unsigned h, bool left, uint32_t rgb);

/* The warning mark: glowing yellow arcs, a few concentric arcs round a centre just outside the layer's outer edge
 * (left: the left edge) with a glow that fades out; 64 x 132 is the design size, any size stretches the arcs: across with w / 64, along the edge with h / 132. */
std::vector<uint8_t> arcs_rgba(unsigned w, unsigned h, bool left);

}  // namespace hs

#endif
