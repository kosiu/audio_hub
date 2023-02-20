# Implemented:
 1. Bluetooth sink (hidden non discoverable)
 1. Alsamixer device
 1. Vlc player with radio station media list
 1. Main async event loop. Listen only infrared keys (for now)
 1. Aux device 5.1 dac and audio switch controlled through gpio with async tasks
 1. In responce to keys (hold or press) send signals to mixer, vlc, aux
 1. Reboot ones per week at 3 a.m.
 1. Web interface and API

# TODO:
 1. Current approach with forcing VLC exit dosn't work.
 1. Implement restart of the full software stack maybe it will be better.
 1. Button restart device insteed VLC
 1. VLC shouldn't wait when signal recived

# Ideas to explore:
 1. Questionable ideas to refactor:
    1. Move IR to class
    1. Move gpio to class
 1. Web edit radios
 1. Web edit bt devices

# New buttons in remote:
```
KEY_BLUETOOTH   <- DAC out 0 (long press - pair)
KEY_PC          <- DAC out 1
KEY_TV          <- DAC out 2
KEY_POWER       <- DAC out 3 (long press - reboot)
KEY_BASSBOOST   <- Surround / Stereo
KEY_VOLUMEDOWN  <- volume +
KEY_VOLUMEUP    <- volume -
KEY_0 ... 9     <- Radios
```

# New states concepts:
Actions:
 - tv
 - pc
 - bt
 - off
 - 0 ... 9 radios

Actions:
 - all above +
 - stereo
 - reboot
 - bt pair

Not yeat clasified:
 - bt pairing
 - bt idle
 - bt connected
 - bt playing
 - radio x playing


Installation
------------

pip install OPi.GPIO dbus-next
pip install evdev python-vlc pyalsaaudio
pip install uvicorn fastapi sse-starlette

