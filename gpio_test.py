import time
import RPi.GPIO as gpio
# Ment to be run: exec(open("gpio_test.py").read())

# constants
input_btn = 37
surround_btn = 35
leds = [33, 40, 38] # led1, led2, led3

# GLOBAL variable in case 2 select_input tasks are launch
input_to_select = -1

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
 
def gpio_exit():
    gpio.cleanup()

def which_input():
    if   gpio.input(leds[0])==0: return 1 # Optical 1
    elif gpio.input(leds[1])==0: return 2 # Optical 2
    elif gpio.input(leds[2])==0: return 3 # Coaxial
    return 0                              # Stereo IN


def next_input():
    gpio.output(input_btn, gpio.LOW)
    time.sleep(.8)           # 0.08 time of push
    gpio.output(input_btn, gpio.HIGH)
    time.sleep(.9)            # 0.8 time of responce
    return which_input()

def select_input(input):
    global input_to_select
    if input_to_select != -1:
        input_to_select = input
        return False
    else: 
        input_to_select = input

    current_input = which_input()
    while current_input != input_to_select:
        current_input = next_input()
        print("now: ", current_input, "search: ", input_to_select)

    print("OK")
    input_to_select = -1
    return True

