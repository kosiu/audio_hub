# Implemented:
 1. Bluetooth sink (hidden non discoverable)
 1. Alsamixer device
 1. Vlc player with radio station media list
 1. Main async event loop. Listen only infrared keys (for now)
 1. Aux device 5.1 dac and audio switch controlled through gpio with async tasks
 1. In responce to keys (hold or press) send signals to mixer, vlc, aux
 1. Reboot ones per week at 3 a.m.

# TODO:
 1. Integrate http server
    1. volume + new mixer (good start)
    1. with new state system

# Ideas to explore:
 1. Refactor ideas:
    1. long press + other buttons in class or module maybe also leds
    1. radios in json
1. display to show current status of radio or input

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
Input:
 - TV
 - PC
 - BT
 - OFF
 - 0 ... 9 radios

Actions:
 - all above
 - toggle surround
 - reboot
 - BT pair

Not yeat clasified:
 - bluetooth pairing
 - bluetooth not connected
 - bluetooth connected
 - bluetooth play
 - bluetooth pause


Installation
------------

pip install uvicorn fastapi sse-starlette

