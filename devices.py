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
        self.set("brightness", "0") 
    def on(self):
        self.set('trigger','none')
        self.set("brightness", "1")
    def default(self):
        self.off()
        self.set(self.option, self.value)
    async def blink(self, blink, on_time=.2, off_time=.5):
        self.is_blinking = blink
        self.set('trigger','none')
        while self.is_blinking:
            self.set("brightness", "1")
            await asyncio.sleep(on_time)
            self.set("brightness", "0")
            await asyncio.sleep(off_time)
        self.default()

# Current configuration:
#             audio_hub | function | krn | |header | krn | function | audio_hub
#  ---------------------|----------|-----|----|----|-----|----------|----------
#                       | 3.3V Out |     |  1 |  2 |     | 5V InOut | power in+
#                       |  I2C SDA | 122 |  3 |  4 |     | 5V InOut |
#                       |  I2C SCL | 121 |  5 |  6 |     | GND      |
#                       |     PWM1 | 118 |  7 |  8 | 354 | TX UART  |
#                       |      GND |     |  9 | 10 | 355 | RX UART  |
#                       |          | 120 | 11 | 12 | 114 |          |
#                       |          | 119 | 13 | 14 |     | GND      |
#                       |          | 362 | 15 | 16 | 111 |          |
#                       | 3.3V Out |     | 17 | 18 | 112 |          |
#   led 1 fiber optic 1 | SPI MOSI | 229 | 19 | 20 |     | GND      |
#   button 5.1 / stereo | SPI MISO | 230 | 21 | 22 | 117 |          |
# button input selector | SPI  CLK | 228 | 23 | 24 | 227 | SPI CS   | led 3 digital coaxial
#             power in- |      GND |     | 25 | 26 | 360 | PWM0     | led 2 fiber optic 2
# 
# Proposition:
#             audio_hub | function | krn || header | krn | function | audio_hub
#  ---------------------|----------|-----|----|----|-----|----------|----------
#                       | 3.3V Out |     |  1 |  2 |     | 5V InOut | power in+
#                       |  I2C SDA | 122 |  3 |  4 |     | 5V InOut |
#                       |  I2C SCL | 121 |  5 |  6 |     | GND      |
#                       |     PWM1 | 118 |  7 |  8 | 354 | TX UART  |
#                       |      GND |     |  9 | 10 | 355 | RX UART  |
#   led 1 fiber optic 1 |          | 120 | 11 | 12 | 114 |          | led 3 digital coaxial
#   button 5.1 / stereo |          | 119 | 13 | 14 |     | GND      | power in-
# button input selector |          | 362 | 15 | 16 | 111 |          | led 2 fiber optic 2
#            volume VCC | 3.3V Out |     | 17 | 18 | 112 |          |
#                       | SPI MOSI | 229 | 19 | 20 |     | GND      | 
#            volume  DI | SPI MISO | 230 | 21 | 22 | 117 |          | 
#            volume CLK | SPI  CLK | 228 | 23 | 24 | 227 | SPI CS   | volume CS
#            volume GND |      GND |     | 25 | 26 | 360 | PWM0     | 
# 
pin_map = { # key: header pin number, value: gpio kernel number
              8:354, 10:355, 12:114,         16:111, 18:112,         22:117, 24:227, 26:360, 
3:122, 5:121, 7:118,         11:120, 13:119, 15:362,         19:229, 21:230, 23:228        }

# constants
input_btn = 15 # 23
surround_btn = 13 # 21
leds = [11, 16, 12] # [19, 26, 24] # led1, led2, led3

dac_inputs = ['bt', 'pc', 'tv', 'off']

# GLOBAL state variable in case 2 select_input tasks are launch
aux_to_select = -1

def init():
    gpio.setmode(pin_map)
    for led in leds:
        gpio.setup(led, gpio.IN)#, pull_up_down=gpio.PUD_OFF) # Not working yeat?
    gpio.setup(input_btn,    gpio.OUT, initial=gpio.HIGH)
    gpio.setup(surround_btn, gpio.OUT, initial=gpio.HIGH)
    return get_aux()

def end():
    gpio.cleanup()

def get_aux():
    i = 0
    if   gpio.input(leds[0])==0: i = 1 # Optical 1
    elif gpio.input(leds[1])==0: i = 2 # Optical 2
    elif gpio.input(leds[2])==0: i = 3 # Coaxial
    with open('/dev/shm/aux','w') as f: f.write(str(i))
    return i                           # Stereo IN

async def surround_toggle():
    gpio.output(surround_btn, gpio.LOW)
    await asyncio.sleep(.8)           # 0.08 time of push
    gpio.output(surround_btn, gpio.HIGH)

async def next_aux():
    gpio.output(input_btn, gpio.LOW)
    await asyncio.sleep(.8)           # 0.08 time of push
    gpio.output(input_btn, gpio.HIGH)
    await asyncio.sleep(.9)           # 0.8 time of responce

async def set_aux(aux):
    aux = dac_inputs.index(aux)
    global aux_to_select
    if aux_to_select != -1:
        aux_to_select = aux
        return
    else: 
        aux_to_select = aux

    current_aux = get_aux()
    while current_aux != aux_to_select:
        await next_aux()
        current_aux = get_aux()
        print("now: ", current_aux, "search: ", aux_to_select)

    print("OK")
    aux_to_select = -1

