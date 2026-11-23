# ZB-CT01 Firmware — CCT controller with very low minimum brightness

Alternative Zigbee firmware for the **eWeLight / Tuya ZB-CT01** dual-white (CCT) LED
controller — Tuya module **ZYZB010**, chip **Telink TLSR8258**.

This is a fork of **[doctor64/tuyaZigbee](https://github.com/doctor64/tuyaZigbee)**, which is
itself built on Telink's official **tl_zigbee_sdk** (Apache License 2.0). This fork keeps the
upstream `light` application and only adapts it to the ZB-CT01 hardware and rewrites the
brightness curve.

## Why this fork

The stock firmware (and the generic upstream `light` app) cannot dim low: the minimum
brightness is too high and there are no fine steps near the bottom. This fork fixes the
hardware mapping and rewrites the dimming curve so the controller can drive a
**BTF-Lighting FCOB 24V CCT strip** (warm + cool, e.g. RA90 3000–6000 K) all the way down to a
**much lower, smooth minimum** — while still reaching full brightness.

## What this changes vs. upstream

- **Low-end dimming fix (the main goal).** Upstream clamped the minimum level to ~6 % and used
  an integer gamma that truncated low values to zero. This fork computes the PWM duty at full
  resolution with a measured floor (~0.075 % duty), so the strip dims smoothly down to a
  barely-visible minimum. A channel computed as 0 is turned fully off, so the color stays clean
  at the color-temperature extremes.
- **Correct pin mapping for the ZB-CT01 board.** Cool = **PB5 / PWM5**, Warm = **PB4 / PWM4**
  (verified on real hardware over SWire; the commonly-cited PC2/PC3 mapping is wrong for this
  board). PWM runs at **6 kHz**.
- **CCT mixing.** The level sets the total light and the color temperature only splits it
  between the warm and cool channels, so moving the color temperature does not change the
  brightness and the lowest levels keep their color.
- **Zigbee model id `ZB-CT01`.** Zigbee2MQTT recognizes it natively as
  **YSRSAI YSR-MINI-01_wwcw** (brightness + color temperature); no custom converter needed.
- **Physical button on PD3.** Short press (on release) = toggle, also without a network; long
  press (≥ 5 s) = factory reset.
- **After a power cut the light stays off** by default (`startUpOnOff`, changeable from
  Zigbee2MQTT). Resets that are not a power cut (watchdog, exception, OTA) keep the state.
- **Zigbee behaviour fixes** in the Telink sample code: brightness "with On/Off" commands,
  transitions that stopped short, scene recall, identify, rejoin retries, default reporting of
  level and color temperature, watchdog enabled. See the git history for details.
- **Build / flash configuration.** `CHIP_TYPE = TLSR_8258_512K`, `BOOT_LOADER_MODE = 0`
  (app at `0x0`, no bootloader). Flash at `0x0` over SWire; the factory MAC at `0x76000` is
  preserved. The build fails if the image does not fit before the NV area (`0x34000`).

## Target hardware

- Module **ZYZB010** (Tuya), model **ZB-CT01** (eWeLight).
- MCU **Telink TLSR8258**, 512 KB flash (JEDEC `C8 60 13`), chip id `0x5562`.
- Pins used in normal operation: `PB4` (warm PWM), `PB5` (cool PWM), `PD3` (button).
  `PA7`/SWS (pin 3) and RST (pin 24) are only used for flashing.

## Build

Requires Docker (the Telink `tc32` toolchain is Linux-only). The Telink SDK is downloaded at
build time and is **not** vendored in this repository.

```sh
docker build -f Dockerfile.tc32 -t tlsr-tc32-build .
docker run --rm --platform linux/amd64 -v "$PWD":/work -w /work tlsr-tc32-build \
  bash -lc 'cd /work && cmake . -B build && cmake --build build --target light.zigbee'
# output: build/light/light.bin (SWire) and build/light/6464-0211-<version>-light.zigbee (OTA)
```

The version is `APP_RELEASE` / `APP_BUILD` in `common/version.h`; raise it for every OTA image.

## Flash

Flash `build/light/light.bin` at address `0x0` over SWire, **erasing by sector** (not a
chip-erase) so the factory MAC at `0x76000` is preserved. Any TLSR82xx SWire programmer works.

**When flashing over the stock firmware, also erase the NV area.** The stock firmware uses the
same Telink NV format, so this build loads its saved state (old network, light off): the light
stays dark, never searches for a network and the button does nothing. Erase these sectors once
(MAC at `0x76000` and calibration at `0x77000` are outside them):

| Range | Content |
|---|---|
| `0x34000`–`0x3FFFF` | NV modules 0-5 |
| `0x79000`–`0x79FFF` | factory-reset flag |
| `0x7A000`–`0x7FFFF` | NV modules 6+ |

On first boot after that, the light stays off and starts looking for a network; the button
turns it on.

## OTA update

The firmware includes the Zigbee OTA client (manufacturer `0x6464`, image type `0x0211`). Without
a bootloader, Telink downloads the new image to `0x40000` and boots from there; the next update
goes back to `0x0`. Both slots hold up to 208 KB.

Serve the `.zigbee` file from Zigbee2MQTT with a local OTA index and start the update from the
device's OTA tab. The device asks for updates every 6 hours, or when Z2M checks.

> After an OTA the running image may be at `0x40000`. To go back to SWire flashing at `0x0`,
> erase `0x40000`–`0x73FFF` as well, or the chip may keep booting the image there.

## Pairing (Zigbee2MQTT)

It reports model id `ZB-CT01`, so Z2M maps it to the built-in **YSRSAI YSR-MINI-01_wwcw**
definition (brightness + color temperature). If a stale entry for the device already exists,
remove it and pair fresh.

## Credits & license

- Upstream project: **[doctor64/tuyaZigbee](https://github.com/doctor64/tuyaZigbee)**.
- Underlying SDK: **Telink `tl_zigbee_sdk`**, © Telink Semiconductor, Apache License 2.0.

Distributed under the **Apache License 2.0** (see [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE)).
Source files keep their original Telink copyright headers; modified files note the changes, as
required by the license.
