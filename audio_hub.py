#!/usr/bin/env -S python3 -u
import asyncio, subprocess,datetime,time # official python packages
import evdev, vlc, alsaaudio         # pip installed
import aux                           # local file require: OPi.GPIO
import dbus_bluez                    # local file require: dbus_next
keys = evdev.ecodes                  # shortcut for key codes

volumes = [0, 18, 19, 20, 21, 22, 24, 25, 27, 28, 30, 32, 33, 35, 38, 40, 42, 45, 47, 50, 53, 56, 60, 63, 67, 71, 75, 79, 84, 89, 94, 100]
# import bisect
# value = volumes[index]
# index = bisect.bisect[value]-1

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
    print(f'Remote key pressed:   {keys.KEY[key]}')
    if   key == keys.KEY_VOLUMEDOWN: change_volume(-3)
    elif key == keys.KEY_VOLUMEUP:   change_volume(+3)
    elif key == keys.KEY_0: set_radio(0)  # Radio 357
    elif key == keys.KEY_1: set_radio(1)  # Radio Nowy Świat
    elif key == keys.KEY_2: set_radio(2)  # Radio FIP
    elif key == keys.KEY_3: set_radio(3)  # RMF Classic
    elif key == keys.KEY_4: set_radio(4)  # Antyradio
    elif key == keys.KEY_5: pass # n.a. on small remote
    elif key == keys.KEY_6: set_aux(0)    # PI stereo input 0 (bluetooth)
    elif key == keys.KEY_7: set_aux(1)    # PC fiber optic input 1
    elif key == keys.KEY_8: set_aux(2)    # TV fiber optic input 2
    elif key == keys.KEY_9: set_aux(3)    # OFF coaxial digital input
    elif key == keys.KEY_RED:    pass
    elif key == keys.KEY_GREEN:  pass
    elif key == keys.KEY_YELLOW: pass
    elif key == keys.KEY_BLUE:   asyncio.create_task(aux.surround_toggle())
    elif key == keys.KEY_UP:     pass
    elif key == keys.KEY_DOWN:   pass

def ir_key_hold(key):
    print(f'Remote key hold:      {keys.KEY[key]}')
    if   key == keys.KEY_VOLUMEDOWN: change_volume(-5)
    elif key == keys.KEY_VOLUMEUP:   change_volume(+5)

def ir_key_long(key):
    print(f'Remote key long hold: {keys.KEY[key]}')
    if   key == keys.KEY_6: asyncio.create_task(dbus_bluez.enable_pairing())
    elif key == keys.KEY_9: subprocess.Popen('reboot')

def change_volume(step):
    old_vol = mixer.getvolume()[0]
    new_vol = max(min(100, old_vol + step), 0)
    if old_vol != new_vol: mixer.setvolume(new_vol)

def set_radio(channel):
    asyncio.create_task(aux.set_aux(0))
    player.play_item_at_index(channel)
    with open('/dev/shm/state','w') as f: f.write(str(channel))

def set_aux(ext_in):
    player.stop()
    asyncio.create_task(aux.set_aux(ext_in))
    with open('/dev/shm/state','w') as f: f.write(chr(65+ext_in))

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

def test_long_press(key):
    '''Due to buggy RC transmiter (no hold option) hack to detect long press buttons'''
    now = time.time()
    if not hasattr(test_long_press, 'done'):
        test_long_press.done        = False
        test_long_press.last_key    = keys.KEY_POWER
        test_long_press.first_press = now
        test_long_press.last_press  = now
    if key == test_long_press.last_key:
        if (now - test_long_press.last_press) < 0.5:
            test_long_press.last_press = now
            if (now - test_long_press.first_press) > 5:
                if test_long_press.done == False:
                    test_long_press.done = True
                    ir_key_long(key)
            return
    test_long_press.done        = False
    test_long_press.first_press = now
    test_long_press.last_press  = now
    test_long_press.last_key    = key

async def ir_loop(ir):
    asyncio.create_task(dbus_bluez.init())
    asyncio.create_task(shedule_reboot())
    async for event in ir.async_read_loop():
        if event.type == keys.EV_KEY:
            KEY_PRESSED  = 0
            KEY_RELEASED = 1
            KEY_HOLD     = 2
            test_long_press(event.code)
            if event.value == KEY_RELEASED: ir_key_pressed(event.code)
            elif event.value ==   KEY_HOLD: ir_key_hold(event.code)

async def shedule_reboot():
    now = datetime.datetime.now()
    at3 = (now + datetime.timedelta(days=7)).replace(hour=3,minute=0,second=0)
    await asyncio.sleep((at3-now).total_seconds())
    pospond = 10
    while player.is_playing() and (pospond > 0):
        pospond -= 1
        await asyncio.sleep(3600)
    subprocess.Popen('reboot')

def main():
    with open('/dev/shm/state','w') as f: f.write(chr(65+aux.init()))
    with open('/sys/devices/platform/leds/leds/green-led/brightness','w') as f: f.write('0')
    with open('/sys/devices/platform/leds/leds/green-led/trigger','w')    as f: f.write('rc-feedback')
    with open('/sys/devices/platform/leds/leds/red-led/brightness','w')   as f: f.write('0')
    with open('/sys/devices/platform/leds/leds/red-led/trigger','w')      as f: f.write('mmc0')
    player.set_media_list(radio_stations())
    asyncio.run(ir_loop(find_ir_device()))

if __name__ == '__main__':
    main()

