# Implemented

 1. Create bluetooth sink (hidden non discoverable) pairing only manualy. I don't like to expose
 1. Create alsamixer device
 1. Create vlc player with radio station media list
 1. Create main async event loop to listen only for infrared keys (for now)
 1. In reponce to keys (hold or press) send signals to mixer or vlc

# TODO:

 1. Incorporate gpio (test included)
 1. Asyncio for gpio
 1. Write short how-to on pairing unpairing new device


# Ideas to test:

 1. reboot every 2 days?
 1. dbus for bluetooth (possible with async interface?)
 1. gpio led display to show current status of radio or input
 1. REST api or different mqtt?
 1. send ir events (needed?)

# Auto-pair agent

    bluetoothctl discoverable on
    bt-agent --capability=NoInputNoOutput

# mplayera (vlc is better for that)
    mplayer -prefer-ipv4 -cache 512 -cache-min 60 https://stream.rcs.revma.com/ye5kghkgcm0uv

