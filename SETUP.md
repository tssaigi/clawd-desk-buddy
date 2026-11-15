# Setup guide (written for an AI coding agent)

You are an agent (for example Claude Code) helping your user set up a **Clawd desk buddy**:
an M5Stack Core2 that shows their Claude Code usage and acts out what Claude is doing.
Follow the steps in order. Each step ends with a check. Do not skip the checks.

The device arrives as a blank slate. It holds no account, no pairing and no settings.
Everything personal (the Claude sign-in, the session data) stays on the user's computer.

## Ground rules for you, the agent

- **Never handle the user's credentials.** The Claude sign-in in step 3 is done by the user
  in their own browser. Do not paste codes or tokens for them.
- **Ask before changing their machine**: installing apps, adding a login item (step 4) and
  editing Claude Code hooks (step 3) each need a yes from the user.
- **Do not flash with the Clawdmeter desktop app.** Its flasher does not know the Core2 and
  would write firmware for a different board. Flash only as described in step 2.
- This was built and tested on **macOS** with a **Core2 v1.1**. Linux and Windows should work
  for flashing and the bridge script, but the login-item scripts are macOS only. Say so to the
  user when it applies, and adapt (a systemd user unit on Linux, Task Scheduler on Windows).

## What the user needs

- An M5Stack Core2 (v1.0 or v1.1; the firmware detects which).
- A USB-C **data** cable. It stays plugged in: the live pages get their data over it.
- A computer with Claude Code, Bluetooth and Python 3.

## Step 1: get the files

```bash
git clone https://github.com/tssaigi/clawd-desk-buddy.git
cd clawd-desk-buddy
```

## Step 2: flash the firmware

Skip this step if the Core2 already shows a QR code to this repository when it starts.
It was flashed for them.

**Option A, the prebuilt image (quickest).** Download `clawd-desk-buddy-core2-*.bin` from this
repository's [Releases](https://github.com/tssaigi/clawd-desk-buddy/releases/latest) page, then:

```bash
python3 -m pip install esptool
python3 -m esptool --baud 460800 write-flash 0x0 clawd-desk-buddy-core2-v1.0.bin
```

If the write fails with a serial error, repeat it with `--baud 230400` or `--baud 115200`.

**Option B, build from source.** Needs Python 3.10 or newer.

```bash
python3 -m pip install platformio
pio run -d firmware -e m5stack_core2 -t upload
```

Notes:
- This overwrites whatever is on the Core2. M5Stack's M5Burner tool can restore the factory
  firmware later.
- Units made from August 2026 have a newer screen chip (ILI9342E). If colours look washed out
  or wrong after flashing, rebuild with
  `PLATFORMIO_BUILD_FLAGS="-DCORE2_PANEL_ILI9342E=1" pio run -d firmware -e m5stack_core2 -t upload`.
- If no serial port shows up, the computer may need the USB driver for the Core2's serial chip
  (WCH CH9102 or Silicon Labs CP2104). Current macOS has it built in.

**Check:** the Core2 restarts, shows the setup QR for 30 seconds (a tap skips it), then a
"Usage" screen with a small orange Clawd and "To pair: hold the B button".
With pyserial installed, `python3 core2/bridge/send_cmd.py info` prints one JSON line with
`"env":"m5stack_core2"` and the detected revision.

## Step 3: usage meters with the user's own Claude account

The meters (session % and week %) come from the **Clawdmeter desktop app**, the upstream
project this firmware is a fork of. It reads the usage of the Claude Code account signed in on
this computer and sends it to the device over Bluetooth.

