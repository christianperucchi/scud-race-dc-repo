# Scud Race — Dreamcast tech demo ("what if it shipped in 1998?")

A homebrew Dreamcast program that runs the **Scud Race bay course** from AM2's 1997
SET 2 technical demo, with the **Porsche and Ferrari F40** from the companion `drive2`
demo, on real Dreamcast hardware using [KallistiOS](https://github.com/KallistiOS/KallistiOS).

The original SET 2 demos are Windows executables for a PowerVR PC card and never ran on a
console. This project extracts their Ninja-format scene data and rebuilds the demo the way a
1998 studio would have shipped it: VQ-compressed textures, baked vertex lighting, hardware
table fog, store-queue vertex submission, 30 fps with 60 Hz physics.

> **No SEGA data is included in this repository.** You need your own copy of the
> *Katana SDK 0.20 Pre.7 (SET 2)* and the *Scud Race / Drive2 texture pack*
> (both released by sega-dreamcast-info.com). The scripts generate the game data locally.

## Features
- Full bay course: ~88k triangles in 3,835 cells, frustum + distance culling, near-plane clipping
- Underwater glass tunnel with a fog/colour transition
- 4-car race (2× Porsche, 2× F40), countdown, 2 laps, position, lap time, speedo, gears, rev bar
- Arcade physics at 60 Hz: 6-speed auto gearbox, torque curve, drag, slopes, grip-limited
  steering with drift, wall crashes/scrapes, car-to-car contact
- AI drivers: racing line, curvature-based braking, overtaking
- Real-time synthesized audio on the AICA stream: engines, skids, wind, crashes, start beeps
- Cameras: chase, bonnet, helicopter, trackside TV; attract mode until you press Start

## Repository layout
| Path | Contents |
|---|---|
| `src/game.c` | The Dreamcast program (KallistiOS, C) |
| `tools/` | Python asset pipeline: Ninja extractor, course/car/texture converters, VQ encoder, driving line |
| `scripts/` | Toolchain setup, asset build, CDI creation |
| `video/` | Deterministic capture + audio resynthesis pipeline used to make the demo video |

## Building
1. Toolchain: a standard KallistiOS setup works (kos-chain or DreamSDK). On Ubuntu 24.04,
   `scripts/setup_toolchain_ubuntu.sh` reproduces the setup used during development
   (distro `gcc-sh-elf` + newlib 4.6 with the KOS patch).
2. Assets (Python 3 with numpy and Pillow):
   ```sh
   export KATANA_SDK="/path/to/Katana SDK Version 0.20 Pre.7/sdk"
   export TEXTURE_PACK="/path/to/Texture pack for Scud Race and Drive2"
   scripts/build_assets.sh
   ```
3. Game and disc:
   ```sh
   source /opt/toolchains/dc/kos/environ.sh
   make            # scudrace.elf  (Flycast, dc-tool-ip / dc-tool-serial)
   make cdi        # scudrace.cdi  (needs genisoimage + cdi4dc from img4dc)
   ```

## Controls
Stick: steer · A / R trigger: throttle · B / L trigger: brake · Y: camera · Start: take control from attract mode

## Notes and limits
- Physics and AI are original arcade-style code, not Scud Race's. The SET 2 demos contain no game logic.
- Frame-rate figures so far come from Flycast; real-hardware testing is welcome (open an issue with results).
- Don't distribute built ELF/CDI files: they contain SEGA assets.

## Credits
AM2/SEGA for the original course and car data · sega-dreamcast-info.com for preserving the SET 2 SDK
and texture pack · Sifting for the Kamui wrapper research · the KallistiOS team · Flycast.

---
### En español
Demo homebrew para Dreamcast que ejecuta el circuito de la bahía de Scud Race de la demo técnica
AM2 (SET 2) con el Porsche y el F40 de la demo `drive2`, física arcade, IA y sonido sintetizado.
El repositorio **no incluye datos de SEGA**: necesitas tu propia copia del SDK Katana 0.20 y del pack
de texturas, y `scripts/build_assets.sh` genera los datos en tu equipo.
