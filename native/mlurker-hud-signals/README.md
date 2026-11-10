# mlurker-hud-signals

The car's signals on the HUD, drawn through the shared cluster renderer ([cluster-render](../cluster-render)),
with or without the map:

- **turn indicators:** a green arrow at the bottom left and one at the bottom right, each flashing with its lamp;
- **blind spot lamp** (`blindspot = 1`, the default; `0` turns it off): glowing yellow arcs at the left or right edge of the
  visible window, centred vertically, while the lane change warning lamp in that exterior mirror is on, steady or flashing like the lamp
  (see "Blind spot lamp");
- **three readouts** in a vertical strip in the top-left corner of the HUD's visible window, each a small title over a bold
  value: **gear** (P, R, N, D1 to D9), **gas** (the fuel range, miles) and **ev** (the electric range).

Findings behind it: [HUD-SIGNALS.md in mlurker/idrive7-aa](https://github.com/mlurker/idrive7-aa/blob/main/scripts/car/hud-and-signal-utils/HUD-SIGNALS.md). Tried on a G05 (P18_23w44): the arrows
show and flash, and the readouts match the cluster.

## How it works

- **Signals in.** A vsomeip application (`mlurker-hud-signals`) as `hmi_5000`, the one uid whose security policy allows
  the services it needs, with the unit's own `/etc/vsomeip`. It subscribes to
  - `de.bmw.body.Light`: `StatusFunctionsIndicators` (which side is on) and `StatusFunctionsIndicators2` (the same
    plus a pulse byte that is non-zero for about 80 ms at the start of each lamp-on phase);
  - `VehicleInformation` (0x1536): `DataPowerTrain2` (the gear selector, 1 byte: 1 N, 2 R, 3 P, 5 to 13 D);
  - `KombiIF` (0xB51B): `statusRangeTankStatus` (km, converted to miles and rounded down like the cluster) and
    `displayRangeElectricVehicle` (already in the car's display unit).
- **Flashing.** Each pulse starts a lamp-on phase of `on_ms` (default 330 ms) on the side(s) that are on; control
  off puts them out at once (`src/indicators.cpp`, tested against the sequence measured on a G05). Hazard lights
  (control 8) light both sides.
- **Arrows out.** Two image layers on the `hud` target (800×480; the car shows only its middle part, about
  x 126–679, y 99–379, and the 36 px arrows sit in that window's bottom corners, clear of the car's own
  speed-limit sign) of cluster-render, class `overlay`, drawn by code (`src/arrows.cpp`, anti-aliased, one
  colour). A flash is an opacity change on a layer that is already there, nothing is uploaded per flash. By
  default the layers stay on the HUD, transparent, between flashes (`hold = 1`), so the HUD slot is already ours.
- **Readouts out.** Three image layers (76×46 px each), re-drawn only when a value changes. The text is drawn by
  code from a 5×7 dot font sampled smoothly (`src/readouts.cpp`), so the service ships no font file. It is not
  BMW's typeface, which is proprietary and drawn by the car's display firmware.
- **Reconnects.** cluster-render's client library sends the layers again after the renderer restarts; vsomeip's
  availability changes are logged.

## Blind spot lamp

With `blindspot = 1` the service asks the body domain controller (BDC, address 0x40) for the identifier `D347`
(`AUSSENSPIEGEL_HC2`) every 300 ms, a **read-only diagnostic request** (UDS 22, never anything else: `src/blindspot.cpp`
builds only that one request; the same identifier takes a write, 2E, that drives the lamp, which this code does not
contain) through the head unit's own diagnostic service (`InternalTasu`, 0xF8A6, which `hmi_5000`'s policy allows). The
answer is two bytes, the state of the lane change warning lamp in the left and the right exterior mirror: 0 off, 1 on
steady (a vehicle in the blind spot), 2 to 4 on flashing (levels 1 to 3: a warning, the indicator on towards it), 0x0D
display error, 0xFF invalid (from the public description of BDC_G05). Glowing yellow arcs (default
51 x 158 px) show at that side's edge of the visible window: steady for 1, blinking (200 ms on, 200 ms off) for 2 to 4. The marks go
out when the car says off, or 3 s after the last answer. The log says `mirror lamps: left <off|steady|flashing> (xx),
right ...` at each change, and `no answer ... after 8 s` or `refused 22 D347` when the unit does not give it.

Settings: `blindspot`, `blindspot_w`, `blindspot_h`, `blindspot_margin_x`, `blindspot_margin_right_x`,
`blindspot_margin_y` (the left / right / bottom distance from the HUD scene's edges; the defaults put the arcs at the visible window's
left and right edges, centred). `debug = 1` shows the raw answers. Version 0.3.0 read the assist sensors unit's `6601` instead (the lane
change warning, a short event, not the lamp); it was replaced.

