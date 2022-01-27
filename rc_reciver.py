#!/usr/bin/env python3

# Ideas to test:
#    move to vls package (faster?)
#    check coexistence of bloototh and vlc
#    dbus for bluetooth
#    gpio read leds
#    gpio set status
#    gpio led display
#    REST api
#    send ir events

import asyncio, evdev, subprocess, alsaaudio

SIGQUIT = 3
keys = evdev.ecodes
keys.VAL_UP   = 0
keys.VAL_DOWN = 1
keys.VAL_HOLD = 2

# Global state
state = 0 # off
#  1...10   radio chanels 1...9,0
# 11...13   aux1, aux2, aux3 => PC,TV,in
#      14   bluetooth

# Global player + mixer
player = None
mixer = alsaaudio.Mixer('Headphone')

def ir_key_pressed(key):
    print(f'Remote key pressed: {keys.KEY[key]}')
    if   key == keys.KEY_VOLUMEDOWN: volume_down()
    elif key == keys.KEY_VOLUMEUP:   volume_up()
    elif key == keys.KEY_UP: # Bluetooth
        play_stream(14)
    elif key==keys.KEY_1: # Radio 357
        play_stream(1,'https://stream.rcs.revma.com/ye5kghkgcm0uv')
    elif key==keys.KEY_2: # Radio Nowy Swiat
        play_stream(2,'https://stream.nowyswiat.online/aac')
    elif key==keys.KEY_3: # FIP
        play_stream(3,'http://icecast.radiofrance.fr/fip-midfi.aac')
    elif key==keys.KEY_4: # RMF Classic
        play_stream(4,'http://195.150.20.242:8000/rmf_classic')
    elif key==keys.KEY_5: # Antyradio
        play_stream(5,'http://ant-kat-01.cdn.eurozet.pl:8604')
    elif key==keys.KEY_6: # 
        play_stream(6)
    elif key==keys.KEY_7: # 
        play_stream(7)
    elif key==keys.KEY_8: # 
        play_stream(8)
    elif key==keys.KEY_9: # 
        play_stream(9)
    elif key==keys.KEY_0: # 
        play_stream(0)

def ir_key_hold(key):
    print(f'Remote key hold:    {keys.KEY[key]}')
    if   key == keys.KEY_VOLUMEDOWN: volume_down()
    elif key == keys.KEY_VOLUMEUP:   volume_up()
    elif key == keys.KEY_OK:         loop.stop()

def play_stream(new_state,stream=None):
    global player
    switch_state(new_state)
    if stream!=None:
        player = subprocess.Popen("cvlc -A alsa".split()+[stream])

def switch_state(new_state):
    global state, player
    if player!=None:
        player.send_signal(SIGQUIT)
        player = None
    state=new_state

def volume_down():
    vol = mixer.getvolume()[0]-2
    mixer.setvolume(0 if vol < 0 else vol)

def volume_up():
    vol = mixer.getvolume()[0]+2
    mixer.setvolume(100 if vol>100 else vol)

def find_ir_device():
    ir = None
    devices = [evdev.InputDevice(path) for path in evdev.list_devices()]
    for device in devices:
        if device.name == 'gpio_ir_recv': ir = device
        else: device.close()
    if ir == None: raise Exception("Can't find IR device")
    return ir

async def ir_loop(ir):
    async for event in ir.async_read_loop():
        if event.type == keys.EV_KEY:
            if event.value == keys.VAL_UP: ir_key_pressed(event.code)
            elif event.value == keys.VAL_HOLD: ir_key_hold(event.code)

if __name__ == '__main__':
    asyncio.ensure_future(ir_loop(find_ir_device()))
    loop = asyncio.get_event_loop()
    print('Start')
    loop.run_forever()
    switch_state(0)
    print('Exit')

