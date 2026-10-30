# agitate-esp 3DS client

This is a native Nintendo 3DS homebrew client for the agitate-esp controller.
It provides process setup, workflow status, motor state, and the same controls
as the web UI. It uses the ESP32's local HTTP API; it does not embed a browser
or require an internet connection.

## Connect and use

1. Use a phone to join the ESP32 AP `agitate-esp-XXXXXX` (default password:
   `developfilm`) and configure its home Wi-Fi at `http://192.168.4.1/wifi`.
   The ESP32 keeps broadcasting its AP as a phone-accessible fallback.
   Once the ESP32 joins the router, its browser interface is also available at
   `http://agitate-esp.local` from devices on that same LAN.
2. In 3DS System Settings, connect to the same home Wi-Fi network as the ESP32.
   Avoid guest networks or client isolation, which can block device-to-device traffic.
3. Launch the client. It resolves `agitate-esp.local` with mDNS to find the
   ESP32's current home-network address; no address entry is required. If mDNS
   fails, it falls back to local UDP discovery and then tries the direct AP
   address `192.168.4.1`. In Setup, use **Find controller** to retry immediately
   or **Controller IP** to edit the ESP32's station IP shown on the Wi-Fi setup page. Use
   Left/Right to select an octet, Up/Down to adjust it by one, L/R to adjust
   by sixteen, A to save, and B to cancel.
4. The top screen shows the connection, active step, countdown, motor state,
   workflow, selected profile, and developer time. Status is refreshed about
   once per second.
5. Tap the top line on the bottom screen to switch between Controls and Setup.
   The physical left/right buttons also switch tabs.

The app loads profile JSON from the controller when connected. It includes no
film/developer chart data. Setup mirrors the webpage's linked Film, Developer,
and Dilution selectors. Press A or tap a selector to open its paged list; tap
an option or use Up/Down and A. Left/Right cycles the current choice, holding
repeats, L/R skips 16 choices, and X/Y skips 64. In a picker list, L/R changes
pages and X/Y skips five items. Use Select on a selector to manually edit its
ID. Fixer is a short list; the Custom fixer option matches the webpage.

Set a custom developer time in Setup. A blank or zero custom time uses the
selected profile time, or 10 minutes when using manual setup.

### Controls

On the Controls tab, tap an action row or use its physical shortcut:

| Button | Action |
|---|---|
| A | Start, resume, or begin the next step |
| B | Pause / resume |
| X | Restart current step |
| Y | Next / Skip |
| R | Stop process |
| L | Test motor |
| Select | Aggressive agitation |
| Start | Exit the app |

The app does not start a process automatically when it connects. The ESP32
continues its workflow if the 3DS disconnects; reconnect and relaunch or refresh
the status to view it again.

## Install

Prebuilt files are attached to each `3ds-v*` release on the repository's
Releases page. Both need a 3DS that can already run homebrew:

- `AgitateESP3DS.cia`: install with a CIA installer such as FBI; it then
  appears on the HOME Menu.
- `AgitateESP3DS.3dsx`: copy to `/3ds/` on the SD card and launch it from the
  Homebrew Launcher.

To publish a new release, push a tag such as `3ds-v1.1.0`. The `3DS app`
GitHub Actions workflow builds both files, sets the CIA version from the tag,
and creates the release. Every push that changes `3ds/` also runs a build, with
the files available as a workflow artifact.

## Build

Install devkitARM with libctru and the devkitPro `3dstools` package
(`smdhtool`, `3dsxtool`), plus
[makerom](https://github.com/3DSGuy/Project_CTR/releases) and
[bannertool](https://github.com/diasurgical/bannertool/releases). Python 3 is used to generate the small
home-menu icon, banner, and silent banner audio at build time. The generated
assets and compiled files are ignored by Git.

From this directory:

```sh
make        # builds AgitateESP3DS.cia (VERSION=1.2.3 sets its version)
make 3dsx   # optionally builds AgitateESP3DS.3dsx
make test   # runs host-side JSON and API-model tests
make clean
```

The Makefile defaults to `/opt/devkitpro`; set `DEVKITPRO` and `DEVKITARM` if
your installation uses another location. The CIA is an unencrypted homebrew
package. Installing a CIA requires a 3DS already set up to run homebrew and a
CIA installer; a standard retail system cannot install this package.

The CIA statically links libctru. Its zlib License notice is included in the
repository's [third-party notices](../THIRD_PARTY_NOTICES.md).
