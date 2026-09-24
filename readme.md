# Audio Hub 2.0

## Current Hardware

1. The TV is the only TOSLINK source.
2. The OrangePi has one application GPIO connection: amplifier STB on header pin
   8. `LOW` or high impedance means standby; `HIGH` enables the amplifier.
3. PC audio uses Moonlight and Sunshine. The application owns a small Xvfb
   display for Moonlight and starts the stream while the `pc` input is selected.
4. CamillaDSP owns volume, channel routing, balancing, filtering, and the final
   six-channel output.
5. The center and subwoofer speakers are physically swapped, so the CamillaDSP
   mapping must account for the wiring.

Current pipeline:

```text
TV TOSLINK -> toslink_play -> Surround loopback (6ch) --+
                                                       +-> full_8ch -> CamillaDSP -> USB ALSA (6ch)
VLC / bluealsa-aplay -> Stereo loopback (2ch) ---------+
Moonlight PC audio -> Surround loopback (6ch) ---------+
```

The first six channels of `full_8ch` are TV surround. The last two are local
stereo.

## Build

```bash
g++ -O2 -Wall -std=c++23 -o toslink_play toslink_play.cpp \
  -lasound -lavformat -lavcodec -lavutil -lswresample
```

## Installation

```bash
apt install libavformat-dev libavcodec-dev libavutil-dev libswresample-dev
pip install OPi.GPIO dbus-next evdev python-vlc uvicorn fastapi sse-starlette
python3 -m pip install --user --upgrade pip setuptools wheel
python3 -m pip install --user git+https://github.com/HEnquist/pycamilladsp.git
```

In `/etc/boot/orangepiEnv.txt` add:

```text
overlays=spi-spidev1
```

### CamillaDSP and GUI

1. Install the plain ALSA AArch64 CamillaDSP build.
2. Install the bundled AArch64 CamillaGUI backend.
3. The tracked systemd units expect both under `/home/kosiu/opt/`.
4. The GUI is served at `http://127.0.0.1:5005`.

## TOSLINK Diagnostics

`toslink_play` reports `off`, `none`, `pcm`, or `ac3` on standard output. Send a
newline to its standard input to query the current state. While TV is selected,
the app keeps the amplifier in standby for `off` and `none`, and enables it for
`pcm` and `ac3`.

To inspect raw capture samples:

```bash
arecord -D hw:ICUSBAUDIO7D -f S16_LE -c 2 -r 48000 -d 1 -t raw -q - | od
```

Internet radio stream directory: http://fmstream.org/index.php

## PC Stream

Install `Xvfb` and Moonlight Embedded at `/usr/bin/Xvfb` and
`/usr/local/bin/moonlight`. Audio Hub starts display `:99` when it starts and
stops it when it exits; no `sudo` is required. Selecting `pc` runs the `Static
Icon` Sunshine application with 5.1 audio on the `Surround` ALSA device. Logs
are written to `/home/kosiu/xvfb.log` and `/home/kosiu/moonlight.log`.

See [project.md](project.md) for the current software and hardware contracts.

~~~
grep apt ~/.zsh_history 
ir-keytable
v4l-utils
vlc
vlc-bin
vlc-data
vlc-bin
vlc-plugin-base
build-essential
python-dev
python3-dev
libasound2-dev
bluez-alsa-utils
cloc
tree
ffmpeg
roc-toolkit-tools\
libavformat-dev
libavcodec-dev
libavutil-dev
libswresample-dev
libevdev
libevdev2
libevdev-dev
libudev-dev
libudev1
libcurl4
curl
libcurl4
libcurl4-gnutls-dev
libavahi-client-dev
libsdl2-dev
libfmt-dev
libgpiod-dev
~~~