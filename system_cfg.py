#!/usr/bin/env python3
import argparse
from pathlib import Path

def main():
    src = [
        Path('/etc/systemd/system/audio_hub.service'),
        Path('/etc/rc_keymaps/protocols/custom.toml'),
        Path('/etc/rc_keymaps/protocols/lg.toml'),
        ]
    dest = Path('system_files')
    for f in src:
        (dest/f.name).write_bytes(f.read_bytes())

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
            description='Copy specified (tracked) system files to "system_files" folder.')
    args = parser.parse_args()
    main()

