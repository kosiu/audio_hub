# Audio Hub 1.0

## Purpose

Audio Hub 1.0 is an OrangePi-based home audio switcher and local audio source.

It combines four jobs:

1. Select the active DAC input.
2. Produce local audio for internet radio and Bluetooth sink.
3. Expose control through IR remote and HTTP API.
4. Mirror a small part of the hardware state back into software.

The current system is built around a physical DAC and amplifier chain, not around pure software routing.

## Description

OrangePi is the control center.

It reads commands from:

1. The infrared remote.
2. The HTTP server and web UI.
3. Bluetooth pairing events.

It drives:

1. The DAC input selector through GPIO button emulation.
2. The DAC input state detection through GPIO reads of LED lines.
3. ALSA volume.
4. VLC internet radio playback.
5. BlueZ pairing mode and Bluetooth audio sink playback.

Audio path summary:

1. OrangePi produces local audio for radio and Bluetooth sink.
2. OrangePi analog/internal audio output goes into one DAC input.
3. PC and TV are connected to two DAC digital inputs.
4. One DAC digital input is intentionally used as a silent or off state.
5. DAC output goes to the 5.1 amplifier.
6. Amplifier output goes to speakers.

Important naming note:

The software action `bt` is a historical name.
In practice it means the OrangePi local audio path.
That path is shared by Bluetooth sink and internet radio.
For Hardware 2.0 it should probably be renamed to `local` or `opi`.

Important power note:

The software action `off` does not power down the amplifier or DAC.
It selects a DAC input that is treated as silent.

![Audio Hub 1.0 hardware view](project_architecture.svg)

## Software Architecture

Control flow summary:

1. `audio_hub.py` creates global runtime state.
2. It starts the HTTP server thread.
3. It starts Bluetooth event monitoring.
4. It discovers evdev input devices for IR and USB remote input.
5. Every command is normalized into `set_action()` or `set_volume()`.
6. Hardware-facing work is delegated to `devices.py` and `dbus_bluez.py`.

Runtime actions:

1. `0..9` selects a radio stream in VLC and forces the DAC to the OrangePi local input.
2. `bt` selects the OrangePi local input.
3. `pc` selects the PC DAC input.
4. `tv` selects the TV DAC input.
5. `off` selects the silent DAC input.
6. `pair` enables temporary Bluetooth pairing mode.
7. `reboot` reboots the whole OrangePi.

## Hardware Interface Contract

This is the key section for Hardware 2.0.

### 1. DAC input select interface

Owned by `devices.py`.

Current contract:

1. There is no direct source-select bus.
2. OrangePi emulates a press of the DAC input button by pulling one GPIO line low for about `0.2s`.
3. The DAC moves to the next input in its own internal cycle.
4. Software waits about `0.8s` for the DAC to react.
5. Software reads three LED sense lines to infer which input is active.
6. Software repeats the fake button press until the inferred input matches the requested logical state.

Implication for Hardware 2.0:

1. This is fragile but simple.
2. If the new hardware has a direct source-select interface, replace the pulse-and-infer loop first.
3. Keep a software API equivalent to `set_aux(name)` so the rest of the program stays stable.

### 2. DAC state feedback interface

Owned by `devices.py`.

Current contract:

1. Three GPIO inputs watch three LED lines.
2. LED1 low means optical input 1.
3. LED2 low means optical input 2.
4. LED3 low means coaxial input.
5. If none is active, software treats that as input `0`, which is the OrangePi local path.

Implication for Hardware 2.0:

1. State is inferred indirectly.
2. Any redesign should expose selected input directly.
3. The mapping from physical indicator to logical source should be made explicit in configuration, not code comments.

### 3. Local audio production interface

Owned mostly by `audio_hub.py`.

Current contract:

1. VLC plays internet radio streams.
2. `bluealsa-aplay` plays Bluetooth sink audio.
3. Both rely on the OrangePi audio stack.
4. Both share one physical DAC input path.

Implication for Hardware 2.0:

