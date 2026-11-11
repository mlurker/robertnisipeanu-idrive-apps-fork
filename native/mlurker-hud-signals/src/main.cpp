// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mlurker@gmail.com
/* mlurker-hud-signals: the car's turn indicators on the HUD, as layers of the shared
 * cluster renderer (cluster-render). It reads de.bmw.body.Light over the
 * unit's SOME/IP (as hmi_5000, the one uid whose vsomeip policy allows it) and
 * draws a green arrow at the HUD's bottom left and bottom right, each flashing
 * with its lamp. It works with or without the map: its layers are its own.
 * With blindspot = 1 it also reads the state of the lane change warning lamps in the exterior mirrors from the body
 * domain controller (a diagnostic read through the head unit's TASU service, blindspot.h) and shows glowing yellow arcs
 * at the HUD's left or right edge, steady or flashing like the mirror lamp.
 *
 *   mlurker-hud-signals [-c FILE] [-s key=value]...
 *
 * Settings (mlurker-hud-signals.conf; a local conf next to it overrides): which sides,
 * the arrows' size and place, colour and how long a lamp-on phase shows. See
 * README.md. Needs cluster-render 0.4.0. */
#include <vsomeip/vsomeip.hpp>

#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <set>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "arrows.h"
#include "blindspot.h"
#include "cluster_render/client.h"
#include "config.h"
#include "indicators.h"
#include "log.h"
#include "readouts.h"

#ifndef HS_VERSION
#define HS_VERSION ""
#endif

using namespace cld;

namespace {

const vsomeip::service_t kLight = 0x5532;
const vsomeip::instance_t kInstance = 1;
const vsomeip::major_version_t kMajor = 1;
const vsomeip::event_t kStatusIndicators = 0x8006;    /* {control, active} */
const vsomeip::event_t kStatusIndicators2 = 0x8009;   /* {control, active, overlay, 1, pulse, 0} */

const vsomeip::service_t kVehicleInformation = 0x1536;
const vsomeip::event_t kGearSelection = 0x8004;       /* DataPowerTrain2: P R N D */
const vsomeip::event_t kVehicleRange = 0x8002;        /* groups 2 and 5 */
const vsomeip::service_t kKombi = 0xB51B;
const vsomeip::event_t kTankRange = 0x801D;           /* group 1 */
const vsomeip::event_t kEvRange = 0x8031;             /* group 1 */

const vsomeip::service_t kTasu = 0xF8A6;              /* InternalTasu: the head unit's diagnostic link */
const vsomeip::instance_t kTasuInstance = 128;
const vsomeip::method_t kTasuRequest = 0x0001;        /* sendTasuRequest */
const vsomeip::event_t kTasuResponse = 0x8001;        /* sendTasuResponse, group 1, selective */

struct Message {
    vsomeip::service_t service;
    vsomeip::event_t event;
    std::vector<uint8_t> data;
};

std::mutex g_mutex;
std::deque<Message> g_queue;
int g_wake[2] = {-1, -1};
std::atomic<bool> g_stop(false);
std::atomic<bool> g_light_up(false);
std::atomic<bool> g_tasu_up(false);

void on_signal(int) { g_stop = true; }

void wake()
{
    char c = 0;
    ssize_t n = write(g_wake[1], &c, 1);
    (void)n;
}

/* vsomeip: Light's two status events into the queue; availability logged. With the blind spot warning on, also the
 * diagnostic link's answers. */
std::shared_ptr<vsomeip::application> start_vsomeip(std::thread *loop, bool blindspot)
{
    /* no VSOMEIP_CONFIGURATION: the unit's /etc/vsomeip, as cluster-render and the HMI */
    std::shared_ptr<vsomeip::application> app = vsomeip::runtime::get()->create_application("mlurker-hud-signals");
    if (!app || !app->init())
        return nullptr;
    struct Sub { vsomeip::service_t service; vsomeip::instance_t instance; const char *name; std::vector<std::pair<vsomeip::event_t, vsomeip::eventgroup_t>> events; };
    std::vector<Sub> subs = {
        {kLight, kInstance, "Light", {{kStatusIndicators, 6}, {kStatusIndicators2, 8}}},
        {kVehicleInformation, kInstance, "VehicleInformation", {{kGearSelection, 4}, {kVehicleRange, 2}}},
        {kKombi, kInstance, "KombiIF", {{kTankRange, 1}, {kEvRange, 1}}},
    };
    if (blindspot)
        subs.push_back({kTasu, kTasuInstance, "TASU", {{kTasuResponse, 1}}});
    for (const Sub &sub : subs) {
        const vsomeip::service_t service = sub.service;
        const vsomeip::instance_t instance = sub.instance;
        const std::string name = sub.name;
        app->register_message_handler(service, instance, vsomeip::ANY_METHOD, [service](const std::shared_ptr<vsomeip::message> &m) {
            /* the diagnostic link's method answer (its error code) is kept too: it says why a read was turned away */
            const bool answer = service == kTasu && m->get_message_type() == vsomeip::message_type_e::MT_RESPONSE;
            if ((m->get_message_type() != vsomeip::message_type_e::MT_NOTIFICATION && !answer) || !m->get_payload())
                return;
            Message msg;
            msg.service = service;
            msg.event = m->get_method();
            msg.data.assign(m->get_payload()->get_data(), m->get_payload()->get_data() + m->get_payload()->get_length());
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (g_queue.size() < 256)
                    g_queue.push_back(std::move(msg));
            }
            wake();
        });
        app->register_availability_handler(service, instance, [service, name](vsomeip::service_t, vsomeip::instance_t, bool up) {
            log_info("%s %s", name.c_str(), up ? "available" : "unavailable");
            if (service == kLight)
                g_light_up = up;
            if (service == kTasu)
                g_tasu_up = up;
            wake();
        }, kMajor, vsomeip::ANY_MINOR);
        for (const auto &e : sub.events)
            app->request_event(service, instance, e.first, std::set<vsomeip::eventgroup_t>{e.second},
                               service == kTasu ? vsomeip::event_type_e::ET_SELECTIVE_EVENT : vsomeip::event_type_e::ET_FIELD,
                               vsomeip::reliability_type_e::RT_UNRELIABLE);
        app->request_service(service, instance, kMajor, vsomeip::ANY_MINOR);
        std::set<vsomeip::eventgroup_t> groups;
        for (const auto &e : sub.events)
            groups.insert(e.second);
        for (vsomeip::eventgroup_t g : groups)
            app->subscribe(service, instance, g, kMajor);
    }
    *loop = std::thread([app] { app->start(); });
    return app;
}

