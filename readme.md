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

Log for new remote controller
-----------------------------
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