Manual mode (M) cannot be shown: the car sends the same gear number in D and in manual. The blind spot services are
not delivered to the head unit's apps at all (see the findings); the mirror lamp's state above comes by a diagnostic read.

## Settings

`mlurker-hud-signals.conf` (a `/var/opt/mlurker-hud-signals/mlurker-hud-signals.local.conf` overrides it and survives updates): which
sides, the arrows' size and distance from the bottom and side edges (`margin_x` for the left arrow,
`margin_right_x` for the right one, `margin_y`), colour, brightness, `on_ms`, `hold`, the readouts (`readouts`,
`readout_dir` (`v` a vertical strip, `h` a row), `readout_x`, `readout_y`, `readout_gap`, `readout_w`, `readout_color`, `miles`), the layer class and a log file. Changes are read
at the next start of the service (a restart takes seconds, no head-unit reboot).

**Live positions.** While it runs, the service watches the file `/var/opt/mlurker-hud-signals/place` (`place_file`).
Lines like `left 122 332`, `right 632 332` (the turn arrows), `bsleft 126 351`, `bsright 654 351` (the blind spot
marks), `gear 124 104`, `gas 124 158`, `ev 124 212` (the layer's top-left corner in the HUD's pixels) move the layers at
once. The file is applied again at each start of the service, once the layers exist. The conf's defaults are the placement chosen on a G05.

## Start at boot: after waze-on-cluster

The service starts after `waze-on-cluster.service` and waits 15 s (`systemd/mlurker-hud-signals.service`). Both connect to
the renderer at boot, and if mlurker-hud-signals is the **first** client the cluster map can loop (shown, then "the
display reports the scene mapped but its renderer is not subscribed", every ~32 s) until the next reboot;
stopping mlurker-hud-signals does not end it. The order is a race otherwise, because both services start in parallel
after `cluster-render.socket`. This is the likely cause, not a proven one: the log of the bad boot shows
mlurker-hud-signals as client 1, and a boot without it, or with it after waze-on-cluster, was clean.

## Build, install, remove

```sh
make test      # the flasher, arrows, gear and range decoders and the text, on this machine
make target    # build/target/mlurker-hud-signals for the unit (Docker, the cluster-render toolchain)
node <idrive-apps>/release/tool_artifact.js native/mlurker-hud-signals --out kits    # kits/mlurker-hud-signals@0.5.0.tgz
```

A Store tool like the others: `component.json` has the unit, `requires` (cluster-render 0.4.0), and `recmgr` /
`crashNames` so that a restart of the service does not restart the head unit. Install it with the deploy tool,
which restarts only this service:

```sh
mgu-store/deploy/hu install --artifact kits/mlurker-hud-signals@0.5.0.tgz --yes    # check first with: hu check --artifact ...
mgu-store/deploy/hu uninstall mlurker-hud-signals --yes                            # removes the service, its files and the whitelist entry
```

(connection options as for any `hu` command: `--hu-ip`, `--iface`, `--identity`).

## Files

| | |
|---|---|
| `src/main.cpp` | the service: vsomeip, the layers, the place file |
| `src/indicators.*` | the flash state machine (control + pulse → lit sides) |
| `src/arrows.*` | the arrow image |
| `src/readouts.*` | the gear / range decoders and the readout text image |
| `test/hud_signals_test.cpp` | host tests (`make test`) |
| `systemd/mlurker-hud-signals.service` | the unit (user `hmi_5000`, group `tm_oap_app` for the renderer's socket, after waze-on-cluster) |
| `component.json`, `mlurker-hud-signals.conf`, `Makefile`, `LICENSE` | the Store manifest, the default settings, the build, the license |

Test helpers that run on the car (the `signal-listen` probe, a DLT capture, launchers, `hud_grid.js`,
`hud_rect.js`) are in [mlurker/idrive7-aa](https://github.com/mlurker/idrive7-aa/tree/main/scripts/car/hud-and-signal-utils).