1. Bluetooth and internet radio are not independent hardware inputs.
2. They are software sources mixed into one local hardware path.
3. Keep that distinction clear in naming and UI.

### 4. Input devices

Owned by `audio_hub.py` plus `system_files/*.toml`.

Current contract:

1. Commands come from Linux evdev key events.
2. Supported device names are hard-coded.
3. Long press for some keys is detected by a timing hack because one remote does not send proper hold events.

Implication for Hardware 2.0:

1. Input abstraction is thin.
2. A richer input layer would remove device-name coupling and timing hacks.

## File Map

### Runtime files

| File | Role | Status | Hardware 2.0 relevance |
| --- | --- | --- | --- |
| `audio_hub.py` | Main runtime, state, command dispatch, radio playback, restart policy, IR loop | Active | High |
| `audio_hub.sh` | Restarts `audio_hub.py` when it exits with code `121` | Active | Medium |
| `devices.py` | GPIO contract with DAC buttons and DAC LED sensing | Active | Critical |
| `dbus_bluez.py` | BlueZ pairing control and Bluetooth sink helper processes | Active | High |
| `http_server.py` | REST API, SSE updates, static file serving | Active | Medium |
| `radios.json` | Radio station list | Active | Low |
| `system_cfg.py` | Copies tracked system files into repo | Support | Low |
| `plot.py` | Local thermal plotting helper | Support | None |
| `readme.md` | Development notes and partial history | Stale | Low |
| `evdev_ecodes.md` | Reference dump of input key names | Reference | None |
| `spi.cpp` | Partial SPI source fragment, not connected to runtime | Obsolete | None |

### Web UI files

| File | Role | Status | Hardware 2.0 relevance |
| --- | --- | --- | --- |
| `static/index.html` | Control panel layout | Active | Low |
| `static/main.js` | REST calls, SSE subscription, dynamic radio buttons, theme toggle | Active | Low |
| `static/styles.css` | Small custom styling | Active | Low |

### Deployment and OS integration files

| File | Role | Status | Hardware 2.0 relevance |
| --- | --- | --- | --- |
| `system_files/audio_hub.service` | Systemd unit for boot startup | Active | Medium |
| `system_files/camilladsp.service` | Systemd unit for CamillaDSP engine | Active | High |
| `system_files/camillagui.service` | Systemd unit for CamillaGUI backend | Active | Medium |
| `system_files/bluez-alsa` | Enables A2DP sink mode | Active | High |
| `system_files/99-gpio.rules` | Grants GPIO and LED sysfs access | Active | Critical |
| `system_files/custom.toml` | Custom remote keymap | Active | Medium |
| `system_files/lg.toml` | LG remote keymap | Active | Medium |
| `system_files/rc_maps.cfg` | Auto-loads both keymaps | Active | Medium |
| `system_files/logind.conf` | Ignores system power key | Active | Low |

## Coarse Notes Per File

### `audio_hub.py`

The main application.
It translates remote and API requests into player, mixer, Bluetooth, and DAC actions.

### `audio_hub.sh`

A small wrapper.
It restarts the Python app only when the app asks for a controlled restart by exiting with code `121`.

### `devices.py`

The hardware adapter.
This is the most important file for migration.

### `dbus_bluez.py`

Bluetooth glue.
It starts helper processes and manages temporary pairing mode through BlueZ D-Bus.

### `http_server.py`

A minimal FastAPI server.
It serves the UI, exposes REST endpoints, and pushes state updates over SSE.

### `radios.json`

Static list of radio names and stream URLs.

### `system_cfg.py`

A maintenance helper.
It copies selected live system files into the repository snapshot.

### `piano2.wav`

Looks like a local sample or test asset.
It is ignored by Git and not referenced by the runtime.

### `plot.py`

A local terminal utility.
It plots CPU temperatures.
It is not part of the audio runtime.

### `readme.md`

Mixed notes.
Useful as project history, but not reliable as current architecture documentation.

### `evdev_ecodes.md`

Reference material only.
Not part of runtime.

### `spi.cpp`

Looks like a partial code excerpt for SPI experiments.
It is not imported, built, or referenced.

