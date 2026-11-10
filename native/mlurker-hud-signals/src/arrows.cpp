// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mlurker@gmail.com
#include "arrows.h"

#include <cmath>

namespace hs {

namespace {

/* Inside the left-pointing arrow? x, y in 0..1 of the image. The head is a
 * triangle on the left 55 %, the tail a bar over the rest, a third of the
 * height. */
bool inside(float x, float y)
{
    const float head = 0.55f;
    if (x <= head) {
        /* the triangle's tip at (0, 0.5), its base the line x = head, full height */
        const float half = 0.5f * (x / head);
        return y >= 0.5f - half && y <= 0.5f + half;
    }
    return x <= 1.0f && y >= 0.5f - 1.0f / 6.0f && y <= 0.5f + 1.0f / 6.0f;
}

}  // namespace

std::vector<uint8_t> arrow_rgba(unsigned w, unsigned h, bool left, uint32_t rgb)
{
    std::vector<uint8_t> px((size_t)w * h * 4, 0);
    const unsigned ss = 4;   /* samples a side */
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            unsigned hit = 0;
            for (unsigned sy = 0; sy < ss; sy++)
                for (unsigned sx = 0; sx < ss; sx++) {
                    float fx = (x + (sx + 0.5f) / ss) / w, fy = (y + (sy + 0.5f) / ss) / h;
                    if (!left)
                        fx = 1.0f - fx;
                    if (inside(fx, fy))
                        hit++;
                }
            uint8_t *p = &px[((size_t)y * w + x) * 4];
            p[0] = (uint8_t)(rgb >> 16);
            p[1] = (uint8_t)(rgb >> 8);
            p[2] = (uint8_t)rgb;
            p[3] = (uint8_t)(hit * 255 / (ss * ss));
        }
    }
    return px;
}

std::vector<uint8_t> arcs_rgba(unsigned w, unsigned h, bool left)
{
    /* One circle centre, 17 px outside the layer's outer edge, in a layer of 64 x 132 (design pixels:
     * the arcs stretch with w / 64 across and h / 132 along the edge). From the centre out: the yellow inside, a dark orange ring, a yellow band, a thin cream arc,
     * a yellow band, a white arc, then a glow that fades out. The arcs fade towards the layer's top and bottom. */
    struct Rgb { float r, g, b; };
    auto over = [](Rgb *c, float *a, Rgb top, float ta) {   /* c is premultiplied */
        c->r = top.r * ta + c->r * (1 - ta);
        c->g = top.g * ta + c->g * (1 - ta);
        c->b = top.b * ta + c->b * (1 - ta);
        *a = ta + *a * (1 - ta);
    };
    auto ring = [](float r, float at, float half) { const float v = half + 0.5f - std::fabs(r - at); return v < 0 ? 0.f : v > 1 ? 1.f : v; };
    const float kx = w / 64.0f, ky = h / 132.0f, cx = -17.0f * kx, cy = h / 2.0f;
    std::vector<uint8_t> px((size_t)w * h * 4, 0);
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x0 = 0; x0 < w; x0++) {
            const unsigned x = left ? x0 : w - 1 - x0;   /* the right one is the mirror image */
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float r = std::sqrt((dx / kx) * (dx / kx) + (dy / ky) * (dy / ky));   /* in design pixels: the arcs are ellipses */
            Rgb c = {0, 0, 0};
            float a = 0;
            if (r > 58.0f) {                                     /* the glow */
                const float g = 1.0f - (r - 58.0f) / 22.0f;
                if (g > 0)
                    over(&c, &a, {255, 212, 0}, 0.9f * g * g);
            } else {                                             /* the yellow, a little deeper near the centre */
                const float t = r < 38.0f ? r / 38.0f : 1.0f;
                over(&c, &a, {250.0f + 5 * t, 196.0f + 18 * t, 0.0f + 10 * t}, 1.0f);
            }
            over(&c, &a, {217, 135, 0}, ring(r, 40.0f, 2.0f));
            over(&c, &a, {255, 243, 200}, ring(r, 48.3f, 1.0f) * 0.9f);
            over(&c, &a, {255, 252, 235}, ring(r, 56.6f, 2.0f));
            /* fade towards the layer's top and bottom (the arcs end there) */
            const float e = std::fabs(dy) / (h / 2.0f);
            const float fade = e < 0.55f ? 1.0f : e > 0.95f ? 0.0f : 1.0f - (e - 0.55f) / 0.40f;
            const float a0 = a;
            a *= fade;
            if (a <= 0.0f)
                continue;
            uint8_t *p = &px[((size_t)y * w + x0) * 4];
            const float inv = 1.0f / a0;   /* c is premultiplied by the alpha before the fade */
            p[0] = (uint8_t)std::fmin(255.0f, c.r * inv + 0.5f);
            p[1] = (uint8_t)std::fmin(255.0f, c.g * inv + 0.5f);
            p[2] = (uint8_t)std::fmin(255.0f, c.b * inv + 0.5f);
            p[3] = (uint8_t)(a * 255.0f + 0.5f);
        }
    }
    return px;
}

}  // namespace hs
