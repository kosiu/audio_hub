#!/usr/bin/env -S python3 -u 
import asyncio, subprocess       # official python packages
import evdev, vlc, alsaaudio     # pip installed
import aux                       # local scripts that also require: OPi.GPIO
keys = evdev.ecodes # shortcut for key codes

# -----------------------------------------------------------------------------
# Global variables (objects):
mixer  = alsaaudio.Mixer('Master')
player = vlc.MediaListPlayer(vlc.Instance('-A alsa'))
# -----------------------------------------------------------------------------

def radio_stations():
    radios = vlc.MediaList(player.get_instance())
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
    elif key == keys.KEY_0: set_radio(0)  # Radio 357
    elif key == keys.KEY_1: set_radio(1)  # Radio Nowy Świat
    elif key == keys.KEY_2: set_radio(2)  # Radio FIP
    elif key == keys.KEY_3: set_radio(3)  # RMF Classic
    elif key == keys.KEY_4: set_radio(4)  # Antyradio
    elif key == keys.KEY_5: player.stop()
    elif key == keys.KEY_6: player.stop()
    elif key == keys.KEY_7: set_aux(1)    # PC Fiber optic input 1
    elif key == keys.KEY_8: asyncio.create_task(aux.surround_toggle()) 
    elif key == keys.KEY_9: player.stop()

def ir_key_hold(key):
    print(f'Remote key hold:    {keys.KEY[key]}')
    if   key == keys.KEY_VOLUMEDOWN: change_volume(-5)
    elif key == keys.KEY_VOLUMEUP:   change_volume(+5)

def change_volume(step):
    old_vol = mixer.getvolume()[0]
    new_vol = max(min(100, old_vol + step), 0)
    if old_vol != new_vol: mixer.setvolume(new_vol)

def set_radio(channel):
    asyncio.create_task(aux.set_aux(0))
    player.play_item_at_index(channel)

def set_aux(ext_in):
    player.stop()
    asyncio.create_task(aux.set_aux(ext_in))

# -----------------------------------------------------------------------------
def find_ir_device():
    print("Searching for IR device")
    ir = None
    devices = [evdev.InputDevice(path) for path in evdev.list_devices()]
    for device in devices:
        if device.name == 'sunxi-ir': ir = device
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
    aux.init()
    baplay = subprocess.Popen(['bluealsa-aplay','00:00:00:00:00:00'])
    player.set_media_list(radio_stations())
    asyncio.run(ir_loop(find_ir_device()))

if __name__ == '__main__':
    main()

