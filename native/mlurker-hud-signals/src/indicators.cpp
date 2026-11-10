// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mlurker@gmail.com
#include "indicators.h"

namespace hs {

void Flasher::control(uint8_t c)
{
    left_ = c == 1 || c == 3 || c == 8;
    right_ = c == 2 || c == 4 || c == 8;
    if (!left_ && !right_)
        lit_until_ = 0;   /* off: out at once */
}

void Flasher::pulse(uint8_t p, int64_t now_ms)
{
    if (p != 0 && last_pulse_ == 0)
        lit_until_ = now_ms + on_ms_;
    last_pulse_ = p;
}

}  // namespace hs
