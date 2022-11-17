# Implemented
 1. Bluetooth sink (hidden non discoverable) but pairing only manualy. I don't like to expose it
 1. Alsamixer device
 1. Vlc player with radio station media list
 1. Main async event loop. Listen only infrared keys (for now)
 1. Aux device 5.1 dac and audio switch controlled through gpio with async tasks
 1. In responce to keys (hold or press) send signals to mixer, vlc, aux
 1. Reboot every day at 3 a.m.

# TODO:
 1. Create files in /dev/shm/ at start
 1. Bloototh pairing from remote controller (long hold bluetooth button)

# Ideas to test:
 1. dbus for bluetooth (possible with async interface?)
 1. gpio led display to show current status of radio or input
 1. REST api or different mqtt?
 1. send ir events (needed?)

