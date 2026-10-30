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

This repository currently provides project source and build instructions, not
prebuilt firmware releases. A notice list alone is not a substitute for
compliance with the applicable LGPL terms. Before distributing prebuilt
firmware, review the LGPL versions above for the chosen distribution method.
Those terms can require providing the license texts and corresponding library
source, and allowing recipients to modify or replace the LGPL libraries and
relink the combined firmware. Any applicable installation information and
other notices must also accompany the distribution.

Use the source and license files from the exact dependency versions used for
the build. See each upstream link above and the Espressif package/component
manifests. Do not treat this summary as legal advice or as confirmation that a
particular binary distribution method satisfies every license condition.

## Nintendo 3DS companion application

Prebuilt `.cia` and `.3dsx` files are published on the repository's Releases
page. They are built by the `3DS app` GitHub Actions workflow in the
`devkitpro/devkitarm:20260610` image, and each release includes this file and
`LICENSE`.

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
