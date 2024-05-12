# Info
 1. Database of streams: http://fmstream.org/index.php
 
# Implemented:
 1. Bluetooth sink (hidden non discoverable)
 1. Alsamixer device
 1. Vlc player with radio station media list
 1. Main async event loop. Listen only infrared keys (for now)
 1. Aux device 5.1 dac and audio switch controlled through gpio with async tasks
 1. In responce to keys (hold or press) send signals to mixer, vlc, aux
 1. Reboot every 3 days at 3 a.m. (temprorary just restart VLC)
 1. Web interface and API

# TODO:
 1. JavaScript to generate "input" and "label" from: "/get_radios" before node "end_radio_list"
 1. Problem - usb device dissapeared one time (not yeat reproduced)
 1. Investigate why after 3-4 days when device is not rebooted some VLC radios stop to work.
    - [x] reboot every night helps but I don't like it
    - [x] restart (hard) VLC doesn't help
    - [x] restart Python aplication doesn't help
    - [x] maybe it's a problem of DNS? trying: sudo resolvectl flush-caches
    - [ ] narrowed down to TLS certificates, renewing them doesn't help
    - ugly solution is to reboot device every 3 days

# Ideas to explore:
 1. Web edit radios
 1. Web edit bt devices
 1. Questionable ideas to refactor:
    1. Move IR to class
    1. Move gpio to class

# New buttons in remote:

```
custom ir:      lg ir:     new radio rc:
KEY_BLUETOOTH   (tv/radio) KEY_VOICECOMMAND  <- DAC out 0 (long press - pair)
KEY_PC          (tv/pc)    KEY_PAGEUP        <- DAC out 1
KEY_TV          (input)    KEY_PAGEDOWN      <- DAC out 2
KEY_POWER                                    <- DAC out 3 (long press - reboot)
KEY_MUTE (blue)                              <- Surround / Stereo -> better: KEY_BASSBOOST
KEY_VOLUMEDOWN                               <- volume +
KEY_VOLUMEUP                                 <- volume -
KEY_0 ... 9                                  <- Radios
```

All keys on new remote left -> right, top -> down
```
KEY_POWER
KEY_MUTE
KEY_PAGEUP
(mouse button is not en event)
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


# Installation
```
pip install OPi.GPIO dbus-next
pip install evdev python-vlc pyalsaaudio
pip install uvicorn fastapi sse-starlette
```

# Dmesg for new remote controller
It's add: Mouse, keyboard and soundcard
```
usb 6-1: new full-speed USB device number 2 using ohci-platform
usb 6-1: New USB device found, idVendor=4842, idProduct=0001, bcdDevice= 1.00
usb 6-1: New USB device strings: Mfr=1, Product=2, SerialNumber=3
usb 6-1: Product: USB Composite Device
usb 6-1: Manufacturer: HAOBO Technology
usb 6-1: SerialNumber: 1120030400060622
input: HAOBO Technology USB Composite Device Keyboard as /devices/platform/soc/5311400.usb/usb6/6-1/6-1:1.2/0003:4842:0001.0001/input/input2
hid-generic 0003:4842:0001.0001: input,hidraw0: USB HID v2.01 Keyboard [HAOBO Technology USB Composite Device] on usb-5311400.usb-1/input2
input: HAOBO Technology USB Composite Device as /devices/platform/soc/5311400.usb/usb6/6-1/6-1:1.3/0003:4842:0001.0002/input/input3
hid-generic 0003:4842:0001.0002: input,hidraw1: USB HID v2.01 Mouse [HAOBO Technology USB Composite Device] on usb-5311400.usb-1/input3
hid-generic 0003:4842:0001.0003: hiddev96,hidraw2: USB HID v2.01 Device [HAOBO Technology USB Composite Device] on usb-5311400.usb-1/input4
usbcore: registered new interface driver snd-usb-audio
```

# Error when USB device disapeear
```
Traceback (most recent call last):
  File "/home/kosiu/audio_hub/./audio_hub.py", line 165, in <module>
    main()
  File "/home/kosiu/audio_hub/./audio_hub.py", line 11, in main
    asyncio.run(s.loop())
  File "/usr/lib/python3.10/asyncio/runners.py", line 44, in run
    return loop.run_until_complete(main)
  File "/usr/lib/python3.10/asyncio/base_events.py", line 646, in run_until_complete
    return future.result()
  File "/home/kosiu/audio_hub/./audio_hub.py", line 30, in loop
    await asyncio.gather(*loops)
  File "/home/kosiu/audio_hub/./audio_hub.py", line 147, in ir_loop
    async for event in device.async_read_loop():
  File "/usr/local/lib/python3.10/dist-packages/evdev/eventio_async.py", line 93, in next_batch_ready
    future.set_result(next(self.current_batch))
  File "/usr/local/lib/python3.10/dist-packages/evdev/eventio.py", line 71, in read
    events = _input.device_read_many(self.fd)
OSError: [Errno 19] No such device
```