/* One diagnostic read, "22 <did>" to <ecu>, through the head unit's TASU service. The answer comes as an event. */
void send_tasu_read(const std::shared_ptr<vsomeip::application> &app, uint8_t ecu, uint16_t did)
{
    std::shared_ptr<vsomeip::message> rq = vsomeip::runtime::get()->create_request(false);
    rq->set_service(kTasu);
    rq->set_instance(kTasuInstance);
    rq->set_method(kTasuRequest);
    rq->set_interface_version(kMajor);
    std::shared_ptr<vsomeip::payload> pl = vsomeip::runtime::get()->create_payload();
    const std::vector<uint8_t> bytes = hs::tasu_read_request(ecu, did);
    pl->set_data(bytes);
    rq->set_payload(pl);
    app->send(rq);
}

/* "hud:800x480" out of the service's hello ("map:1088x480,panel:1920x720,hud:800x480"). */
bool target_size(const std::string &targets, const std::string &name, unsigned *w, unsigned *h)
{
    size_t at = 0;
    while (at < targets.size()) {
        size_t end = targets.find(',', at);
        if (end == std::string::npos)
            end = targets.size();
        const std::string t = targets.substr(at, end - at);
        const size_t colon = t.find(':');
        if (colon != std::string::npos && t.compare(0, colon, name) == 0 && colon == name.size())
            return std::sscanf(t.c_str() + colon + 1, "%ux%u", w, h) == 2;
        at = end + 1;
    }
    return false;
}

struct Side {
    uint32_t ref = 0;
    bool lit = false;
    cr::LayerOptions lo;
};

