# Implemented:
 1. Bluetooth sink (hidden non discoverable)
 1. Alsamixer device
 1. Vlc player with radio station media list
 1. Main async event loop. Listen only infrared keys (for now)
 1. Aux device 5.1 dac and audio switch controlled through gpio with async tasks
 1. In responce to keys (hold or press) send signals to mixer, vlc, aux
 1. Reboot ones per week at 3 a.m.

# TODO:
 1. blink when discoverable (new states like: conected, discoverable, playing)

# Ideas to explore:
 1. gpio led or oled display to show current status of radio or input
 1. API from outisde: rest+(SSE), mqtt, dbus, ir-events?
    https://devdojo.com/bobbyiliev/how-to-use-server-sent-events-sse-with-fastapi
 1. Refactor ideas:
     2. states + led together in module (re-think concept -- maybe more after API)
     2. long press + other buttons in clas or module
     2. general class with configuration
