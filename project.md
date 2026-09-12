# Audio Hub 2.0

## Purpose

Audio Hub is an OrangePi-based controller and audio processor. It combines:

1. Internet radio and Bluetooth audio.
2. TV audio received over TOSLINK.
3. A future PC source based on Moonlight and Sunshine.
4. CamillaDSP volume, routing, and speaker processing.
5. IR remote and HTTP control.
6. Amplifier standby control.

## Audio Architecture

ALSA exposes one eight-channel capture contract named `full_8ch`:

1. Channels `0..5` carry TV audio from `toslink_play` through the `Surround`
   ALSA loopback device.
2. Channels `6..7` carry local stereo from VLC, `bluealsa-aplay`, and eventually
   the Moonlight PC client through the `Stereo` ALSA loopback device.
3. CamillaDSP consumes `full_8ch` and produces the six physical speaker outputs.

The TV is the only TOSLINK device. `toslink_play` reads the optical capture
device, detects the transport, decodes AC-3 when needed, expands PCM stereo to
six channels, and writes to `Surround`.

## Source Actions

`audio_hub.py` owns the logical source state:

| Action | Behavior |
| --- | --- |
| Radio index | Stop `toslink_play`, enable the amplifier, and play the selected VLC stream. |
| `bt` | Stop VLC and `toslink_play`, enable the amplifier, and use the local Bluetooth path. |
| `pc` | Stop VLC and `toslink_play`, enable the amplifier, and reserve the local path for a future Moonlight client. |
| `tv` | Stop VLC, put the amplifier in standby, and start `toslink_play`. Its reported signal state then controls STB. |
| `off` | Stop VLC and `toslink_play`, then put the amplifier in standby. |
| `pair` | Temporarily enable Bluetooth pairing. |
| `reboot` | Reboot the OrangePi. |

The historical name `bt` currently means the local Bluetooth source. Radio
stations also use the local stereo path but retain their numeric state for the
web UI.

## TOSLINK Player Contract

`toslink_play` takes no command-line arguments.

It writes the detected signal state to standard output, one line at a time:

```text
off
none
pcm
ac3
```

Writing a newline to its standard input asks it to print the current state.
`audio_hub.py` sends this query immediately after launching the process. The
process is started once when TV is selected, left running on repeated TV
selections, and terminated when another source is selected or the app exits.
While TV is selected, `off` and `none` keep the amplifier in standby; `pcm` and
`ac3` enable it. This mutes transitions between absent and active TV signals.

## Hardware Contract

The only application-controlled GPIO connection is amplifier STB on header pin
8, kernel GPIO 354:

1. `LOW` or high impedance puts the amplifier in standby.
2. `HIGH` enables the amplifier.

`devices.py` owns this GPIO and the OrangePi system LED helpers used by Bluetooth
pairing. It initializes STB low and restores standby during shutdown.

## Runtime Components

| File | Role |
| --- | --- |
| `audio_hub.py` | Main state, source actions, process supervision, volume, radio, and IR input. |
| `toslink_play.cpp` | TV optical capture, signal detection, AC-3 decoding, and six-channel playback. |
| `devices.py` | Amplifier STB GPIO and system LED helpers. |
| `dbus_bluez.py` | BlueZ pairing and Bluetooth sink helper processes. |
| `http_server.py` | REST API, SSE state updates, and static web UI. |
| `camilla_cfg/main.yaml` | CamillaDSP eight-input to six-output processing configuration. |
| `system_files/asound.conf` | ALSA `Stereo`, `Surround`, and `full_8ch` routing. |

## Deployment

`system_files/audio_hub.service` starts `audio_hub.sh`, which restarts the Python
application only after the controlled restart exit code `121`. CamillaDSP must
be running first. The `snd-aloop` configuration supplies the local stereo and TV
surround paths.

The HTTP API remains intentionally small:

1. `/get` returns input and volume.
2. `/get_radios` returns configured stations.
3. `/set` changes action or volume.
4. `/update` streams state changes using SSE.

## Next PC Step

The `pc` action is currently a placeholder. Its future implementation should
start and stop a Moonlight client in the same way that the `tv` action owns
`toslink_play`, while continuing to feed the local stereo ALSA path.