/* a readout: a small label with the value under it, one image layer */
struct Readout {
    Readout(const char *n, const char *l) : name(n), label(l) {}
    const char *name;     /* "gear", "gas", "ev": also its place-file key */
    const char *label;
    uint32_t ref = 0;
    cr::LayerOptions lo;
    std::string value = "--";
    bool dirty = true;
};

constexpr unsigned kLabelScale = 2, kValueScale = 4, kReadoutH = 7 * 4 + 4 + 7 * 2;   /* label, gap, value */

class Hud {
public:
    Hud(const Config &cfg, cr::Client *client) : cfg_(cfg), client_(client), flasher_((int)cfg.num("on_ms")) {}

    /* The layers, once the service is up (and again after its hello, which carries the targets). */
    void make_layers()
    {
        if (made_)
            return;
        unsigned tw = 0, th = 0;
        const std::string target = cfg_.str("target");
        if (!target_size(client_->targets, target, &tw, &th)) {
            log_error("the cluster renderer has no target %s (%s)", target.c_str(), client_->targets.c_str());
            return;
        }
        const unsigned size = (unsigned)cfg_.num("arrow_size");
        const int mx = (int)cfg_.num("margin_x"), mxr = (int)cfg_.num("margin_right_x"), my = (int)cfg_.num("margin_y");
        const uint32_t rgb = (uint32_t)strtoul(cfg_.str("color").c_str(), nullptr, 16);
        const bool left = cfg_.num("left") != 0, right = cfg_.num("right") != 0;
        for (int i = 0; i < 2; i++) {
            const bool is_left = i == 0;
            if (is_left ? !left : !right)
                continue;
            Side &s = is_left ? left_ : right_;
            s.lo.target = target;
            s.lo.klass = cfg_.str("layer_class");
            s.lo.x = is_left ? mx : (int)tw - (int)size - mxr;
            s.lo.y = (int)th - (int)size - my;
            s.lo.w = s.lo.h = size;
            s.lo.opacity = 0;
            s.lo.visible = hold();
            cr::ImageOptions io;
            io.w = io.h = size;
            s.ref = client_->image(s.lo, io);
            std::vector<uint8_t> px = hs::arrow_rgba(size, size, is_left, rgb);
            if (!s.ref || !client_->pixels(s.ref, {}, px.data(), px.size()))
                log_warn("the %s arrow's image was not accepted yet", is_left ? "left" : "right");
            log_info("%s arrow: %ux%u at %d,%d on %s (%ux%u)", is_left ? "left" : "right", size, size, s.lo.x, s.lo.y,
                     target.c_str(), tw, th);
        }
        if (cfg_.num("blindspot") != 0) {
            const unsigned bw = (unsigned)cfg_.num("blindspot_w"), bh = (unsigned)cfg_.num("blindspot_h");
            for (int i = 0; i < 2; i++) {
                const bool is_left = i == 0;
                Side &s = is_left ? bs_left_ : bs_right_;
                s.lo.target = target;
                s.lo.klass = cfg_.str("layer_class");
                s.lo.x = is_left ? (int)cfg_.num("blindspot_margin_x") : (int)tw - (int)bw - (int)cfg_.num("blindspot_margin_right_x");
                s.lo.y = (int)th - (int)bh - (int)cfg_.num("blindspot_margin_y");
                s.lo.w = bw;
                s.lo.h = bh;
                s.lo.opacity = 0;
                s.lo.visible = hold();
                cr::ImageOptions io;
                io.w = bw;
                io.h = bh;
                s.ref = client_->image(s.lo, io);
                std::vector<uint8_t> px = hs::arcs_rgba(bw, bh, is_left);
                if (!s.ref || !client_->pixels(s.ref, {}, px.data(), px.size()))
                    log_warn("the %s warning mark's image was not accepted yet", is_left ? "left" : "right");
                log_info("%s warning mark: %ux%u at %d,%d", is_left ? "left" : "right", bw, bh, s.lo.x, s.lo.y);
            }
        }
        if (cfg_.num("readouts") != 0) {
            const unsigned rw = (unsigned)cfg_.num("readout_w");
                for (int i = 0; i < 3; i++) {
                Readout &r = readouts_[i];
                r.lo.target = target;
                r.lo.klass = cfg_.str("layer_class");
                /* a vertical strip (readout_dir = v, the default) or a row (h) */
                const bool vertical = cfg_.str("readout_dir") != "h";
                r.lo.x = (int)cfg_.num("readout_x") + (vertical ? 0 : i * (int)rw);
                r.lo.y = (int)cfg_.num("readout_y") + (vertical ? i * ((int)kReadoutH + (int)cfg_.num("readout_gap")) : 0);
                r.lo.w = rw;
                r.lo.h = kReadoutH;
                r.lo.opacity = 1.0f;
                r.lo.visible = true;
                cr::ImageOptions io;
                io.w = rw;
                io.h = kReadoutH;
                r.ref = client_->image(r.lo, io);
                r.dirty = true;
                log_info("%s readout: %ux%u at %d,%d", r.name, rw, kReadoutH, r.lo.x, r.lo.y);
            }
            draw_readouts();
        }
        made_ = true;
    }

