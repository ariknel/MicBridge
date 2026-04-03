# WASAPI → ASIO Bridge

A real-time audio bridge that captures from any USB microphone (or input device) via WASAPI and routes it to your DAW (Bitwig, Reaper, etc.) through a virtual audio device + FlexASIO.

```
USB Mic → WASAPI Capture → Lock-free Ring Buffer → WASAPI Render → Virtual Cable → FlexASIO → Bitwig
```

---

## Prerequisites

### Required

| Software | Purpose | Download |
|---|---|---|
| **VB-Cable** (free) | Virtual audio cable — the pipe between Bridge and FlexASIO | https://vb-audio.com/Cable/ |
| **FlexASIO** | ASIO wrapper for Windows audio devices | https://github.com/dechamps/FlexASIO/releases |
| **Visual Studio 2019/2022** | C++ compiler (Community = free) | https://visualstudio.microsoft.com/ |
| **CMake 3.20+** | Build system | https://cmake.org/download/ |

### Optional

| Software | Purpose |
|---|---|
| VoiceMeeter | More flexible virtual routing |
| ASIO4ALL | Alternative ASIO driver |

---

## Installation

### Step 1 — Install VB-Cable

1. Download from https://vb-audio.com/Cable/
2. Run **VBCABLE_Setup_x64.exe** as Administrator
3. Reboot if prompted
4. Verify: open Windows Sound settings → you should see **CABLE Input** (playback) and **CABLE Output** (recording)

### Step 2 — Install FlexASIO

1. Download the latest `FlexASIO_*.exe` from GitHub releases
2. Install (default settings)
3. FlexASIO will now appear in DAWs as an ASIO driver

### Step 3 — Configure FlexASIO

Create (or edit) `%USERPROFILE%\FlexASIO.toml`:

```toml
backend = "WASAPI"

[input]
device = "CABLE Output (VB-Audio Virtual Cable)"

[output]
device = "Default"   # or your speakers/headphones

latencyFrames = 480   # 10ms @ 48kHz; lower = less delay but more risk of glitches
```

> **Tip:** The `device` name must match exactly what Windows shows. Run `bridge.exe --list-outputs` to see available device names.

### Step 4 — Build the Bridge

```bat
# In the project root:
build.bat
```

Executables appear in `output\`:
- `bridge.exe` — CLI
- `bridge_gui.exe` — GUI (recommended)

---

## Usage

### GUI (Recommended)

```bat
output\bridge_gui.exe
```

1. **Input Device** — select your USB microphone
2. **Output Device** — select **★ CABLE Input (VB-Audio Virtual Cable)**
3. **Sample Rate** — match your FlexASIO/Bitwig project rate (usually 48000)
4. Click **▶ START BRIDGE**
5. Open Bitwig → Preferences → Audio → Driver: **FlexASIO**
6. Input channels will show CABLE Output → record away

### CLI

```bat
# List all devices
output\bridge.exe --list

# List only inputs
output\bridge.exe --list-inputs

# List only outputs
output\bridge.exe --list-outputs

# Start bridge: input 0, output 2, 48000 Hz
output\bridge.exe --input 0 --output 2 --rate 48000

# With debug logging to file
output\bridge.exe --input 0 --output 2 --debug --log bridge.log
```

CLI flags:

| Flag | Description | Default |
|---|---|---|
| `--list` | List all audio devices | — |
| `--list-inputs` | List capture devices | — |
| `--list-outputs` | List render devices | — |
| `--input <n>` | Select input by list index | default device |
| `--output <n>` | Select output by list index | default device |
| `--rate <hz>` | Sample rate | 48000 |
| `--buffer <frames>` | Buffer size | 480 |
| `--debug` | Verbose logging | off |
| `--log <file>` | Log file path | stdout only |

---

## Bitwig Setup

1. **Bitwig → Preferences → Audio**
   - Driver Type: `ASIO`
   - Audio Device: `FlexASIO`
   - Sample Rate: match your bridge setting
   - Block Size: 512 (or 256 for lower latency)

2. Click **Activate** — Bitwig should show input channels from CABLE Output

3. In the Bitwig **Arranger** or **Mixer**, create an audio track:
   - Input: `1/2` (or the channel pair FlexASIO exposes)
   - Arm the track (red button)
   - Speak into your mic — you should see signal

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     bridge_gui.exe / bridge.exe                 │
│                                                                 │
│  ┌──────────────┐    ┌─────────────────┐    ┌───────────────┐  │
│  │ WASAPI       │    │  Lock-free       │    │ WASAPI        │  │
│  │ CaptureClient│───▶│  Ring Buffer    │───▶│ RenderClient  │  │
│  │ (USB Mic)    │    │  (200ms / 9600f)│    │ (CABLE Input) │  │
│  └──────────────┘    └─────────────────┘    └───────────────┘  │
│    Thread: Capture     Shared SPSC buffer     Thread: Render    │
│    Priority: ProAudio                         Priority: ProAudio │
│                                                                 │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Resampler (linear interp)  if capture ≠ target rate       │ │
│  │  Format converter (Int16/24/32 → Float32)                  │ │
│  └────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
         ↓ CABLE Input (render)       ↑ CABLE Output (capture)
┌─────────────────────────────────────────────────────────────────┐
│              VB-Audio Virtual Cable (kernel driver)             │
└─────────────────────────────────────────────────────────────────┘
                                      ↓
┌─────────────────────────────────────────────────────────────────┐
│   FlexASIO (ASIO driver wrapping WASAPI → CABLE Output)         │
└─────────────────────────────────────────────────────────────────┘
                                      ↓
┌─────────────────────────────────────────────────────────────────┐
│                    Bitwig Studio (DAW)                          │
└─────────────────────────────────────────────────────────────────┘
```

