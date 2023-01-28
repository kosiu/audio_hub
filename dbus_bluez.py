#!/usr/bin/env python3
import subprocess
import dbus_next
import asyncio

async def init(led_in):
    global led
    led = led_in
    subprocess.Popen(['bt-agent','--capability=NoInputNoOutput'])
    subprocess.Popen(['bluealsa-aplay','00:00:00:00:00:00'])

    global bus
    bus = await dbus_next.aio.MessageBus(bus_type=dbus_next.BusType.SYSTEM).connect()

    tree = await create_interface('/','org.freedesktop.DBus.ObjectManager')
    tree.on_interfaces_added(interfaces_added)

    monitor = await create_interface('/org/bluez/hci0','org.freedesktop.DBus.Properties')
    monitor.on_properties_changed(properties_changed)

    global adapter
    adapter = await create_interface('/org/bluez/hci0','org.bluez.Adapter1')

async def create_interface(path,interface):
    introspection = await bus.introspect('org.bluez', path)
    #print(introspection.tostring())
    proxy_object  = bus.get_proxy_object('org.bluez', path, introspection)
    return proxy_object.get_interface(interface)

def properties_changed(iface,value,_):
    if iface != 'org.bluez.Adapter1': return
    if 'Discoverable' not in value: return
    asyncio.create_task(led.blink(value['Discoverable'].value))

def interfaces_added(name,value):
    if not name.startswith('/org/bluez/hci0/dev_'): return
    name = name[20:].split('/',1)
    if len(name) != 1: return
    asyncio.create_task(disable_pairing())
    print(f'New Device: {name[0]}')

async def disable_pairing():
    await asyncio.sleep(10)
    await adapter.set_discoverable(False)
    await adapter.set_pairable(False)

async def enable_pairing():
    await adapter.set_discoverable(True)
    await adapter.set_discoverable_timeout(180)
    await adapter.set_pairable(True)
    await adapter.set_pairable_timeout(180)

async def test():
    await init()
    await enable_pairing()
    print('Waiting...')
    await bus.wait_for_disconnect()

if __name__ == '__main__':
    asyncio.run(test())
    # test requires mockup of led