    /* Re-draws the readouts whose text changed. */
    void draw_readouts()
    {
        if (!made_ && !readouts_[0].ref)
            return;
        const unsigned rw = (unsigned)cfg_.num("readout_w");
        const uint32_t rrgb = (uint32_t)strtoul(cfg_.str("readout_color").c_str(), nullptr, 16);
        for (Readout &r : readouts_) {
            if (!r.ref || !r.dirty)
                continue;
            r.dirty = false;
            std::vector<uint8_t> px = hs::readout_rgba(rw, kReadoutH, r.label, kLabelScale, r.value, kValueScale, rrgb);
            if (!client_->pixels(r.ref, {}, px.data(), px.size()))
                log_warn("the %s readout's image was not accepted yet", r.name);
        }
    }

    void set_readout(int i, const std::string &v)
    {
        if (readouts_[i].value == v)
            return;
        readouts_[i].value = v;
        readouts_[i].dirty = true;
        draw_readouts();
    }

    void on_data(const Message &m)
    {
        const bool miles = cfg_.num("miles") != 0;
        unsigned km = 0;
        if (cfg_.num("debug")) {
            char hex[128] = "";
            for (size_t i = 0; i < m.data.size() && i < 24; i++)
                std::snprintf(hex + i * 3, 4, "%02x ", m.data[i]);
            log_info("event %04x/%04x [%zu B] %s", (unsigned)m.service, (unsigned)m.event, m.data.size(), hex);
        }
        if (m.service == kVehicleInformation && m.event == kGearSelection) {
            set_readout(0, hs::gear_text(m.data));
        } else if (m.service == kKombi && m.event == kTankRange) {
            if (hs::tank_range_km(m.data, &km))
                set_readout(1, std::to_string(miles ? hs::km_to_miles(km) : km));
        } else if (m.service == kKombi && m.event == kEvRange) {
            if (hs::ev_range_km(m.data, &km))
                set_readout(2, std::to_string(km));   /* already in the display unit */
        }
    }

    /* The diagnostic link's answers: the method's error code (said once per change) and the event with the unit's reply. */
    void on_tasu(const Message &m, int64_t now)
    {
        if (cfg_.num("debug")) {
            char hex[200] = "";
            for (size_t i = 0; i < m.data.size() && i < 60; i++)
                std::snprintf(hex + i * 3, 4, "%02x ", m.data[i]);
            log_info("tasu %04x [%zu B] %s", (unsigned)m.event, m.data.size(), hex);
        }
        if (m.event == kTasuRequest) {
            const int err = m.data.size() == 4 ? (int)(((unsigned)m.data[0] << 24) | (m.data[1] << 16) | (m.data[2] << 8) | m.data[3]) : -1;   /* the enum, 32 bits */
            if (err != last_tasu_err_) {
                last_tasu_err_ = err;
                log_info("diagnostic request answered: %d%s", err, err == 0 ? " (no error)" : err == 1 ? " (pending, the reply follows)" : "");
            }
            return;
        }
        uint8_t ecu = 0;
        std::vector<uint8_t> uds;
        if (m.event != kTasuResponse || !hs::parse_tasu_response(m.data, &ecu, &uds) || ecu != hs::kBdcEcu)
            return;   /* every app's answers come on this event: only the body controller's D347 is ours */
        uint8_t l = 0, r = 0;
        if (hs::lamp_reply(uds, &l, &r)) {
            const int ol = lamps_.state(true, now), orr = lamps_.state(false, now);
            lamps_.reading(l, r, now);
            if (lamps_.state(true, now) != ol || lamps_.state(false, now) != orr || !got_reply_) {
                got_reply_ = true;
                static const char *const kName[] = {"off", "steady", "flashing"};
                log_info("mirror lamps: left %s (%02x), right %s (%02x)", kName[lamps_.state(true, now)], (unsigned)l,
                         kName[lamps_.state(false, now)], (unsigned)r);
            }
        } else if (uds.size() >= 3 && uds[0] == 0x7F && uds[1] == 0x22 && !refused_) {
            refused_ = true;
            log_warn("the body controller refused 22 D347: code %02x", uds.size() > 2 ? (unsigned)uds[2] : 0u);
        }
    }