### `static/index.html`

The browser UI structure.

### `static/main.js`

The browser control logic.

### `static/styles.css`

Small UI tweaks.

### `system_files/audio_hub.service`

Boot integration.

### `system_files/bluez-alsa`

Bluetooth audio sink mode configuration.

### `system_files/99-gpio.rules`

Permission bridge between Linux device files and the app.

### `system_files/custom.toml`

Primary remote map for the custom IR remote.

### `system_files/lg.toml`

Alternate remote map for an LG remote.

### `system_files/rc_maps.cfg`

Loads remote maps into the kernel input stack.

### `system_files/logind.conf`

Prevents the OS power-key handler from fighting with the application.

## Detailed Notes For Important Files

### `devices.py`

Why it matters:

1. This file is the real hardware abstraction layer.
2. Hardware 2.0 should start here.

What it controls:

1. DAC input selection by emulating a front-panel button.
2. DAC source detection by reading LED lines.

Current logical input map:

1. `bt` -> local OrangePi audio path.
2. `pc` -> one DAC digital input.
3. `tv` -> one DAC digital input.
4. `off` -> DAC input reserved as silent state.

Important implementation details:

1. `gpio.setup(leds, gpio.IN)` makes the DAC indicator lines inputs.
2. `gpio.setup(input_btn, gpio.OUT, initial=gpio.HIGH)` prepares the fake input-select button.
3. `set_aux()` serializes overlapping source changes through the global `aux_to_select` variable.
4. `get_aux()` stores its inferred state into `/dev/shm/aux`.

Migration concerns:

1. Source identity is inferred, not read directly.
2. Source changes are cyclical, not addressed directly.
3. Timing values are hardware-dependent.
4. The logical names hide the fact that `bt` is also radio.

Suggested Hardware 2.0 target:

1. Replace pulse-and-wait with direct input selection.
2. Replace LED sensing with direct state readback.
3. Rename logical sources to match physical reality.
4. Move pin mapping and source mapping into configuration.

### `audio_hub.py`

Why it matters:

1. It is the command router.
2. It defines the state model used by the remote and web UI.

Main responsibilities:

1. Load radio definitions from `radios.json`.
2. Create an ALSA mixer on `Master`.
3. Start `http_server.run_thread(self)`.
4. Start Bluetooth monitoring with `dbus_bluez.init()`.
5. Scan evdev devices and attach async loops.
6. Translate button presses into normalized actions.

Action model:

1. Numeric action -> play radio station and force local input.
2. DAC input name -> stop VLC and select external or local source.
3. `pair` -> open Bluetooth pairing window.
4. `reboot` -> reboot OS.

Important implementation details:

1. The state value `input` is overloaded.
2. It can contain a string like `pc` or `tv`.
3. It can also contain an integer for a radio index.
4. That is enough for the current UI, but it is weak typing.
5. There is a scheduled reboot every few days as a workaround for long-running radio failures.
6. Long press detection is partly synthetic because one remote does not emit clean hold events.
7. A missing evdev device can force an app restart path.

Migration concerns:

1. Local audio source and physical input are mixed into one field.
2. Restart logic encodes operational workarounds, not core product behavior.
3. Device-name matching in `ir_loops()` is hardware-specific.

Suggested Hardware 2.0 target:

1. Separate `selected_physical_input` from `selected_local_source`.
2. Move remote profiles into configuration.
3. Replace global singleton state with clearer typed state.

### `dbus_bluez.py`

Why it matters:

1. It defines the Bluetooth operating mode.
2. It controls user-visible pairing behavior.

Main responsibilities:

1. Start `bt-agent` with `NoInputNoOutput` capability.
2. Start `bluealsa-aplay` as the Bluetooth sink playback path.
3. Watch BlueZ adapter properties over D-Bus.
4. Blink the red system LED while the adapter is discoverable.
5. Disable pairing automatically after a timeout or a new device event.

Migration concerns:

