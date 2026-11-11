// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mlurker@gmail.com
/* The turn indicators as the head unit reports them, and the lamps' flashing.
 *
 * de.bmw.body.Light sends StatusFunctionsIndicators {control, active} and
 * StatusFunctionsIndicators2 {control, active, overlay, 1, pulse, 0}. control
 * 1 and 3 are left, 2 and 4 right, 8 both (hazard), 0 off. The fifth byte of
 * the second is a pulse that is non-zero for about 80 ms at the start of each
 * lamp-on phase (a period of about 640 ms), in step with the lamps (measured
 * on a G05, scripts/car/hud-and-signal-utils/HUD-SIGNALS.md). A side is lit from a pulse for on_ms, while
 * its control is on; control off puts it out at once.
 *
 * Pure logic, no clock of its own: now_ms is passed in. */
#ifndef HS_INDICATORS_H
#define HS_INDICATORS_H

#include <stdint.h>

namespace hs {

class Flasher {
public:
    explicit Flasher(int on_ms = 330) : on_ms_(on_ms) {}

    /* StatusFunctionsIndicators / StatusFunctionsIndicators2's first byte. */
    void control(uint8_t c);
    /* StatusFunctionsIndicators2's pulse byte; a rising edge starts a lamp-on phase. */
    void pulse(uint8_t p, int64_t now_ms);

    bool left_active() const { return left_; }
    bool right_active() const { return right_; }
    bool left_lit(int64_t now_ms) const { return left_ && now_ms < lit_until_; }
    bool right_lit(int64_t now_ms) const { return right_ && now_ms < lit_until_; }
    /* When the lit state next changes by itself (the end of a phase), or -1. */
    int64_t next_change(int64_t now_ms) const { return (left_ || right_) && now_ms < lit_until_ ? lit_until_ : -1; }

private:
    int on_ms_;
    bool left_ = false, right_ = false;
    uint8_t last_pulse_ = 0;
    int64_t lit_until_ = 0;
};

}  // namespace hs

#endif
