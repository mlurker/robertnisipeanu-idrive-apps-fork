// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mlurker@gmail.com
/* The flasher against the sequence measured on the car (scripts/car/hud-and-signal-utils/HUD-SIGNALS.md),
 * and the arrow image. */
#include <cstdio>
#include <string>

#include "arrows.h"
#include "blindspot.h"
#include "indicators.h"
#include "readouts.h"

static int failed = 0;
#define CHECK(what, cond)                                       \
    do {                                                        \
        if (!(cond)) { std::printf("FAIL %s\n", what); failed++; } \
        else std::printf("ok   %s\n", what);                    \
    } while (0)

int main()
{
    hs::Flasher f(330);
    CHECK("off at start", !f.left_active() && !f.right_active() && !f.left_lit(0));

    /* left tip: control 1, then the pulse 2 at once, 0 after 80 ms, 1 every 640 ms */
    f.control(1);
    CHECK("left active, not lit before a pulse", f.left_active() && !f.left_lit(10));
    f.pulse(2, 100);
    CHECK("lit at the first pulse", f.left_lit(100) && !f.right_lit(100));
    f.pulse(0, 180);
    CHECK("still lit inside on_ms", f.left_lit(300));
    CHECK("out after on_ms", !f.left_lit(430) && f.next_change(300) == 430);
    f.pulse(1, 740);
    CHECK("lit again at the next pulse", f.left_lit(750) && !f.left_lit(1070));
    f.pulse(1, 760);
    CHECK("a pulse that stays 1 is one edge, not extended", f.left_lit(1060) && !f.left_lit(1075));
    f.pulse(0, 820);

    /* control off puts it out at once, inside a phase */
    f.pulse(1, 1380);
    CHECK("lit", f.left_lit(1390));
    f.control(0);
    CHECK("off at once", !f.left_lit(1390) && !f.left_active() && f.next_change(1390) == -1);

    /* right, and both */
    f.control(2);
    f.pulse(0, 2000);
    f.pulse(1, 2010);
    CHECK("right only", f.right_lit(2020) && !f.left_lit(2020));
    f.control(4);
    CHECK("continuous right is right", f.right_active() && !f.left_active());
    f.control(8);
    f.pulse(0, 2100);
    f.pulse(1, 2650);
    CHECK("hazard lights both", f.left_lit(2660) && f.right_lit(2660));
    f.control(3);
    CHECK("continuous left is left", f.left_active() && !f.right_active());

    /* the arrow */
    const unsigned w = 96, h = 96;
    std::vector<uint8_t> a = hs::arrow_rgba(w, h, true, 0x2bd96b), b = hs::arrow_rgba(w, h, false, 0x2bd96b);
    CHECK("image size", a.size() == (size_t)w * h * 4 && b.size() == a.size());
    auto alpha = [&](const std::vector<uint8_t> &v, unsigned x, unsigned y) { return v[((size_t)y * w + x) * 4 + 3]; };
    CHECK("left: tip side filled at the middle, far corner empty", alpha(a, 8, 48) > 200 && alpha(a, 0, 0) == 0);
    CHECK("left: tail reaches the right edge", alpha(a, 94, 48) > 200 && alpha(a, 94, 5) == 0);
    CHECK("right is the mirror", alpha(b, w - 1 - 8, 48) == alpha(a, 8, 48) && alpha(b, 1, 48) > 200);
    CHECK("colour kept", a[(48 * w + 40) * 4] == 0x2b && a[(48 * w + 40) * 4 + 1] == 0xd9 && a[(48 * w + 40) * 4 + 2] == 0x6b);

    /* the readouts */
    /* DataPowerTrain2 as measured: P 3, N 1, R 2, D 5 */
    CHECK("gear P", hs::gear_text({3}) == "P");
    CHECK("gear N", hs::gear_text({1}) == "N" && hs::gear_text({4}) == "N");
    CHECK("gear R", hs::gear_text({2}) == "R");
    CHECK("gear D with its number", hs::gear_text({5}) == "D1" && hs::gear_text({6}) == "D2" && hs::gear_text({13}) == "D9");
    CHECK("no gear when invalid or empty", hs::gear_text({15}) == "-" && hs::gear_text({0}) == "-" && hs::gear_text({}) == "-");
    unsigned km = 0;
    CHECK("tank range from the measured bytes",
          hs::tank_range_km({0x35, 0x00, 0x6d, 0x00, 0x02, 0x18, 0x00, 0x01, 0x76, 0x24}, &km) && km == 536);
    CHECK("ev range from the measured bytes", hs::ev_range_km({0x00, 0x14, 0x00, 0x18}, &km) && km == 20);
    CHECK("miles, rounded down like the cluster", hs::km_to_miles(524) == 325 && hs::km_to_miles(536) == 333 && hs::km_to_miles(0) == 0);
    std::vector<uint8_t> r = hs::readout_rgba(76, 46, "gas", 2, "333", 4, 0xffffff);
    size_t lit = 0;
    for (size_t i = 3; i < r.size(); i += 4)
        lit += r[i] == 255;
    CHECK("readout draws something inside its box", r.size() == 76u * 46 * 4 && lit > 100 && lit < 76u * 46 / 2);
    CHECK("value fits the width", hs::text_width("333", 4) <= 76 && hs::text_width("gear", 2) <= 76);
    /* the value sits above the label: the top rows hold the value, the rows under it the label */
    auto row_lit = [&](unsigned y) { size_t n = 0; for (unsigned x = 0; x < 76; x++) n += r[((size_t)y * 76 + x) * 4 + 3] > 0; return n; };
    /* the label is 14 rows tall at scale 2, then a gap, then the value from row 18 */
    size_t label_rows = 0, value_rows = 0;
    for (unsigned y = 0; y < 14; y++) label_rows += row_lit(y);
    for (unsigned y = 18; y < 46; y++) value_rows += row_lit(y);
    CHECK("label on top, gap, value under", label_rows > 0 && row_lit(15) == 0 && row_lit(16) == 0 && value_rows > label_rows);

    /* the mirror lamps: 22 D347 from the BDC, two status bytes after 62 D3 47 */
    auto hexbytes = [](const std::string &h) { std::vector<uint8_t> v; for (size_t i = 0; i + 1 < h.size(); i += 2) v.push_back((uint8_t)std::stoi(h.substr(i, 2), nullptr, 16)); return v; };
    uint8_t lv = 9, rv = 9;
    CHECK("lamp reply read", hs::lamp_reply({0x62, 0xD3, 0x47, 0x00, 0x02}, &lv, &rv) && lv == 0 && rv == 2);
    CHECK("another identifier's reply is not ours", !hs::lamp_reply({0x62, 0xD3, 0x48, 0, 0}, &lv, &rv) && !hs::lamp_reply({0x7F, 0x22, 0x31}, &lv, &rv) && !hs::lamp_reply({0x62, 0xD3, 0x47, 0x01}, &lv, &rv));

    /* the request: source F2, ecu, service 22, 0, length 2, ByteBuffer (32-bit length) D3 47, deadlock 0 */
    CHECK("request bytes", hs::tasu_read_request(0x40, 0xD347) == hexbytes("F2402200" "0002" "00000002" "D347" "00"));
    /* the response event: ByteBuffer(frame) + 16-bit length + 32-bit error code; frame = 9 header bytes (ecu at 6) + the reply */
    std::vector<uint8_t> frame = hexbytes("00000000000040" "0000"), body = {0x62, 0xD3, 0x47, 0x00, 0x00};
    frame.insert(frame.end(), body.begin(), body.end());
    std::vector<uint8_t> payload = {0, 0, 0, (uint8_t)frame.size()};
    payload.insert(payload.end(), frame.begin(), frame.end());
    payload.insert(payload.end(), {0, (uint8_t)frame.size(), 0, 0, 0, 0});
    uint8_t ecu = 0;
    std::vector<uint8_t> uds;
    CHECK("response parsed", hs::parse_tasu_response(payload, &ecu, &uds) && ecu == 0x40 && uds == body);
    std::vector<uint8_t> bad = payload;
    bad.back() = 5;
    CHECK("an error code is no reply", !hs::parse_tasu_response(bad, &ecu, &uds));
    bad = payload;
    bad[3 + 3] = 0x7F;   /* header byte 2: rejected by the transport */
    CHECK("a rejected frame is no reply", !hs::parse_tasu_response(bad, &ecu, &uds));
    /* what the car sent for another unit's 6601 read (the first bytes: 76 byte frame, 23 at header byte 6) */
    std::vector<uint8_t> car = hexbytes("0000004cf0f27101" "0f0b2300" "43" "626601");
    CHECK("garbage is no reply", !hs::parse_tasu_response({1, 2, 3}, &ecu, &uds) && !hs::parse_tasu_response({}, &ecu, &uds) && !hs::parse_tasu_response(car, &ecu, &uds));

    /* the lamps: off, steady, flashing; invalid values are off; stale reads put them out */
    hs::MirrorLamps ml;
    CHECK("off before any read", !ml.lit(true, 0) && !ml.lit(false, 0) && ml.next_change(0) == -1);
    ml.reading(0, 0, 100);
    CHECK("both off", ml.state(true, 100) == 0 && ml.state(false, 100) == 0 && ml.next_change(100) == -1);
    ml.reading(0, 1, 400);
    CHECK("right steady", ml.state(false, 400) == 1 && ml.lit(false, 400) && !ml.lit(true, 400) && ml.lit(false, 1000));
    ml.reading(2, 1, 700);
    CHECK("left flashing, right steady", ml.state(true, 700) == 2 && ml.state(false, 700) == 1);
    CHECK("flashing: on, off, on every 200 ms", ml.lit(true, 700) && ml.lit(true, 899) && !ml.lit(true, 900) && !ml.lit(true, 1099) && ml.lit(true, 1100));
    CHECK("next change is the flash edge", ml.next_change(750) == 900);
    ml.reading(4, 1, 1000);
    CHECK("the flash level does not restart the phase", ml.state(true, 1000) == 2 && !ml.lit(true, 1000) && ml.lit(true, 1100));
    ml.reading(0x0D, 0xFF, 1200);
    CHECK("display error and invalid are off", ml.state(true, 1200) == 0 && ml.state(false, 1200) == 0 && !ml.lit(true, 1200));
    ml.reading(1, 0, 2000);
    CHECK("steady until the read is stale", ml.lit(true, 2000 + hs::kStaleMs - 1) && !ml.lit(true, 2000 + hs::kStaleMs) && ml.next_change(2000) == 2000 + hs::kStaleMs);
    ml.reading(0, 2, 5000);
    ml.reading(0, 0, 5100);
    CHECK("a flashing lamp that goes off stays off", !ml.lit(false, 5100) && !ml.lit(false, 5300));

    /* the mark: glowing arcs round a centre outside the outer edge, 64 x 132 */
    std::vector<uint8_t> al = hs::arcs_rgba(64, 132, true), ar = hs::arcs_rgba(64, 132, false);
    auto aa = [](const std::vector<uint8_t> &v, unsigned x, unsigned y) { return v[((size_t)y * 64 + x) * 4 + 3]; };
    CHECK("mark size", al.size() == 64u * 132 * 4 && ar.size() == al.size());
    CHECK("left mark: opaque on the outer edge at mid height, the white arc further in", aa(al, 0, 66) == 255 && aa(al, 38, 66) == 255);
    CHECK("left mark: the white arc is whiter than the glow beyond it", al[(66 * 64 + 39) * 4 + 2] > 180 && al[(66 * 64 + 60) * 4 + 2] < 20);
    CHECK("left mark: the glow fades out and nothing is drawn beyond it", aa(al, 55, 66) > 0 && aa(al, 55, 66) < 255 && aa(al, 63, 66) == 0);
    CHECK("left mark: faded at the top and bottom, empty in the far corner", aa(al, 0, 0) < 10 && aa(al, 0, 131) < 10 && aa(al, 63, 0) == 0);
    bool mirror = true;
    for (unsigned y = 0; y < 132; y++)
        for (unsigned x = 0; x < 64; x++)
            for (int c = 0; c < 4; c++)
                mirror = mirror && al[((size_t)y * 64 + x) * 4 + c] == ar[((size_t)y * 64 + (63 - x)) * 4 + c];
    CHECK("right mark is the mirror", mirror);
    std::vector<uint8_t> an = hs::arcs_rgba(51, 158, true);
    CHECK("the default size: opaque at the edge, the white arc at x 31, nothing at the far edge",
          an.size() == 51u * 158 * 4 && an[(79 * 51 + 0) * 4 + 3] == 255 && an[(79 * 51 + 31) * 4 + 2] > 180 && an[(79 * 51 + 50) * 4 + 3] == 0);
    CHECK("mark colour is yellow", al[(66 * 64 + 20) * 4] > 240 && al[(66 * 64 + 20) * 4 + 1] > 190 && al[(66 * 64 + 20) * 4 + 2] < 20);
    std::printf(failed ? "FAILED\n" : "all ok\n");
    return failed ? 1 : 0;
}
