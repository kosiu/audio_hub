# TODO:

 1. gpio in python

# Ideas to test:

 1. asyncio for gpio
 2. dbus for bluetooth (possible with async interface?)
 3. gpio led display
 4. REST api or different mqtt?
 5. send ir events (needed?)

# Auto-pair agent

    bluetoothctl discoverable on
    bt-agent --capability=NoInputNoOutput

# mplayera (vlc is better for that)
    mplayer -prefer-ipv4 -cache 512 -cache-min 60 https://stream.rcs.revma.com/ye5kghkgcm0uv

# GPIO notes
 
Standart GPIO for Raspberry PI examples. I wounder if there is async version of it?

```python
import time
import RPi.GPIO as GPIO

in_btn = 37
ch_btn = 35
a_led = 33
b_led = 40
c_led = 38

GPIO.setmode(GPIO.BOARD)
print(GPIO.getmode()) # should be 10

leds = [a_led, b_led, c_led]
for led in leds:
    GPIO.setup(led, GPIO.IN)

GPIO.setup(in_btn, GPIO.OUT)


GPIO.output(37,GPIO.LOW);
time.sleep(.07);             # 0.07 shortest possible
GPIO.output(37,GPIO.HIGH);
time.sleep(.6);              # 0.6 shortest possible
print([GPIO.input(led) for led in leds])

```
