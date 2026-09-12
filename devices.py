import asyncio
import OPi.GPIO as gpio

class System_Led:
    def __init__(self, color, default_option, default_value):
        self.color = color
        self.option = default_option
        self.value = default_value
        self.off()
        self.set(default_option, default_value)
    def set(self, option,value):
        with open(f'/sys/devices/platform/leds/leds/{self.color}-led/{option}','w') as f:
            f.write(value)
    def off(self):
        self.set('trigger','none')
        self.set('brightness', '0') 
    def on(self):
        self.set('trigger','none')
        self.set('brightness', '1')
    def default(self):
        self.off()
        self.set(self.option, self.value)
    async def blink(self, blink, on_time=.2, off_time=.5):
        self.is_blinking = blink
        self.set('trigger','none')
        while self.is_blinking:
            self.set('brightness', '1')
            await asyncio.sleep(on_time)
            self.set('brightness', '0')
            await asyncio.sleep(off_time)
        self.default()

# GPIO pin configuration:
#             audio_hub | function | krn || header | krn | function | audio_hub
#  --------------------:|---------:|----:|---:|---:|----:|:---------|:---------
#                       | 3.3V Out |     |  1 |  2 |     | 5V InOut | 
#                       |  I2C SDA | 122 |  3 |  4 |     | 5V InOut | power in+
#                       |  I2C SCL | 121 |  5 |  6 |     | GND      | power in-
#                       |     PWM1 | 118 |  7 |  8 | 354 | TX UART  | amp STB
#                       |      GND |     |  9 | 10 | 355 | RX UART  | 
#                       |          | 120 | 11 | 12 | 114 |          |                      
#                       |          | 119 | 13 | 14 |     | GND      |          
#                       |          | 362 | 15 | 16 | 111 |          |                    
#                       | 3.3V Out |     | 17 | 18 | 112 |          |
#                       | SPI MOSI | 229 | 19 | 20 |     | GND      | 
#                       | SPI MISO | 230 | 21 | 22 | 117 |          | 
#                       | SPI  CLK | 228 | 23 | 24 | 227 | SPI CS   |          
#                       |      GND |     | 25 | 26 | 360 | PWM0     | 
# 
pin_map = { # key: header pin number, value: gpio kernel number
              8:354, 10:355, 12:114,         16:111, 18:112,         22:117, 24:227, 26:360, 
3:122, 5:121, 7:118,         11:120, 13:119, 15:362,         19:229, 21:230, 23:228        }

amp_standby = 8

def init():
    gpio.setmode(pin_map)
    gpio.setup(amp_standby, gpio.OUT, initial=gpio.LOW)

def set_amp_active(active):
    gpio.output(amp_standby, gpio.HIGH if active else gpio.LOW)

def end():
    try:
        set_amp_active(False)
    except Exception:
        pass
    gpio.cleanup()