1. Download the app for the user's OS from
   [HermannBjorgvin/Clawdmeter releases](https://github.com/HermannBjorgvin/Clawdmeter/releases/latest)
   and install it. On macOS it is unsigned: the user allows it once under
   System Settings, Privacy & Security, Open Anyway.
2. In its setup wizard **skip "Your board" and "Flash"**.
3. Pair: in the OS Bluetooth settings, connect **Clawdmeter**. It shows up as a keyboard
   because its buttons send key presses. If it does not appear, hold the **B** mark under the
   screen for 3 seconds and release. That clears old pairings on the device.
4. Claude sign-in. The app runs the Claude Code command-line tool to read usage, so that tool
   must be signed in. Find the exact binary the app uses: it is logged as `claude CLI: <path>`
   in the app's log (`~/.config/clawdmeter/daemon.log` on macOS and Linux). Then:
   - run `"<path>" auth status`. If it prints `"loggedIn": true`, nothing to do.
   - if not, ask the **user** to run `"<path>" auth login` and approve in their browser.
   Being signed in to the Claude desktop app is not enough; the command-line tool has its own
   sign-in.
5. Optional, ask first: the app's **Hooks** page edits Claude Code's `settings.json` (it shows
   the change and keeps a backup) so the device can alert when a session needs an answer.

**Check:** within a few minutes the Core2 shows real percentages instead of `--%`. If it says
"No data" for long, the app's log will say why. The usual cause is step 4.

## Step 4: the bridge (what Claude is doing, sessions, agents)

A small script on the computer reads Claude Code's own session files in
`~/.claude/projects` and tells the device, over the USB cable, what each session is doing.
Nothing leaves the computer.

macOS, as a login item (ask the user first):

```bash
core2/bridge/install_macos.sh
```

It creates a private Python environment in `core2/bridge/.venv`, installs pyserial and starts
the bridge now and at every login. The repository folder must stay where it is afterwards.
Remove it with `core2/bridge/uninstall_macos.sh`.

Any OS, by hand:

```bash
python3 -m pip install pyserial
python3 core2/bridge/clawd_status_bridge.py
```

**Check:** `core2/bridge/bridge.log` (or the terminal) shows `connected to <port>` and then
`state: ...` lines that follow what Claude Code is doing. On the device, Clawd walks to the
middle of the screen and acts it out: a cloud while thinking, a laptop while editing or
running commands, a magnifier while reading or searching.

The bridge holds the serial port. Before flashing again, pause it:
`core2/bridge/pause.sh`, and afterwards `core2/bridge/pause.sh resume`.

## Step 5: show the user how it works

| On the device | What happens |
|---|---|
| Swipe up on the main screen | The orange Clawd face. A tap sends it away. |
| Tap | Clawd runs off to one side. Follow him by swiping to that side. |
| Swipe right to left | Sessions page: one box per live Claude Code session, with its action and a context bar. |
| Swipe left to right | Colony page: each session's agents (the main Claude and its helpers). Tap a block to open it. |
| Swipe back the other way | Returns to the main screen. |
| **A** mark under the screen | Sends Space (Claude Code voice push-to-talk). |
| **B** mark | Brightness. Hold 3 seconds and release: clear Bluetooth pairing. |
| **C** mark | Sends Shift+Tab (Claude Code mode toggle). |
| Side button | Power: press to turn on, hold about 4 seconds to turn off. |

The device makes no sound. Its speaker is not used.

## Remove the startup QR

The QR code that shows for 30 seconds at every start (and when the screen wakes after 6 hours
or more of sleep) points at this repository. To turn it off:

**Without reflashing (kept across restarts):**

```bash
core2/bridge/pause.sh                       # only if the bridge is installed
python3 core2/bridge/send_cmd.py "bootqr off"
core2/bridge/pause.sh resume
```

The device answers `{"bootqr":false,"available":true}`. `bootqr on` brings it back,
`bootqr show` shows it once, right now.

**For good, at build time:** in `firmware/platformio.ini`, in the `[env:m5stack_core2]` block,
delete the two lines `-DLV_USE_QRCODE=1` and `-DCLAWD_REPO_URL=...` (or add `-DCLAWD_BOOT_QR=0`),
then rebuild and flash (step 2, option B). To point the QR somewhere else instead, change the
URL in `-DCLAWD_REPO_URL`. The code is in `firmware/src/boot_qr.cpp`.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| "Waiting for the Mac bridge" on the side pages | The bridge is not running, or the USB cable is charge-only or unplugged. |
| Meters show `--%` or go dim | The Clawdmeter app is not running or not paired, or the Claude command-line tool is signed out (step 3). |
| Flashing fails: port busy | The bridge holds the port. Pause it. |
| The device restarts when the bridge starts | Normal, once. Opening the serial port toggles the reset line. |
| Clawdmeter is in the Bluetooth list but will not connect | Hold **B** 3 seconds, then choose "Forget this device" on the computer and connect again. |
| Wrong colours | The ILI9342E screen variant. See the note in step 2. |

## Useful serial commands

Send with `core2/bridge/send_cmd.py` (115200 baud): `info`, `ble`, `unpair`, `bootqr [on|off|show]`,
`status <state>`, `page sessions|colony|home`, `clawd visit left|right`, `screenshot`
(see `.claude/skills/capturing-device-screenshots`).

## Where things are

- `firmware/src/boards/m5stack_core2/`: the Core2 port (power chips, touch, display).
- `firmware/src/gestures.cpp`, `sessions_view.cpp`, `colony_view.cpp`, `visitor.cpp`,
  `boot_qr.cpp`: the pages and gestures added in this fork. Status mirror: `director_status`
  in `clawd_director.cpp`.
- `core2/bridge/`: the computer-side bridge.
- `core2/enclosure/`: a 3D-printable Clawd body for the Core2.
- Everything else is upstream Clawdmeter. Its docs are in `docs/`.
