// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mlurker@gmail.com
/* The HUD's three readouts (gear, fuel range, electric range): the decoders of
 * the car's fields (scripts/car/hud-and-signal-utils/HUD-SIGNALS.md) and a text image drawn by code (a 5x7
 * font, whole-pixel scales), so the service ships no font. */
#ifndef HS_READOUTS_H
#define HS_READOUTS_H

#include <stdint.h>

#include <string>
#include <vector>

namespace hs {

/* VehicleInformation.DataPowerTrain2 (service 0x1536, event 0x8004, 1 byte, statusGearSelectionDrive; measured on a
 * G05): 1 N, 2 R, 3 P, 4 N or P, 5 to 13 D (gear 1 to 9), else none. "P", "R", "N", "D1" to "D9" (D and manual both send the gear number) or "-". */
std::string gear_text(const std::vector<uint8_t> &payload);

/* KombiIF.statusRangeTankStatus (10 B): the range in km at bytes 4-5, big endian. */
bool tank_range_km(const std::vector<uint8_t> &payload, unsigned *km);
/* KombiIF.displayRangeElectricVehicle (4 B): range and maximum, big endian. Already in the display unit
 * (miles on a car set to miles); the fuel range is always km. */
bool ev_range_km(const std::vector<uint8_t> &payload, unsigned *km);
unsigned km_to_miles(unsigned km);

/* A w x h RGBA image (straight alpha) with the small label on top and the value under it, both centred, in one colour. Scales
 * are whole pixels per font dot. */
std::vector<uint8_t> readout_rgba(unsigned w, unsigned h, const std::string &label, unsigned label_scale,
                                  const std::string &value, unsigned value_scale, uint32_t rgb);
/* The width of a text at a scale, in pixels. */
unsigned text_width(const std::string &text, unsigned scale);

}  // namespace hs

#endif
