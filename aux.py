import asyncio
import OPi.GPIO as gpio

pin_map = { # key: header pin number, value: gpio kernel number
#  1: 3.3V
#  2: 5V     connected
   3: 122,
#  4: 5V
   5: 121,
#  6: GND
   7: 118,
   8: 354,
#  9: GND
  10: 355,
  11: 120,
  12: 114,
  13: 119,
# 14: GND
  15: 362,
  16: 111,
# 17: 3.3V
  18: 112,
  19: 229, # led 1 fiber optic 1
# 20: GND
  21: 230, # button 5.1 / stereo
  22: 117,
  23: 228, # button input selector
  24: 227, # led 3 digital coaxial
# 25: GND    conected
  26: 360} # led 2 fiber optic 2

# constants
input_btn = 23
surround_btn = 21
leds = [19, 26, 24] # led1, led2, led3

# GLOBAL state variable in case 2 select_input tasks are launch
aux_to_select = -1

def init():
    gpio.setmode(pin_map)
    for led in leds:
        gpio.setup(led, gpio.IN)#, pull_up_down=gpio.PUD_OFF) # Not working yeat?
    gpio.setup(input_btn,    gpio.OUT, initial=gpio.HIGH)
    gpio.setup(surround_btn, gpio.OUT, initial=gpio.HIGH)
 
def end():
    gpio.cleanup()

def get_aux():
    if   gpio.input(leds[0])==0: return 1 # Optical 1
    elif gpio.input(leds[1])==0: return 2 # Optical 2
    elif gpio.input(leds[2])==0: return 3 # Coaxial
    return 0                              # Stereo IN

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
    with open('/dev/shm/aux','w') as f: f.write(str(current_aux))
    aux_to_select = -1