1. The process model is hard-coded.
2. The Bluetooth stack assumes BlueZ plus bluez-alsa.
3. Adapter path `/org/bluez/hci0` is fixed.

Suggested Hardware 2.0 target:

1. Keep pairing state as a service boundary.
2. Consider making the backend pluggable if the audio stack changes.

### `http_server.py`

Why it matters:

1. It is the software integration surface for phones, tablets, or other clients.
2. It is not the main hardware seam, but it is the easiest place to keep compatibility.

Endpoints:

1. `/` -> redirect to the static UI.
2. `/get` -> current input and volume.
3. `/get_radios` -> radio list.
4. `/set` -> mutate action and volume.
5. `/update` -> SSE stream for live UI refresh.

Important implementation details:

1. The server binds to a fixed IP address: `192.168.1.18`.
2. It runs in a background thread.
3. The module contains a local `State` mock used only for standalone UI testing.

Migration concerns:

1. The hard-coded IP is deployment-specific.
2. The API is small and worth keeping stable.

### `system_files/*`

Why they matter:

1. They are required for a working deployed box.
2. The runtime code depends on them more than the repository structure suggests.

Key pieces:

1. `99-gpio.rules` grants access to GPIO and LED sysfs nodes.
Without it, `devices.py` and `System_Led` will fail under a normal service user.
2. `audio_hub.service` starts the wrapper script at boot after the network comes up.
3. `camilladsp.service` starts the DSP engine with websocket, statefile, and log file support.
4. `camillagui.service` starts the CamillaGUI backend at boot so the browser UI is reachable over the LAN.
5. `bluez-alsa` enables A2DP sink behavior.
6. `custom.toml`, `lg.toml`, and `rc_maps.cfg` translate remote scancodes into Linux key events that `audio_hub.py` understands.
7. `logind.conf` sets `HandlePowerKey=ignore`, which avoids conflict with remote or front-panel power semantics.

## Obsolete, Stale, Or Historical Items

These should be treated carefully during migration.

1. `spi.cpp` looks obsolete.
It is not wired into the build or runtime.
2. `plot.py` is a local diagnostic helper.
It is unrelated to the product behavior.
3. `evdev_ecodes.md` is reference material only.
4. `readme.md` is partially stale.
One TODO says the radio list should be generated in JavaScript, but that is already implemented in `static/main.js`.
5. `http_server.py` contains a mock `State` class.
That is useful for isolated UI testing, but not part of the real runtime once imported by `audio_hub.py`.
6. The logical action name `bt` is historically stale.
It no longer describes only Bluetooth.

## Hardware 2.0 Migration Checklist

Do these items first.

1. Define the new physical input map using explicit names.
Suggested starting names: `local`, `pc`, `tv`, `mute`.
2. Replace the cyclical DAC button emulation with direct addressed control if the new hardware allows it.
3. Replace LED inference with direct readback.
4. Keep a thin adapter layer that preserves today’s software API until the rest of the app is cleaned up.
5. Split local-source selection from physical-input selection in the runtime state.
6. Decide whether Bluetooth still uses `bluez-alsa` or moves to a different audio backend.
7. Move hard-coded hardware constants, GPIO maps, device names, and timing values into configuration.
8. Keep the HTTP API small and backward compatible unless there is a strong reason to break it.
9. Re-evaluate the reboot workaround after audio stack and hardware changes.

## Recommended Rename Map For Future Refactor

These are documentation names only for now.

| Current name | Better meaning |
| --- | --- |
| `bt` | `local` or `opi_audio` |
| `off` | `mute_input` or `silent_input` |
| `set_aux()` | `select_dac_input()` |
| `next_aux()` | `pulse_input_select_button()` |

## Final Summary

Audio Hub 1.0 is a hardware-centered control system.
The software is small, but it encodes several physical assumptions.

The most important migration rule is simple:

Preserve the boundary around source selection, source readback, and local audio generation.

If Hardware 2.0 gets a cleaner electrical or digital interface, replace only the adapter layer first.
That keeps the remote logic, Bluetooth logic, radio logic, and HTTP API stable while the hardware changes underneath.