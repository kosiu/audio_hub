#!/usr/bin/env python3
import argparse
from pathlib import Path

def main():
    src = [
        Path('/etc/default/bluez-alsa'),
        Path('/etc/modules-load.d/camilladsp-aloop.conf'),
        Path('/etc/systemd/system/audio_hub.service'),
        Path('/etc/systemd/system/camilladsp.service'),
        Path('/etc/systemd/system/camillagui.service'),
        Path('/etc/systemd/logind.conf'),
        Path('/etc/udev/rules.d/99-gpio.rules'),
        Path('/etc/asound.conf'),
        ]
    dest = Path('system_files')
    for f in src:
        if not f.exists():
            print(f'Skipping missing file: {f}')
            continue
        (dest/f.name).write_bytes(f.read_bytes())

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
            description='Copy specified (tracked) system files to "system_files" folder.')
    args = parser.parse_args()
    main()

