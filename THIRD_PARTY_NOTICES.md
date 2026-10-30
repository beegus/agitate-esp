# Third-party notices

The MIT license in `LICENSE` applies only to original agitate-esp project code.
Firmware and 3DS builds also use third-party libraries and toolchain
components. Their copyright notices and license terms remain in force.

## Direct firmware dependencies

| Component | Version | License | Upstream |
|---|---:|---|---|
| ESPAsyncWebServer | 3.12.1 | LGPL-3.0 | [Source and license](https://github.com/ESP32Async/ESPAsyncWebServer/tree/3.12.1) |
| AsyncTCP | 3.5.0 | LGPL-3.0 | [Source and license](https://github.com/ESP32Async/AsyncTCP/tree/3.5.0) |
| ArduinoJson | 7.4.3 | MIT | [Source and license](https://github.com/bblanchon/ArduinoJson/tree/7.4.3) |
| Arduino-ESP32 framework | 3.3.12 | LGPL-2.1-or-later | [Source and license](https://github.com/espressif/arduino-esp32/tree/3.3.12) |
| Arduino-ESP32 precompiled framework libraries | 5.5.5 (`sha.b774170ff46`) | LGPL-2.1-or-later package metadata; bundled components may have their own terms | [Framework package](https://github.com/espressif/arduino-esp32/releases/tag/3.3.12) |

PlatformIO resolves these versions from `platformio.ini` and the pinned
pioarduino platform. The framework packages include additional Espressif and
ESP-IDF components; component-level notices and licenses may differ from the
package-level identifier above. Review the exact package manifests and
component notices for the build being distributed.

## Firmware binary distribution

Prebuilt firmware images are published on the repository's Releases page. They
are built by the `Release` GitHub Actions workflow from the tagged source, with
the dependency versions pinned in `platformio.ini`. ESPAsyncWebServer, AsyncTCP,
and the Arduino-ESP32 framework are LGPL and statically linked, so each release
also includes:

- `agitate-esp-X.Y.Z-source.tar.gz`: the complete project source and build
  scripts, plus the exact ESPAsyncWebServer, AsyncTCP, ArduinoJson, and
  Arduino-ESP32 (`cores`, `libraries`, `variants`) source used for the build,
  each with its license file. With it you can modify any of these libraries and
  rebuild and relink the firmware using the steps in `README.md`.
- `LGPL-2.1.txt`, `LGPL-3.0.txt`, and `GPL-3.0.txt`, also kept in `licenses/`.
- This file and `LICENSE`.

The precompiled ESP-IDF libraries in the Arduino-ESP32 framework package are
built from ESP-IDF v5.5.5 (`b774170ff46`) and components whose versions are
listed in that package's `versions.txt`. ESP-IDF is Apache-2.0, and its
components carry their own permissive licenses; see the
[ESP-IDF source](https://github.com/espressif/esp-idf/tree/v5.5.5) and each
component's license files.

This is a good-faith summary, not legal advice.

## Nintendo 3DS companion application

Prebuilt `.cia` and `.3dsx` files are published in the same releases. They are
built by the `Release` GitHub Actions workflow in the
`devkitpro/devkitarm:20260610` image.

| Component | License | Notes |
|---|---|---|
| [libctru](https://github.com/devkitPro/libctru) | zlib | Statically linked; notice reproduced below |
| devkitARM newlib C library | Various permissive licenses | Statically linked; see newlib's [`COPYING.NEWLIB`](https://sourceware.org/git/?p=newlib-cygwin.git;a=blob;f=COPYING.NEWLIB) |
| devkitARM GCC runtime and libstdc++ | GPL-3.0 with GCC Runtime Library Exception | The exception permits distributing the compiled app under any license |
| [makerom](https://github.com/3DSGuy/Project_CTR) 0.18.4, [bannertool](https://github.com/diasurgical/bannertool) 1.2.0, devkitPro `3dstools` | Their own terms | Build-time packaging tools only; not included in the app |

libctru license notice:

> This software is provided 'as-is', without any express or implied warranty.
> In no event will the authors be held liable for any damages arising from the
> use of this software.
>
> Permission is granted to anyone to use this software for any purpose,
> including commercial applications, and to alter it and redistribute it
> freely, subject to the following restrictions:
>
> 1. The origin of this software must not be misrepresented; you must not claim
>    that you wrote the original software. If you use this software in a
>    product, an acknowledgment in the product documentation would be
>    appreciated but is not required.
> 2. Altered source versions must be plainly marked as such, and must not be
>    misrepresented as being the original software.
> 3. This notice may not be removed or altered from any source distribution.
