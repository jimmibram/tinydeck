# mini-pc

A small operating system for the Waveshare ESP32-S3-RLCD-4.2: a 400x300
reflective LCD driven by an ESP32-S3 (16 MB flash, 8 MB PSRAM), used as a
text terminal with a Bluetooth Low Energy keyboard.

## Layout

- `firmware/` ESP-IDF project
  - `components/rlcd` ST7305 display driver (packed 1-bpp framebuffer, full-frame SPI flush)
  - `components/font8x16` 8x16 bitmap font (ASCII + Latin-1), generated from the kbd `lat1-16` console font
  - `components/term` 50x18 character terminal with cursor, scrolling, UTF-8 input
  - `components/ble_kbd` BLE HID keyboard host (scan, bond, HID report -> bytes, key repeat)
  - `main/` input queue, USB console reader, shell
  - `tools/psf2c.py` font converter, `tools/99-espressif.rules` udev rule
- `logs/` build and tool logs

## Toolchain

ESP-IDF v5.5.5 lives in `~/esp/esp-idf`, tools in `~/.espressif`. ESP-IDF does
not support Python 3.14 yet, so `~/esp/pybin` shims `python3` to the uv-managed
Python 3.12. Activate everything with:

```sh
cd firmware
. ./env.sh
```

## One-time: serial port access

The board shows up as `/dev/ttyACM0` (Espressif USB JTAG/serial, 303a:1001),
owned by root:uucp. Either install the udev rule or join the uucp group:

```sh
sudo cp firmware/tools/99-espressif.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

## Build, flash, monitor

```sh
cd firmware
. ./env.sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor      # Ctrl-] exits the monitor
```

Anything typed in the monitor is fed into the on-screen shell too, so the
terminal can be tested before a keyboard is paired.

## Keyboard

The ESP32-S3 has BLE only, no Classic Bluetooth. The keyboard must support
Bluetooth Low Energy HID. On boot the firmware scans, connects to a bonded
keyboard if one is seen, otherwise to the strongest keyboard-like device. If
the keyboard asks for a passkey, the code is shown on the screen; type it on
the keyboard and press Enter. `kbd pair` in the shell clears bonds and pairs a
new keyboard. Layout is US for now.

## Screenshots from the device

With the board connected, `python3 firmware/tools/screenshot.py /dev/ttyACM0 shot.png`
asks the firmware for its framebuffer (Ctrl-\ on the USB console) and renders it
as a PNG. Useful for checking layouts without looking at the panel.
