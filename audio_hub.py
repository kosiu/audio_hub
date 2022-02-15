#!/usr/bin/env python3
import asyncio, evdev, vlc, alsaaudio, subprocess,time
time.sleep(60*2)
keys = evdev.ecodes # shortcut for key codes

# -----------------------------------------------------------------------------
# Global variables (objects):
mixer  = alsaaudio.Mixer('Speaker')
player = vlc.MediaListPlayer()
# -----------------------------------------------------------------------------

def radio_stations():
    radios = vlc.MediaList()
    radios.add_media('https://stream.rcs.revma.com/ye5kghkgcm0uv')
    radios.add_media('https://stream.nowyswiat.online/aac')
    radios.add_media('http://icecast.radiofrance.fr/fip-midfi.aac')
    radios.add_media('http://195.150.20.242:8000/rmf_classic')
    radios.add_media('http://ant-kat-01.cdn.eurozet.pl:8604')
    return radios

def ir_key_pressed(key):
    print(f'Remote key pressed: {keys.KEY[key]}')
    if   key == keys.KEY_VOLUMEDOWN: change_volume(-3)
    elif key == keys.KEY_VOLUMEUP:   change_volume(+3)
    elif key == keys.KEY_0: player.play_item_at_index(0) # Radio 357
    elif key == keys.KEY_1: player.play_item_at_index(1) # Radio Nowy Świat
    elif key == keys.KEY_2: player.play_item_at_index(2) # Radio FIP
    elif key == keys.KEY_3: player.play_item_at_index(3) # RMF Classic
    elif key == keys.KEY_4: player.play_item_at_index(4) # Antyradio
    elif key == keys.KEY_5: player.stop()
    elif key == keys.KEY_6: player.stop()
    elif key == keys.KEY_7: player.stop()
    elif key == keys.KEY_8: player.stop()
    elif key == keys.KEY_9: player.stop()

def ir_key_hold(key):
    print(f'Remote key hold:    {keys.KEY[key]}')
    if   key == keys.KEY_VOLUMEDOWN: change_volume(-5)
    elif key == keys.KEY_VOLUMEUP:   change_volume(+5)

def change_volume(step):
    old_vol = mixer.getvolume()[0]
    new_vol = max(min(100, old_vol + step), 0)
    if old_vol != new_vol: mixer.setvolume(new_vol)

# -----------------------------------------------------------------------------
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
            KEY_PRESSED  = 0
            KEY_RELEASED = 1
            KEY_HOLD     = 2
            if event.value == KEY_RELEASED: ir_key_pressed(event.code)
            elif event.value   == KEY_HOLD: ir_key_hold(event.code)

def main():
    baplay = subprocess.Popen(['bluealsa-aplay','00:00:00:00:00:00'])
    player.set_media_list(radio_stations())

    asyncio.ensure_future(ir_loop(find_ir_device()))
    asyncio.get_event_loop().run_forever()

if __name__ == '__main__':
    main()

