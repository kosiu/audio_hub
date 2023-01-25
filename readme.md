# Implemented:
 1. Bluetooth sink (hidden non discoverable)
 1. Alsamixer device
 1. Vlc player with radio station media list
 1. Main async event loop. Listen only infrared keys (for now)
 1. Aux device 5.1 dac and audio switch controlled through gpio with async tasks
 1. In responce to keys (hold or press) send signals to mixer, vlc, aux
 1. Reboot ones per week at 3 a.m.

# TODO:
 1. API from outisde like rest + server send events which means: FastAPI + SSE
    https://devdojo.com/bobbyiliev/how-to-use-server-sent-events-sse-with-fastapi

# Ideas to explore:
 1. Refactor ideas:
    1. states + led together in module (re-think concept -- maybe more after API)
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
 - 0 ... 9 -- radio channels
 - A -- out 0
 - B -- out 1
 - C -- out 2
 - D -- out 3
 - E -- pairing
or
 - TV
 - PC
 - OFF
 - 0 ... 9 radios
 - bluetooth pairing
 - bluetooth not connected
 - bluetooth connected
 - bluetooth play
 - bluetooth pause

pip install uvicorn fastapi
pip install sse-starlette
