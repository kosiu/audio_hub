# Audio Hub 2.0 idea and migration

## Hardware

Diagram:

![Audio Hub 2.0 hardware view](doc/hardware.svg)

GPIO still not conected which means:

1. STB (standby activated with LO) doesn't work no saving energy
2. Optical switch set on auto (not perfect) - work simmilar to pevious switch
   by cycling Auto, Ch1, Ch2... pushing button
3. Important is that even in Alsamixer volume is set to 0 it is still laud
4. I don't have active filters on subwoofer, other speakers has it, but minimal one
5. I messup connection of speakers (swaped central with subwoofer)
6. During migration DAC GPIO selector and LED feedback stay commented out in
   `devices.py`. Uncomment only those lines when the external connector is back.

## Software migration

1. Concept of the migration is do smallest changes possible onece at the time
2. Minimal config for now is to have radio working (with working volume controll)
3. Everytime updating system files new file is added to `system_files`
4. Everytime new package is installed information is added on last chprer here 
   `installation`
5. `CamillaDSP` will be central part of the system for audio transformation and
   should take all heavy lifting, ALSA interface super minimal configuration.
6. Version 1.0 is described here: [AudioHub v1.0](doc/v1_documentation.md)


## Current Notes

1. Database of streams: http://fmstream.org/index.php

Remote layout, left to right and top to bottom:

```text
KEY_POWER
KEY_MUTE
KEY_PAGEUP
(mouse button is not an event)
KEY_PAGEDOWN
KEY_UP
KEY_DOWN
KEY_LEFT
KEY_RIGHT
KEY_SELECT
KEY_BACK
KEY_HOMEPAGE
KEY_VOLUMEDOWN
KEY_VOICECOMMAND
KEY_VOLUMEUP
KEY_PREVIOUSSONG
KEY_PLAYPAUSE
KEY_NEXTSONG
KEY_0 ... 9
KEY_BACKSPACE
KEY_COMPOSE
```

## Installation

```text
pip install OPi.GPIO dbus-next
pip install evdev python-vlc pyalsaaudio
pip install uvicorn fastapi sse-starlette
```

In `/etc/boot/orangepiEnv.txt` add:

```text
overlays=spi-spidev1
```
