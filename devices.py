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
#                       | 3.3V Out |     |  1 |  2 |     | 5V InOut | power in+
#                       |  I2C SDA | 122 |  3 |  4 |     | 5V InOut |
#                       |  I2C SCL | 121 |  5 |  6 |     | GND      |
#                       |     PWM1 | 118 |  7 |  8 | 354 | TX UART  | amp STB
#                       |      GND |     |  9 | 10 | 355 | RX UART  |
#   led 1 fiber optic 1 |          | 120 | 11 | 12 | 114 |          | led 3 digital coaxial
#                unused |          | 119 | 13 | 14 |     | GND      | power in-
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
amp_standby = 8
input_btn = 18 # 16 will be closer to rest of the signals
leds = [13, 11, 15] # led1, led2, led3 (pc,tv,coax)

dac_inputs = ['bt', 'pc', 'tv', 'off']
off_aux = dac_inputs.index('off')
AUX_STATE_PATH = '/dev/shm/aux'

def _set_amp_active(active):
    gpio.output(amp_standby, gpio.HIGH if active else gpio.LOW)

def _read_cached_aux(default=0):
    try:
        with open(AUX_STATE_PATH) as f:
            aux = int(f.read().strip())
        if 0 <= aux < len(dac_inputs):
            return aux
    except (FileNotFoundError, ValueError):
        pass
    return default

cached_aux = _read_cached_aux()

def _write_aux_state(aux):
    global cached_aux
    cached_aux = aux
    with open(AUX_STATE_PATH,'w') as f:
        f.write(str(aux))
    return aux

# GLOBAL state variable in case 2 select_input tasks are launch
aux_to_select = -1

def init():
    gpio.setmode(pin_map)
    gpio.setup(amp_standby, gpio.OUT, initial=gpio.LOW)
    # DAC GPIO connector is disconnected during the 2.0 migration.
    # Uncomment the lines below when the external selector wiring is back.
    # gpio.setup(leds, gpio.IN)#, pull_up_down=gpio.PUD_OFF) # Not working yeat?
    # gpio.setup(input_btn, gpio.OUT, initial=gpio.HIGH)
    return get_aux()

def end():
    try:
        _set_amp_active(False)
    except Exception:
        pass
    gpio.cleanup()

def get_aux():
    # DAC LED feedback is disconnected, so keep the last requested source.
    # Uncomment the block below when the DAC indicator lines are wired again.
    # i = 0
    # if   gpio.input(leds[0])==0: i = 1 # Optical 1
    # elif gpio.input(leds[1])==0: i = 2 # Optical 2
    # elif gpio.input(leds[2])==0: i = 3 # Coaxial
    # return _write_aux_state(i)
    return _write_aux_state(cached_aux)

async def next_aux():
    # DAC input button line is disconnected during migration.
    # Uncomment the block below when the external selector wiring is back.
    # gpio.output(input_btn, gpio.LOW)
    # await asyncio.sleep(.2)           # 0.08 time of push
    # gpio.output(input_btn, gpio.HIGH)
    # await asyncio.sleep(.8)           # 0.8 time of responce
    await asyncio.sleep(0)

async def set_aux(aux):
    aux = dac_inputs.index(aux)
    global aux_to_select
    if aux_to_select != -1:
        aux_to_select = aux
        return
    else: 
        aux_to_select = aux

    try:
        # DAC selector connector is unplugged during migration.
        # Keep the requested source in software and drive amp standby directly.
        _write_aux_state(aux_to_select)
        _set_amp_active(aux_to_select != off_aux)
        print('DAC selector GPIO disconnected, cached request only')
        return

        # Uncomment this old selector loop after restoring the DAC GPIO wiring.
        # current_aux = get_aux()
        # steps_left = len(dac_inputs)
        # while current_aux != aux_to_select and steps_left > 0:
        #     await next_aux()
        #     current_aux = get_aux()
        #     steps_left -= 1
        #     print("now: ", current_aux, "search: ", aux_to_select)
        #
        # if current_aux == aux_to_select:
        #     print("OK")
        # else:
        #     print(f'Unable to confirm DAC input {dac_inputs[aux_to_select]} after {len(dac_inputs)} steps')
    finally:
        aux_to_select = -1

