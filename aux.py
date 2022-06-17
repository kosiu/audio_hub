import time # only for init() function
import asyncio
import RPi.GPIO as gpio
# Ment to be run: exec(open("gpio_test.py").read())
# asyncio.run(select_input(0))
# await select_input(0)
# asyncio.create_task(select_input(0))

# constants
input_btn = 37
surround_btn = 35
leds = [33, 40, 38] # led1, led2, led3

# GLOBAL variable in case 2 select_input tasks are launch
aux_to_select = -1

def init():
    gpio.setmode(gpio.BOARD)
    for led in leds:
        gpio.setup(led, gpio.IN, pull_up_down=gpio.PUD_DOWN)
    gpio.setup(input_btn,    gpio.OUT)
    gpio.setup(surround_btn, gpio.OUT)
    # somehow surround button has to be pushed
    # otherwise the input switch dosn't work
    gpio.output(surround_btn, gpio.LOW)
    time.sleep(1)
    gpio.output(surround_btn, gpio.HIGH)
 
def end():
    gpio.cleanup()

def get_aux():
    if   gpio.input(leds[0])==0: return 1 # Optical 1
    elif gpio.input(leds[1])==0: return 2 # Optical 2
    elif gpio.input(leds[2])==0: return 3 # Coaxial
    return 0                              # Stereo IN


async def next_aux():
    gpio.output(input_btn, gpio.LOW)
    await asyncio.sleep(.8)           # 0.08 time of push
    gpio.output(input_btn, gpio.HIGH)
    await asyncio.sleep(.9)           # 0.8 time of responce

async def set_aux(aux):
    global aux_to_select
    if aux_to_select != -1:
        aux_to_select = aux
    else: 
        aux_to_select = aux

    current_aux = get_aux()
    while current_aux != aux_to_select:
        await next_aux()
        current_aux = get_aux()
        print("now: ", current_aux, "search: ", aux_to_select)

    print("OK")
    aux_to_select = -1

# initialise during import
init()