    void on_message(const Message &m, int64_t now)
    {
        if (m.service == kTasu) {
            on_tasu(m, now);
            return;
        }
        if (m.service != kLight) {
            on_data(m);
            return;
        }
        if (m.event == kStatusIndicators && m.data.size() >= 1) {
            note_control(m.data[0]);
            flasher_.control(m.data[0]);
        } else if (m.event == kStatusIndicators2 && m.data.size() >= 5) {
            note_control(m.data[0]);
            flasher_.control(m.data[0]);
            if (cfg_.num("debug") && m.data[4] != last_pulse_)
                log_info("pulse %u", (unsigned)m.data[4]);
            last_pulse_ = m.data[4];
            flasher_.pulse(m.data[4], now);
        }
    }

    /* Draws what the flasher says now; true when a side changed. */
    void refresh(int64_t now)
    {
        show(left_, flasher_.left_lit(now));
        show(right_, flasher_.right_lit(now));
        show(bs_left_, lamps_.lit(true, now));
        show(bs_right_, lamps_.lit(false, now));
    }

    /* True when the next mirror lamp read is due (every kPollMs, once the diagnostic link is up). */
    bool blindspot_due(int64_t now)
    {
        if (cfg_.num("blindspot") == 0 || !g_tasu_up || now - last_poll_ < hs::kPollMs)
            return false;
        if (!first_poll_)
            first_poll_ = now;
        if (!got_reply_ && !refused_ && !no_answer_logged_ && now - first_poll_ > 8000) {
            no_answer_logged_ = true;
            log_warn("no answer from the body controller to 22 D347 after 8 s");
        }
        last_poll_ = now;
        return true;
    }

    /* Moves the layers when the place file changed (checked every loop, a stat). The file's lines:
     * "left X Y" and "right X Y" (the turn arrow's top-left corner, in the target's pixels), "bsleft X Y" and
     * "bsright X Y" (the blind spot marks), "gear", "gas" and "ev" (the readouts), "#" comments. */
    void apply_place(const std::string &path)
    {
        if (path.empty() || !made_)
            return;   /* before the layers exist there is nothing to move: keep the file for later */
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || (st.st_mtime == place_mtime_ && st.st_size == place_size_))
            return;
        place_mtime_ = st.st_mtime;
        place_size_ = st.st_size;
        FILE *f = fopen(path.c_str(), "r");
        if (!f)
            return;
        char line[128];
        while (fgets(line, sizeof line, f)) {
            char side[8];
            int x, y;
            if (std::sscanf(line, "%7s %d %d", side, &x, &y) != 3)
                continue;
            Readout *ro = nullptr;
            for (Readout &r : readouts_)
                if (!std::strcmp(side, r.name))
                    ro = &r;
            if (ro && ro->ref) {
                ro->lo.x = x;
                ro->lo.y = y;
                client_->set(ro->ref, ro->lo);
                log_info("%s readout moved to %d,%d", side, x, y);
                continue;
            }
            /* the turn arrows ("left", "right") and the blind spot marks ("bsleft", "bsright") */
            Side *s = !std::strcmp(side, "left") ? &left_ : !std::strcmp(side, "right") ? &right_
                    : !std::strcmp(side, "bsleft") ? &bs_left_ : !std::strcmp(side, "bsright") ? &bs_right_ : nullptr;
            if (!s || !s->ref)
                continue;
            s->lo.x = x;
            s->lo.y = y;
            client_->set(s->ref, s->lo);
            log_info("%s moved to %d,%d", side, x, y);
        }
        fclose(f);
    }

    /* The next time the lit state changes by itself, or -1. */
    int64_t next_change(int64_t now) const
    {
        const int64_t a = flasher_.next_change(now), b = lamps_.next_change(now);
        return a < 0 ? b : b < 0 ? a : (a < b ? a : b);
    }

