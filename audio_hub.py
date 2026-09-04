#!/usr/bin/env -S python3 -u
import asyncio, subprocess, datetime, time, json, signal, os, threading # official python packages
import evdev, vlc                # pip installed
from camilladsp import CamillaClient
import devices                   # local file require: OPi.GPIO
import dbus_bluez                # local file require: dbus_next
import http_server               # local file require: dbus_next

CAMILLA_HOST = '127.0.0.1'
CAMILLA_PORT = 1234
CAMILLA_MIN_DB = -60.0
CAMILLA_MAX_DB = 0.0

class CamillaMixer:
    def __init__(self, host=CAMILLA_HOST, port=CAMILLA_PORT):
        self.client = CamillaClient(host, port)
        self.lock = threading.Lock()

    def _request(self, label, callback):
        last_error = None
        for _ in range(2):
            try:
                if not self.client.is_connected():
                    self.client.connect()
                return callback()
            except Exception as error:
                last_error = error
                try:
                    self.client.disconnect()
                except Exception:
                    pass
                time.sleep(.1)
        raise RuntimeError(f'CamillaDSP request failed: {label}') from last_error

    def setvolume(self, volume):
        with self.lock:
            volume = max(min(100, int(volume)), 0)
            if volume == 0:
                self._request('SetMute', lambda: self.client.volume.set_main_mute(True))
                return
            self._request('SetVolume', lambda: self.client.volume.set_main_volume(round(self._percent_to_db(volume), 1)))
            self._request('SetMute', lambda: self.client.volume.set_main_mute(False))

    def getvolume(self):
        with self.lock:
            if self._request('GetMute', lambda: self.client.volume.main_mute()):
                return [0]
            return [self._db_to_percent(self._request('GetVolume', lambda: self.client.volume.main_volume()))]

    def close(self):
        with self.lock:
            try:
                if self.client.is_connected():
                    self.client.disconnect()
            except Exception:
                pass

    def _percent_to_db(self, volume):
        if volume <= 0:
            return CAMILLA_MIN_DB
        scale = volume / 100.0
        return CAMILLA_MIN_DB + (CAMILLA_MAX_DB - CAMILLA_MIN_DB) * scale

    def _db_to_percent(self, volume_db):
        if volume_db <= CAMILLA_MIN_DB:
            return 1
        if volume_db >= CAMILLA_MAX_DB:
            return 100
        scale = (volume_db - CAMILLA_MIN_DB) / (CAMILLA_MAX_DB - CAMILLA_MIN_DB)
        return max(1, min(100, round(scale * 100)))

def main():
    global s
    s = State()
    asyncio.run(s.loop())

class State:
    def __init__(self):
        signal.signal(signal.SIGINT,  shutdown_app)
        signal.signal(signal.SIGTERM, shutdown_app)
        self.__init_radios()
        self.mixer     = CamillaMixer()
        self.update_ui = asyncio.Event()
        self.red_led   = devices.System_Led('red',  'trigger','mmc0')
        self.green_led = devices.System_Led('green','trigger','rc-feedback')
        devices.init()
        http_server.run_thread(self)

    async def loop(self):
        loops = ir_loops()
        asyncio.create_task(dbus_bluez.init(self.red_led))
        asyncio.create_task(self.__shedule_player_restart())
        self.set_action('off')
        await asyncio.gather(*loops)

    def set_action(self, action):
        print(f'Action: {action}')
        if type(action) == int or action.isdigit(): self.__set_radio(int(action))
        elif action in devices.dac_inputs: self.__set_dac_in(action)
        elif action == 'reboot': subprocess.Popen('reboot')
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
        instance = vlc.Instance('-A alsa')
        self.player = vlc.MediaListPlayer(instance)
        radios = vlc.MediaList(instance)
        with open('radios.json') as json_file: self.radio_list = json.load(json_file)
        for _, stream in self.radio_list: radios.add_media(stream)
        self.player.set_media_list(radios)

    async def __shedule_player_restart(self):
        now = datetime.datetime.now()
        at3 = (now + datetime.timedelta(days=4)).replace(hour=3,minute=0,second=0)
        await asyncio.sleep((at3-now).total_seconds())
        pospond = 10
        while self.player.is_playing() and (pospond > 0):
            pospond -= 1
            await asyncio.sleep(3600)
        self.player.stop()
        subprocess.Popen('reboot')


# IR Device + Event Loop ----------------------------------------------------

keys = evdev.ecodes

def ir_key_pressed(key):
    print(f'Remote key pressed:   {keys.KEY[key]}')
    if   key == keys.KEY_VOLUMEDOWN:      s.change_volume(-1)
    elif key == keys.KEY_VOLUMEUP:        s.change_volume(+1)
    elif keys.KEY_1 <= key <= keys.KEY_0: s.set_action(key-keys.KEY_1)
    elif key == keys.KEY_BLUETOOTH:       s.set_action('bt')
    elif key == keys.KEY_VOICECOMMAND:    s.set_action('bt')
    elif key == keys.KEY_PC:              s.set_action('pc')
    elif key == keys.KEY_PAGEUP:          s.set_action('pc')
    elif key == keys.KEY_TV:              s.set_action('tv')
    elif key == keys.KEY_PAGEDOWN:        s.set_action('tv')
    elif key == keys.KEY_POWER:           s.set_action('off')

def ir_key_hold(key):
    print(f'Remote key hold:      {keys.KEY[key]}')
    if   key == keys.KEY_VOLUMEDOWN: s.change_volume(-2)
    elif key == keys.KEY_VOLUMEUP:   s.change_volume(+2)

def ir_key_long(key):
    print(f'Remote key long hold: {keys.KEY[key]}')
    if   key == keys.KEY_BLUETOOTH: s.set_action('pair')
    elif key == keys.KEY_POWER:     s.set_action('reboot')

def ir_loops():
    print("Searching for event devices")
    devices = [evdev.InputDevice(path) for path in evdev.list_devices()]
    loops = set()
    for device in devices:
        if (( device.name == 'sunxi-ir' ) or 
            ( device.name == 'HAOBO Technology USB Composite Device Keyboard')):
                print('Found: ',device.name)
                loops.add(asyncio.create_task(ir_loop(device)))
        else: device.close()
    return loops

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

async def ir_loop(device):
    try:
        async for event in device.async_read_loop():
            if event.type == keys.EV_KEY:
                KEY_PRESSED  = 0
                KEY_RELEASED = 1
                KEY_HOLD     = 2
                test_long_press(event.code)
                if event.value == KEY_RELEASED: ir_key_pressed(event.code)
                elif event.value ==   KEY_HOLD: ir_key_hold(event.code)
    except OSError as error:
        print('---------- ERROR USB GLITCH? ---------------')
        print(error)
        print('TODO: wait, re-initiate ',device.name)
        shutdown_app('restart')

#--------------------------------------------------------------------------------

def shutdown_app(signum, frame=None):
    exit_code = 0 if signum != 'restart' else 121
    try: s.mixer.close()
    except Exception as error: print('Error when disconnecting CamillaDSP: ',error)
    dbus_bluez.exit()
    try: devices.end()
    except OSError as error: print('Error when stopping GPIO: ',error)
    os._exit(exit_code)

if __name__ == '__main__':
    main()

