# Paddle Power — Garmin Connect IQ DataField

Displays BLE Cycling Power Service (UUID 0x1818) data as a full-screen DataField on
fenix/epix watches.  The field manages BLE entirely itself, bypassing Garmin's native
sensor pairing (which doesn't expose power in paddle activity profiles).

## Layout

```
┌──────────────────┐
│ PWR              │
│      245 W       │  ← large, current instantaneous power
│ 3s: 241  Avg:238 │  ← small, 3-second rolling avg + session avg
└──────────────────┘
```

Status messages ("Scanning...", "Pairing...", etc.) replace the metrics until a
connection is established.

## Prerequisites

| Tool | Path |
|------|------|
| Connect IQ SDK | `/home/gavinok/.Garmin/ConnectIQ/Sdks/connectiq-sdk-lin-8.4.1-2026-02-03-e9f77eeaa` |
| Developer key | `connect_iq_power/developer_key` (binary DER format, already present) |
| Java 11+ | required by monkeyc (included with SDK) |

Device definitions must be downloaded for any device you want to build against.
Currently downloaded: `fenix7`, `fenix7pro`, `fenix7s`, `fenix7spro`.
The `./monkeyc` wrapper handles paths automatically.

## Build

```bash
cd connect_iq_power

# Build for fenix7 (recommended — device definition downloaded)
./monkeyc -d fenix7 -o PaddlePower_fenix7.prg

# Build for fenix7pro
./monkeyc -d fenix7pro -o PaddlePower_fenix7pro.prg

# Build with warnings enabled
./monkeyc -d fenix7 -o PaddlePower.prg -w

# Build release (stripped debug info)
./monkeyc -d fenix7 -o PaddlePower.prg -r

# Create a distributable .iq package (all targets)
./monkeyc -d fenix7 --package-app -e -o PaddlePower.iq
```

The `./monkeyc` wrapper is equivalent to:
```bash
SDK=/home/gavinok/.Garmin/ConnectIQ/Sdks/connectiq-sdk-lin-8.4.1-2026-02-03-e9f77eeaa
$SDK/bin/monkeyc \
  -f monkey.jungle \
  -y developer_key \
  -d <device> \
  -o <output.prg>
```

## Simulate

```bash
# Start the simulator (opens a GUI window)
SDK=/home/gavinok/.Garmin/ConnectIQ/Sdks/connectiq-sdk-lin-8.4.1-2026-02-03-e9f77eeaa
$SDK/bin/simulator &

# Then load the .prg via the simulator's File > Load menu,
# or use monkeydo to launch it directly:
$SDK/bin/monkeydo PaddlePower_fenix7.prg fenix7
```

Note: the simulator does **not** support real BLE scanning; it will stay on "Scanning..."
but you can test the layout and drawing code.

## Download More Device Definitions

Use the Connect IQ SDK Manager to download additional device definitions:
```bash
SDK=/home/gavinok/.Garmin/ConnectIQ/Sdks/connectiq-sdk-lin-8.4.1-2026-02-03-e9f77eeaa
$SDK/bin/sdkmanager   # opens the SDK manager GUI
```

Or download device definitions from: https://developer.garmin.com/connect-iq/sdk/

## Deploy to Watch (Sideload)

1. Build the `.prg` file for your specific watch model.
2. Connect the watch via USB (MTP mode).
3. Copy the `.prg` to `GARMIN/APPS/` on the watch storage.
4. Eject and the field appears under "Data Field" in Connect IQ apps.

Alternatively, use the Garmin Connect IQ phone app or Garmin Express to sideload.

## Supported Devices (manifest)

`fenix6`, `fenix7`, `fenix7pro`, `fenix7x`, `fenix7xpro`,
`epix2`, `epix2pro47mm`, `epix2pro51mm`, `epix2pro42mm`, `venu3`

To add more devices, edit `manifest.xml` and add `<iq:product id="..."/>` entries.

## Project Structure

```
connect_iq_power/
├── monkeyc                     ← helper build script (run this)
├── monkey.jungle               ← build config
├── manifest.xml                ← app metadata & supported devices
├── developer_key               ← signing key (binary DER, keep private)
├── source/
│   ├── PaddlePowerApp.mc       ← AppBase; starts BLE scan, holds shared state
│   ├── PaddlePowerView.mc      ← DataField; draws 3 metrics
│   └── PowerBleDelegate.mc     ← BLE state machine + packet parser
└── resources/
    ├── strings/strings.xml     ← app name string
    └── drawables/
        ├── drawables.xml       ← launcher icon declaration
        └── launcher_icon.svg   ← icon (replace with a custom one)
```

## BLE State Machine

```
SCANNING  →(UUID 0x1818 found)→  PAIRING
PAIRING   →(onConnectedStateChanged CONNECTED)→  CONNECTED
CONNECTED →(getService/getCharacteristic)→  SUBSCRIBING
SUBSCRIBING →(CCCD write STATUS_SUCCESS)→  RECEIVING
RECEIVING →(onCharacteristicChanged)→  parse 0x2A63 → onPowerReading()
any state →(disconnect)→  SCANNING
```

## Troubleshooting

| Error | Fix |
|-------|-----|
| `Invalid device id` | Download device definition via SDK Manager |
| `Unable to load private key` | Key must be binary DER format, not PEM |
| `A launcher icon must be specified` | Add `launcherIcon="@Drawables.LauncherIcon"` to manifest |
| Build works but "Scanning..." on watch | Normal — BLE scanning works only on real hardware |

## Side Loading
jmtpfs ~/garmin
cp code/other/power/connect_iq_power/bin/PaddlePower.prg garmin/Internal\ Storage/GARMIN/Apps/
fusermount -u ~/garmin

