# Implemented

 1. Bluetooth sink (hidden non discoverable) but pairing only manualy. I don't like to expose it
 1. Alsamixer device
 1. Vlc player with radio station media list
 1. Main async event loop. Listen only infrared keys (for now)
 1. Aux device 5.1 dac and audio switch controlled through gpio with async tasks
 1. In responce to keys (hold or press) send signals to mixer, vlc, aux

# TODO:
 1. Remote - more buttons (doc folder?)
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

