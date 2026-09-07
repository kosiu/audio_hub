# Audio Hub 2.0 idea and migration

## Hardware

Diagram:

![Audio Hub 2.0 hardware view](doc/hardware.svg)

GPIO migration status:

1. STB on header pin 8 is now driven by the app. `LOW` or high impedance keeps
   the amplifier in standby and `HIGH` enables it.
2. Optical switch set on auto (not perfect) - work simmilar to pevious switch
   by cycling Auto, Ch1, Ch2... pushing button
3. Important is that even in Alsamixer volume is set to 0 it is still laud
4. I don't have active filters on subwoofer, other speakers has it, but minimal one
5. I messup connection of speakers (swaped central with subwoofer)
6. During migration DAC GPIO selector and LED feedback still stay commented out in
   `devices.py`. Uncomment only those lines when the external connector is back.

## Software migration

1. Concept of the migration is do smallest changes possible onece at the time
2. Minimal config for now is to have radio working (with working volume controll)
3. Everytime updating system files new file is added to `system_files`
4. Everytime new package is installed information is added on last chprer here 
   `installation`
5. `CamillaDSP` will be central part of the system for audio transformation and
   should take all heavy lifting. ALSA should stay minimal and feed audio into
   CamillaDSP, while filters, balancing, remapping, and 6 channel output live
   in CamillaDSP.
6. Version 1.0 is described here: [AudioHub v1.0](doc/v1_documentation.md)
7. Current target pipeline: `SPDIF/TOSLINK in -> ffmpeg decode -> ALSA loopback 6ch + VLC/bluealsa-aplay -> ALSA loopback 2ch -> full_8ch -> CamillaDSP -> USB ALSA 6ch`
8. Channel plan for `full_8ch`: channels `0..5` are reserved for decoded digital
   surround and channels `6..7` are reserved for local stereo sources.

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
python3 -m pip install --user --upgrade pip setuptools wheel
pip install evdev python-vlc
python3 -m pip install --user git+https://github.com/HEnquist/pycamilladsp.git
pip install uvicorn fastapi sse-starlette
```

In `/etc/boot/orangepiEnv.txt` add:

```text
overlays=spi-spidev1
```

### Camilla DSP & GUI

1. Download from github (current v4.1.3): https://github.com/HEnquist/camilladsp/releases
2. For this OrangePi use the plain ALSA build: `camilladsp-linux-aarch64.tar.gz`
3. Unpacked to: `/home/kosiu/camilladsp/`

1. Download from github (current v4.1.0): https://github.com/HEnquist/camillagui-backend/releases
2. For this OrangePi use the bundled Linux backend: `bundle_linux_aarch64.tar.gz`
3. Unpacked to: `/home/kosiu/camilladsp/`
4. GUI served at: `http://127.0.0.1:5005`

## Ideas for TV optical decoding

Adding optical snoop channel wich copy same data usful if 2 programs should read
~~~
# /etc/asound.conf addition
pcm.opt_dsnoop {
    type dsnoop
    ipc_key 5566
    slave {
        pcm "hw:CARD=ICUSBAUDIO7D,DEV=0"
        channels 2
        rate 48000
        format S16_LE
    }
}
~~~

Passing 5.1 channels:
~~~
arecord -D plughw:CARD=ICUSBAUDIO7D,DEV=0 -f S16_LE -r 48000 -c 2 -t raw \
  --buffer-time=5000 --period-time=1000 | \
ffmpeg -hide_banner -loglevel warning -probesize 32 -analyzeduration 0 \
  -fflags nobuffer -flags low_delay -f spdif -i - \
  -af "aresample=async=1:out_channel_layout=5.1,pan=5.1|c0=c0|c1=c1|c2=c4|c3=c5|c4=c2|c5=c3" \
  -c:a pcm_s16le -f alsa Surround
~~~

Passing Stereo:
~~~
ffmpeg -hide_banner -loglevel warning -fflags nobuffer -flags low_delay \
  -f alsa -thread_queue_size 1024 -i plughw:CARD=ICUSBAUDIO7D,DEV=0 \
  -c:a pcm_s16le -f alsa Stereo
~~~

Display row channels:
~~~
arecord -D hw:ICUSBAUDIO7D -f S16_LE -c 2 -r 48000 -d 1 -t raw -q - | od
~~~

Detecting:
~~~
arecord -q -D plughw:CARD=ICUSBAUDIO7D,DEV=0 -f S16_LE -r 48000 -c 2 -t raw \
    --buffer-time=50000 --period-time=10000 --samples=9600 2>/dev/null | \
  ffprobe -v error -analyzeduration 0 -probesize 32 -f spdif -i pipe:0 \
    -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 2>/dev/null
~~~

