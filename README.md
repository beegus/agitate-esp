# agitate-esp

ESP32-C3 firmware and a phone web UI that automate Paterson tank agitation with a 28BYJ-48 stepper and a
ULN2003 driver. The board runs its own Wi-Fi access point, serves the control page, and agitates on a schedule
through pre-soak, developer, stop bath, and fixer. Washing is done by hand.

This is an experimental hobby project, provided as-is. Read [Safety and liability](#safety-and-liability)
before building or using it.

## Safety and liability

**You build and use this entirely at your own risk.** To the fullest extent permitted by law, the authors and
contributors accept no liability for any damage, loss, or injury arising from it. That includes fire, electric
shock, damage to property or equipment, exposure to chemicals, and ruined film. The software is provided
"as is", without warranty of any kind, as stated in the [MIT license](LICENSE). This is not certified safety
equipment or a laboratory instrument, and nothing here is professional electrical, chemical, photographic, or
legal advice.

- **Electrical and fire:** use only a regulated 5 V DC supply from a reputable maker. Never use mains voltage
  or 12 V. Insulate every joint, keep the electronics dry and away from flammable material, don't leave the
  device powered and unattended, and unplug it when not in use.
- **Chemicals:** follow each chemical's label and safety data sheet. Work with ventilation and protective
  equipment, and mount the electronics where nothing can splash or drip on them.
- **Mechanical:** the motor can start from any connected phone. Unplug it before adjusting the mechanism or
  wiring.
- **Processing:** the firmware only runs timers and the motor. It cannot check chemicals, temperature,
  dilution, or results. Confirm times with the film and chemical makers, check that the tank is still
  light-tight with the coupler fitted, and test on film you don't care about first.
- **Network:** there is no authentication or encryption. Anyone who joins the access point can control the
  motor. Change the default password (see [Wi-Fi](#wi-fi)), and never expose the device to the Internet.

## Parts

| Qty | Part | Notes |
|---|---|---|
| 1 | [ESP32-C3 SuperMini](https://www.aliexpress.us/item/3256808827955225.html) | Native USB-C |
| 1 | [28BYJ-48 stepper + ULN2003 driver](https://www.aliexpress.us/item/3256806053405517.html) | The **5 V** version, not 12 V |
| 1 | 5 V supply, 1 A or more | USB wall adapter or power bank |
| 6 | Female-to-female Dupont jumpers | Cut one end off and solder it to the ESP32 |
| 2 | [M2 x 4 mm socket cap screws](https://www.aliexpress.us/item/3256808127808927.html) | Mounting |
| 1 | Shaft coupler | Motor to Paterson agitation stick |
| - | Heat-shrink, zip ties | Insulation and strain relief |

Motor mount reference: [Paterson Film Developing Tank Motorization Setup](https://www.printables.com/model/736613-paterson-film-developing-tank-motorization-setup).

## Wiring

| ESP32-C3 | ULN2003 |
|---|---|
| GPIO 4 | IN1 |
| GPIO 5 | IN2 |
| GPIO 6 | IN3 |
| GPIO 7 | IN4 |
| GND | `-` power terminal |
| 5V | `+` power terminal |

- Use only `IN1`-`IN4` on the driver's input header; leave `IN5`-`IN7` empty. Don't add resistors, and leave
  the components already on the driver board alone.
- The motor plugs into the driver's white 5-pin socket. Never plug or unplug it while powered.
- The driver's power input says `5-12V`, but this motor needs **5 V only**. Reversing `+` and `-` destroys the
  driver.
- Pins can be changed in `src/Config.h`. Avoid GPIO 2/8/9 (strapping) and 18/19 (USB).

**Power:** the motor draws about 200-300 mA. The simplest setup powers everything from the ESP32's USB port
through its `5V` pin, using a supply rated 1 A or more (a 500 mA laptop port will brown out). For a permanent
build, you can power the driver from its own 5 V supply instead. In that case **the two grounds must be
connected**, or the motor will stutter or not move.

## Soldering

Only the ESP32 end needs soldering. Cut one connector off each Dupont jumper; the remaining female end pushes
onto the driver's pins.

1. Strip about 3 mm from each cut end. Tin the wire and the pad, then solder the four signal wires, GND, and
   5V to the ESP32 (skip 5V if the driver has its own supply).
2. GPIO 4-7 sit next to each other. Check under good light that there are no solder bridges.
3. Heat-shrink each joint and zip-tie the bundle so nothing pulls on a pad.
4. Before powering up, use a multimeter to confirm each GPIO reaches its IN pin, nothing is shorted to its
   neighbor or to `+`/`-`, and GND reaches `-`. Then double-check the 5 V polarity.

## Flash the firmware

Install [PlatformIO](https://docs.platformio.org/en/latest/core/installation/index.html), then from the
repository root:

```bash
pio run -t upload      # flash firmware
pio run -t uploadfs    # flash the web pages and any local profiles in data/
pio device monitor     # serial console at 115200
```

Run `uploadfs` again whenever you change anything in `data/`; without it the board can't serve its pages. If
upload fails or the port disappears, hold **BOOT**, tap **RESET**, release **BOOT**, and retry.

## First power-up

1. Join the Wi-Fi network `agitate-esp-XXXXXX` (unique per board) with password `developfilm`.
2. The control page should open by itself. If it doesn't, browse to `http://192.168.4.1`.
3. Press **Test Motor**. The shaft should turn about half a turn, pause, and return.
4. Run a full cycle with an empty tank before developing anything.

| Symptom | Fix |
|---|---|
| Motor buzzes but doesn't turn | Swap the IN2 and IN3 wires |
| Moves only half as far as expected | Check `kStepsPerRevolution` in `src/Config.h` (4096 for a standard 28BYJ-48) |
| Stalls, or the ESP32 reboots mid-sweep | Supply is too weak: use a stronger one or a separate driver supply, or lower `kAgitationRpm` |

## Using it

1. Pick a film, developer, and dilution from your [profiles](#profiles), or enter a custom developer time.
2. Press **Start**. When a step's timer reaches zero the station waits: change chemicals, press
   **Next/Skip**, then **Start Step**. The next step never starts on its own.
3. **Pause/Resume**, **Restart Step**, and **Stop** work at any time. **Aggressive Agitation** runs twenty
   quick 30-degree twists during an active step.

Agitation is timed from the start of each step:

| Step | Duration | Agitation |
|---|---|---|
| Pre-soak | 1:00 | first 10 s, then 4 s every 30 s |
| Developer | per run | first 10 s, then 5 s every minute |
| Stop bath | 0:45 | continuous |
| Fixer | 5:00 | first 10 s, then 5 s every minute |

These defaults live in `src/Workflow.cpp`. Motion settings (speed, sweep angle) are in `src/Config.h`.

## Wi-Fi

The access point is always on. To also put the device on your home network (needed for the 3DS app), join the
access point and open `http://192.168.4.1/wifi` on your phone. From that page you can:

- **Join home Wi-Fi (2.4 GHz only):** the controller is then at `http://agitate-esp.local`, or the IP the page
  shows. If the router is unavailable, the device retries in the background and the access point keeps
  working.
- **Forget home Wi-Fi.**
- **Change the access point password.** The access point restarts and phones must rejoin. A full flash erase
  (`pio run -t erase`) restores `developfilm`.

These settings can only be changed from the access point, not from the home network. The ESP32 has one radio,
so the access point moves to the router's channel when it connects. Phones on the access point may briefly
drop at that moment.

## Nintendo 3DS app

The `3ds/` folder contains a native 3DS client. Download the `.cia` or `.3dsx` from the repository's Releases
page (a homebrew-enabled 3DS is required), connect the 3DS to the same home network as the ESP32, and the app
finds the controller automatically. See [3ds/README.md](3ds/README.md) for installing, building, and controls.

## Profiles

No development-time data is included. To get film/developer choices, add your own JSON files to
`data/profiles/` and run `pio run -t uploadfs`. Each file is an array of entries:

```json
[
  {
    "filmId": "examplefilm400",
    "filmName": "Example Film 400",
    "developerId": "exampledeveloper",
    "developerName": "Example Developer",
    "dilution": "1+1",
    "asaIso": "400",
    "timeMinutes": 10,
    "filmSize": "35mm",
    "notes": ""
  }
]
```

`filmId`, `developerId`, `dilution`, and `timeMinutes` are required. Profile files are ignored by Git. Only
use data you have the right to use, and don't commit or share third-party charts. For example, the Massive Dev
Chart's terms forbid republishing it online.

## Development

```bash
python3 scripts/dev_server.py     # web UI at http://localhost:8080, no hardware needed
pio test -e native                # workflow and stepper tests
python3 -m unittest discover -s tests
make -C 3ds test                  # 3DS client parsing tests
```

The firmware is pinned to pioarduino `55.03.312-1` (Arduino-ESP32 3.3.12). CI runs all of the above plus a
firmware build on every push. Pushing a `3ds-vX.Y.Z` tag publishes a 3DS release.

<details>
<summary>HTTP API</summary>

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/status` | Status snapshot |
| GET | `/api/profiles` | Profile filenames on the device |
| POST | `/api/start` | Start a run (body: `film`, `developer`, `fixer`, `dilution`, `customTimeMinutes`), or resume / start the current step |
| POST | `/api/pause` | Toggle pause |
| POST | `/api/restart_step` | Restart the current step |
| POST | `/api/next` | Advance to the next step |
| POST | `/api/stop` | Stop the run and motor |
| POST | `/api/motor/test` | One test sweep (idle only) |
| POST | `/api/motor/aggressive` | Twenty quick 30-degree cycles |
| WS | `/ws` | Status pushed every second |
| GET | `/wifi`, `/api/network` | Wi-Fi setup page and status (AP only) |
| POST | `/api/wifi` | Save and join home Wi-Fi (body: `ssid`, `password`; AP only) |
| POST | `/api/wifi-forget` | Forget home Wi-Fi (AP only) |
| POST | `/api/ap-password` | Change the AP password (body: `password`; AP only) |

Requests must use one of the device's own addresses as `Host` (`192.168.4.1`, its home-network IP, or
`agitate-esp.local`). Browser POSTs and WebSockets must also send a matching `Origin`. These checks guard
against cross-site requests and DNS rebinding. Text fields are limited to 64 characters, and request bodies
to 1 KB.

</details>

## License

Original agitate-esp code is MIT licensed; see [LICENSE](LICENSE). The license doesn't cover third-party
data, trademarks, or dependencies; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Product names are
used for identification only, and this project is not affiliated with or endorsed by any vendor mentioned.