private:
    /* a change of the indicator state, logged (rare: not the pulses) */
    void note_control(uint8_t control)
    {
        if (control == last_control_)
            return;
        log_info("indicators: control %u (%s)", (unsigned)control,
                 control == 0 ? "off" : control == 1 || control == 3 ? "left" : control == 2 || control == 4 ? "right" : control == 8 ? "both" : "?");
        last_control_ = control;
    }

    bool hold() const { return cfg_.num("hold") != 0; }

    void show(Side &s, bool lit)
    {
        if (!s.ref || s.lit == lit)
            return;
        s.lit = lit;
        s.lo.opacity = lit ? (float)cfg_.num("opacity") / 100.0f : 0.0f;
        /* held: the layer stays visible (transparent) so the HUD's slot is already ours at the next flash */
        s.lo.visible = lit || hold();
        client_->set(s.ref, s.lo);
    }

    const Config &cfg_;
    cr::Client *client_;
    hs::Flasher flasher_;
    Side left_, right_, bs_left_, bs_right_;
    hs::MirrorLamps lamps_;
    bool got_reply_ = false, refused_ = false, no_answer_logged_ = false;
    int64_t first_poll_ = 0;
    int last_tasu_err_ = -2;
    int64_t last_poll_ = 0;
    Readout readouts_[3] = {Readout("gear", "gear"), Readout("gas", "gas"), Readout("ev", "ev")};
    bool made_ = false;
    uint8_t last_control_ = 255, last_pulse_ = 0;
    time_t place_mtime_ = 0;
    off_t place_size_ = -1;
};

}  // namespace

