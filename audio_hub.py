#!/usr/bin/env -S python3 -u
import asyncio, subprocess, datetime, time, json, signal, os # official python packages
import evdev, vlc, alsaaudio         # pip installed
import devices                       # local file require: OPi.GPIO
import dbus_bluez                    # local file require: dbus_next
import http_server

def main():
    global s
    s = State()
    asyncio.run(ir_loop())

class State:
    def __init__(self):
        signal.signal(signal.SIGINT, shutdown_app)
        self.__init_radios()
        self.mixer     = alsaaudio.Mixer('Master')
        self.update_ui = asyncio.Event()
        self.red_led   = devices.System_Led('red',  'trigger','mmc0')
        self.green_led = devices.System_Led('green','trigger','rc-feedback')
        devices.init()
        http_server.run_thread(self)

    def async_init(self):
        asyncio.create_task(dbus_bluez.init(self.red_led))
        asyncio.create_task(self.__shedule_player_restart())
        self.set_action('off')

    def set_action(self, action):
        print(f'Action: {action}')
        if type(action) == int or action.isdigit(): self.__set_radio(int(action))
        elif action in devices.dac_inputs: self.__set_dac_in(action)
        elif action == 'stereo': asyncio.create_task(devices.surround_toggle())
        elif action == 'reboot': self.__restart_vlc() #self.player.stop(); subprocess.Popen('reboot')
        elif action == 'pair':   asyncio.create_task(dbus_bluez.enable_pairing())
        else: print(f'Unknown action: {action}')

    def set_volume(self, volume):
        self.mixer.setvolume(volume)
        self.update_ui.set()

    def change_volume(self, step):
        old_vol = self.get_volume()
        self.set_volume(max(min(100, old_vol + step*3), 0))
        self.update_ui.set()

    def get_volume(self):   return self.mixer.getvolume()[0]
    def get_input(self):    return self.input
    def get_ui_state(self): return dict(input=self.input,volume=self.get_volume())

    def __set_radio(self, channel):
        asyncio.create_task(devices.set_aux('bt'))
        self.player.play_item_at_index(channel)
        self.input = channel
        self.update_ui.set()

    def __set_dac_in(self, ext_in):
        self.player.stop()
        asyncio.create_task(devices.set_aux(ext_in))
        self.input = ext_in
        self.update_ui.set()

    def __init_radios(self):
        self.vlc = vlc.Instance('-A alsa')
        self.player = vlc.MediaListPlayer(self.vlc)
        self.radios = vlc.MediaList(self.vlc)
        with open('radios.json') as json_file: radio_list = json.load(json_file)
        for _, stream in radio_list: self.radios.add_media(stream)
        self.player.set_media_list(self.radios)

    async def __shedule_player_restart(self):
        now = datetime.datetime.now()
        at3 = (now + datetime.timedelta(days=1)).replace(hour=3,minute=0,second=0)
        await asyncio.sleep((at3-now).total_seconds())
        pospond = 10
        while self.player.is_playing() and (pospond > 0):
            pospond -= 1
            await asyncio.sleep(3600)
        self.__restart_vlc()

    def __restart_vlc(self):
        print('Restarting VLC')
        self.player.stop()
        self.radios.release()
        self.player.get_media_player().release()
        self.player.release()
        for i in range(10): self.vlc.release()
        time.sleep(10)
        self.__init_radios()


# IR Device + Event Loop ----------------------------------------------------

keys = evdev.ecodes

def ir_key_pressed(key):
    print(f'Remote key pressed:   {keys.KEY[key]}')
    if   key == keys.KEY_VOLUMEDOWN:      s.change_volume(-1)
    elif key == keys.KEY_VOLUMEUP:        s.change_volume(+1)
    elif key == keys.KEY_0:               s.set_action(0)
    elif keys.KEY_1 <= key <= keys.KEY_4: s.set_action(key-keys.KEY_1+1)
    elif key == keys.KEY_6:               s.set_action('bt')
    elif key == keys.KEY_7:               s.set_action('pc')
    elif key == keys.KEY_8:               s.set_action('tv')
    elif key == keys.KEY_9:               s.set_action('off')
    elif key == keys.KEY_RED:             pass
    elif key == keys.KEY_GREEN:           pass
    elif key == keys.KEY_YELLOW:          pass
    elif key == keys.KEY_BLUE:            s.set_action('stereo')
    elif key == keys.KEY_UP:              pass
    elif key == keys.KEY_DOWN:            pass

def ir_key_hold(key):
    print(f'Remote key hold:      {keys.KEY[key]}')
    if   key == keys.KEY_VOLUMEDOWN: s.change_volume(-2)
    elif key == keys.KEY_VOLUMEUP:   s.change_volume(+2)

def ir_key_long(key):
    print(f'Remote key long hold: {keys.KEY[key]}')
    if   key == keys.KEY_6: s.set_action('pair')
    elif key == keys.KEY_9: s.set_action('reboot')

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

async def ir_loop():
    s.async_init()
    ir = find_ir_device()
    async for event in ir.async_read_loop():
        if event.type == keys.EV_KEY:
            KEY_PRESSED  = 0
            KEY_RELEASED = 1
            KEY_HOLD     = 2
            test_long_press(event.code)
            if event.value == KEY_RELEASED: ir_key_pressed(event.code)
            elif event.value ==   KEY_HOLD: ir_key_hold(event.code)

#--------------------------------------------------------------------------------

def shutdown_app(signum, frame): os._exit(0)

if __name__ == '__main__':
    main()

