// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mlurker@gmail.com
#include "readouts.h"

#include <cmath>
#include <cstring>

namespace hs {

namespace {

struct Glyph {
    char c;
    const char *rows[7];
};

const Glyph kFont[] = {
    {'0', {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}},
    {'1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
    {'2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
    {'3', {"11110", "00001", "00001", "01110", "00001", "00001", "11110"}},
    {'4', {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}},
    {'5', {"11111", "10000", "11110", "00001", "00001", "10001", "01110"}},
    {'6', {"00110", "01000", "10000", "11110", "10001", "10001", "01110"}},
    {'7', {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
    {'8', {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}},
    {'9', {"01110", "10001", "10001", "01111", "00001", "00010", "01100"}},
    {'A', {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'D', {"11110", "11001", "11001", "11001", "11001", "11001", "11110"}},
    {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
    {'G', {"01110", "10001", "10000", "10111", "10001", "10001", "01110"}},
    {'N', {"10001", "11001", "10101", "10011", "10001", "10001", "10001"}},
    {'P', {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}},
    {'R', {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}},
    {'S', {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}},
    {'V', {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}},
    {'-', {"00000", "00000", "00000", "11111", "00000", "00000", "00000"}},
    {'a', {"00000", "00000", "01110", "00001", "01111", "10001", "01111"}},
    {'e', {"00000", "00000", "01110", "10001", "11111", "10000", "01110"}},
    {'g', {"00000", "01111", "10001", "10001", "01111", "00001", "01110"}},
    {'r', {"00000", "00000", "10110", "11001", "10000", "10000", "10000"}},
    {'s', {"00000", "00000", "01111", "10000", "01110", "00001", "11110"}},
    {'v', {"00000", "00000", "10001", "10001", "10001", "01010", "00100"}},
};

const Glyph *glyph(char c)
{
    for (const Glyph &g : kFont)
        if (g.c == c)
            return &g;
    return nullptr;
}

float smooth(float lo, float hi, float x)
{
    if (x <= lo) return 0.0f;
    if (x >= hi) return 1.0f;
    const float t = (x - lo) / (hi - lo);
    return t * t * (3.0f - 2.0f * t);
}

/* One glyph dot (col, row) as 0 or 1; the 5x7 cell has nothing outside. */
float dot(const Glyph *g, int col, int row)
{
    if (!g || col < 0 || col > 4 || row < 0 || row > 6)
        return 0.0f;
    return g->rows[row][col] == '1' ? 1.0f : 0.0f;
}

/* The glyph bitmap sampled smoothly (bilinear) and cut with a soft threshold below one half, so the
 * strokes come out thick with rounded corners, like the car's own speed readout. */
void draw_text(std::vector<uint8_t> &img, unsigned w, unsigned h, const std::string &text, unsigned scale, unsigned y,
               uint32_t rgb)
{
    const unsigned tw = text_width(text, scale);
    const unsigned x0 = tw < w ? (w - tw) / 2 : 0;
    const unsigned rows = 7 * scale;
    for (unsigned py = y; py < y + rows && py < h; py++) {
        const float v = ((float)(py - y) + 0.5f) / (float)scale - 0.5f;
        const int r0 = (int)std::floor(v);
        const float fr = v - (float)r0;
        for (unsigned px = x0; px < x0 + tw && px < w; px++) {
            const float u = ((float)(px - x0) + 0.5f) / (float)scale;
            const unsigned idx = (unsigned)(u / 6.0f);
            if (idx >= text.size())
                continue;
            const Glyph *g = glyph(text[idx]);
            const float s = u - 6.0f * (float)idx - 0.5f;
            const int c0 = (int)std::floor(s);
            const float fc = s - (float)c0;
            const float cov = (1 - fr) * ((1 - fc) * dot(g, c0, r0) + fc * dot(g, c0 + 1, r0)) +
                              fr * ((1 - fc) * dot(g, c0, r0 + 1) + fc * dot(g, c0 + 1, r0 + 1));
            const float a = smooth(0.22f, 0.5f, cov);
            if (a <= 0.0f)
                continue;
            uint8_t *p = &img[((size_t)py * w + px) * 4];
            p[0] = (rgb >> 16) & 0xff;
            p[1] = (rgb >> 8) & 0xff;
            p[2] = rgb & 0xff;
            p[3] = (uint8_t)(a * 255.0f + 0.5f);
        }
    }
}

}  // namespace

std::string gear_text(const std::vector<uint8_t> &payload)
{
    if (payload.empty())
        return "-";
    const uint8_t v = payload[0];
    if (v == 1 || v == 4)
        return "N";
    if (v == 2)
        return "R";
    if (v == 3)
        return "P";
    if (v >= 5 && v <= 13)
        return std::string("D") + (char)('0' + (v - 4));
    return "-";
}

bool tank_range_km(const std::vector<uint8_t> &p, unsigned *km)
{
    if (p.size() < 10)
        return false;
    *km = ((unsigned)p[4] << 8) | p[5];
    return *km != 0xffff;
}

bool ev_range_km(const std::vector<uint8_t> &p, unsigned *km)
{
    if (p.size() < 4)
        return false;
    *km = ((unsigned)p[0] << 8) | p[1];
    return *km != 0xffff;
}

/* whole miles, rounded down as the cluster does (524 km shows 325) */
unsigned km_to_miles(unsigned km) { return (unsigned)((unsigned long)km * 1000000ul / 1609344ul); }

unsigned text_width(const std::string &text, unsigned scale)
{
    return text.empty() ? 0 : (unsigned)text.size() * 6 * scale - scale;
}

std::vector<uint8_t> readout_rgba(unsigned w, unsigned h, const std::string &label, unsigned label_scale,
                                  const std::string &value, unsigned value_scale, uint32_t rgb)
{
    std::vector<uint8_t> img((size_t)w * h * 4, 0);
    /* the small title on top, the value big under it */
    draw_text(img, w, h, label, label_scale, 0, rgb);
    draw_text(img, w, h, value, value_scale, 7 * label_scale + 4, rgb);
    return img;
}

}  // namespace hs