int main(int argc, char **argv)
{
    Config cfg;
    const char *defaults[][2] = {
        {"render_socket", "/dev/shm/cluster-render/render.sock"},
        {"target", "hud"},
        {"layer_class", "overlay"},
        {"left", "1"}, {"right", "1"},
        {"arrow_size", "36"},
        {"margin_x", "128"}, {"margin_right_x", "122"}, {"margin_y", "112"},
        {"color", "2bd96b"},
        {"opacity", "100"},
        {"on_ms", "330"},
        {"hold", "1"},
        {"place_file", "/var/opt/mlurker-hud-signals/place"},
        {"readouts", "0"}, {"readout_dir", "v"}, {"readout_gap", "8"}, {"readout_x", "130"}, {"readout_y", "104"}, {"readout_w", "76"},
        {"readout_color", "ffffff"}, {"miles", "1"},
        {"blindspot", "1"}, {"blindspot_w", "51"}, {"blindspot_h", "158"},
        {"blindspot_margin_x", "126"}, {"blindspot_margin_right_x", "121"}, {"blindspot_margin_y", "162"},
        {"debug", "0"},
        {"log_file", ""}, {"log_file_kb", "256"},
    };
    for (auto &d : defaults)
        cfg.define(d[0], d[1]);
    std::string err;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "-c") && i + 1 < argc) {
            if (!cfg.load(argv[++i], &err)) { std::fprintf(stderr, "mlurker-hud-signals: %s\n", err.c_str()); return 2; }
            /* a local conf beside it keeps the owner's choices across updates */
            const std::string local = std::string("/var/opt/mlurker-hud-signals/mlurker-hud-signals.local.conf");
            if (access(local.c_str(), F_OK) == 0 && !cfg.load(local, &err)) { std::fprintf(stderr, "mlurker-hud-signals: %s\n", err.c_str()); return 2; }
        } else if (!std::strcmp(argv[i], "-s") && i + 1 < argc) {
            if (!cfg.set_pair(argv[++i], &err)) { std::fprintf(stderr, "mlurker-hud-signals: %s\n", err.c_str()); return 2; }
        } else {
            std::fprintf(stderr, "usage: mlurker-hud-signals [-c FILE] [-s key=value]...\n");
            return 2;
        }
    }
    if (!cfg.str("log_file").empty())
        log_to_file(cfg.str("log_file"), log_file_cap(cfg.num("log_file_kb")), "mlurker-hud-signals", HS_VERSION);
    const long size = cfg.num("arrow_size");
    if (size < 16 || size > 400 || size % 2) {
        log_error("arrow_size = %ld: 16 to 400, even", size);
        return 2;
    }
    if (cfg.num("blindspot_w") < 8 || cfg.num("blindspot_w") > 200 || cfg.num("blindspot_h") < 8 || cfg.num("blindspot_h") > 200) {
        log_error("blindspot_w and blindspot_h: 8 to 200");
        return 2;
    }
    if (cfg.num("on_ms") < 50 || cfg.num("on_ms") > 600 || cfg.num("opacity") < 10 || cfg.num("opacity") > 100) {
        log_error("on_ms must be 50 to 600 and opacity 10 to 100");
        return 2;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    if (pipe(g_wake) != 0)
        return 1;

    cr::Client client("mlurker-hud-signals", cfg.str("render_socket"));
    Hud hud(cfg, &client);
    client.on_ready = [&] { hud.make_layers(); };
    client.on_layer = [](uint32_t ref, const std::string &state, const std::string &reason) {
        log_info("render: layer %u %s%s%s", ref, state.c_str(), reason.empty() ? "" : " - ", reason.c_str());
    };
    client.on_target = [](const std::string &t, const std::string &state, const std::string &holder, const std::string &reason) {
        log_info("render: target %s %s%s%s%s%s", t.c_str(), state.c_str(), holder.empty() ? "" : " (", holder.c_str(),
                 holder.empty() ? "" : ")", reason.empty() ? "" : (" - " + reason).c_str());
    };
    client.on_error = [](const std::string &op, uint32_t, const std::string &reason) {
        log_error("render: %s refused: %s", op.c_str(), reason.c_str());
    };
    std::thread loop;
    std::shared_ptr<vsomeip::application> app = start_vsomeip(&loop, cfg.num("blindspot") != 0);
    if (!app) {
        log_error("the vsomeip application did not initialise");
        return 1;
    }
    log_info("mlurker-hud-signals %s: %s arrows, waiting for Light and the cluster renderer", HS_VERSION,
             cfg.str("target").c_str());

    while (!g_stop) {
        const int64_t now = now_ms();
        client.poll(now);
        int timeout = 500;
        const int64_t next = hud.next_change(now);
        if (next >= 0)
            timeout = (int)(next - now) + 1 < 1 ? 1 : (int)(next - now) + 1;
        if (cfg.num("blindspot") != 0 && timeout > 100)
            timeout = 100;   /* the lamp read is due every 300 ms, a flash edge every 200 */
        struct pollfd fds[2];
        fds[0].fd = g_wake[0];
        fds[0].events = POLLIN;
        fds[1].fd = client.fd();
        fds[1].events = POLLIN | (client.wants_write() ? POLLOUT : 0);
        poll(fds, client.fd() >= 0 ? 2 : 1, timeout < 200 || client.fd() >= 0 ? timeout : 200);
        if (fds[0].revents & POLLIN) {
            char buf[64];
            ssize_t n = read(g_wake[0], buf, sizeof buf);
            (void)n;
        }
        std::deque<Message> got;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            got.swap(g_queue);
        }
        const int64_t t = now_ms();
        for (const Message &m : got)
            hud.on_message(m, t);
        client.poll(t);
        if (hud.blindspot_due(t))
            send_tasu_read(app, hs::kBdcEcu, hs::kLampDid);
        hud.refresh(t);
        hud.apply_place(cfg.str("place_file"));
    }
    log_info("mlurker-hud-signals: stopping");
    _exit(0);   /* vsomeip's thread has no clean stop worth waiting for */
}