### Key design decisions

| Decision | Rationale |
|---|---|
| **WASAPI Shared Mode** | Works without drivers; compatible with all USB audio devices; supports concurrent use |
| **SPSC lock-free ring buffer** | Zero mutex contention between capture and render threads; real-time safe |
| **Event-driven capture** | WASAPI event callback wakes thread only when data is ready; no busy-polling |
| **Pro Audio thread priority** | `AvSetMmThreadCharacteristics("Pro Audio")` gives OS scheduling priority to audio threads |
| **200ms ring buffer** | Absorbs jitter between capture/render callbacks without audible latency increase |
| **Linear interpolation resampler** | Sufficient for small ratio differences (44100↔48000); zero external dependencies |
| **Float32 internal format** | Native WASAPI shared mode format; no conversion overhead on most modern devices |

---

## Latency Budget

| Stage | Latency |
|---|---|
| WASAPI capture buffer | ~10ms |
| Ring buffer fill (target) | ~10ms |
| WASAPI render buffer | ~10ms |
| VB-Cable kernel driver | ~1ms |
| FlexASIO ASIO buffer | ~10ms |
| **Total typical** | **~30–40ms** |

To reduce latency:
- Lower `--buffer` (risk: underruns)
- Lower FlexASIO `latencyFrames`
- Use Exclusive Mode (future feature)

---

## Troubleshooting

### Bridge starts but no signal in Bitwig

1. Confirm **Output Device** in bridge = **CABLE Input**
2. Confirm FlexASIO `input.device` = **CABLE Output** (they're paired opposites)
3. In Bitwig, enable the track's input monitoring or arm it for record
4. Check `bridge.log` for errors

### "Failed to open capture device"

- Unplug and replug the USB mic
- Try a different USB port
- Check Windows Sound settings: mic must not be disabled
- Run `bridge.exe --list-inputs` to confirm the device is visible

### "Failed to open output device"

- VB-Cable not installed — install it and reboot
- CABLE Input disabled — re-enable in Windows Sound settings → Playback tab

### Underruns / clicking / dropouts

- Increase `--buffer` to 960 or 1920
- Close other audio applications
- Set your PC to **High Performance** power plan
- Disable USB power saving for the mic's USB hub
- Increase FlexASIO `latencyFrames`

### Wrong sample rate

The bridge auto-resamples if the capture device rate differs from your target. For best quality:
- Set bridge `--rate` to match your Bitwig project rate
- Set FlexASIO and Bitwig to the same rate

### Bitwig doesn't see FlexASIO

- Confirm FlexASIO is installed (check Add/Remove Programs)
- Restart Bitwig after installing FlexASIO
- Check `%USERPROFILE%\FlexASIO.toml` syntax with a TOML validator

---

## Building from Source

```bash
# Requirements: Visual Studio 2019/2022, CMake 3.20+

# Option A: One-click
build.bat

# Option B: Manual
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

Output: `build/bin/Release/bridge.exe` and `bridge_gui.exe`

---

## File Layout

```
wasapi-asio-bridge/
├── CMakeLists.txt          Main build file
├── build.bat               One-click build
├── README.md               This file
├── include/
│   ├── AudioEngine.h       Top-level orchestrator
│   ├── WasapiCapture.h     WASAPI capture thread
│   ├── AsioOutput.h        WASAPI render thread
│   ├── RingBuffer.h        Lock-free SPSC ring buffer
│   ├── Resampler.h         Linear interpolation resampler
│   ├── DeviceEnumerator.h  Device listing via MMDevice API
│   ├── Logger.h            Thread-safe logger
│   ├── AudioFormat.h       Format structs
│   ├── Common.h            Shared types, macros
│   ├── MainWindow.h        Win32 GUI window
│   └── LevelMeter.h        Level meter (rendered in MainWindow)
├── src/
│   ├── main_cli.cpp        CLI entry point
│   ├── main_gui.cpp        GUI entry point
│   ├── AudioEngine.cpp
│   ├── WasapiCapture.cpp
│   ├── AsioOutput.cpp
│   ├── RingBuffer.cpp
│   ├── Resampler.cpp
│   ├── DeviceEnumerator.cpp
│   ├── Logger.cpp
│   ├── MainWindow.cpp
│   └── LevelMeter.cpp
└── resources/
    └── resources.rc        App version info
```

---

## License

MIT — free to use, modify, distribute.
