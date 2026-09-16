# bytech-kbd-led

Userspace RGB control for the BY Tech Gaming Keyboard (`258A:0049`) on macOS. Lighting is a HID feature report. No kernel driver.

Needs Homebrew `hidapi`. Do not send report `0x05` ISP commands to this chip.

```bash
make
./kbdled static '#ff2800'
./kbdled static red
./kbdled effect rainbow
./kbdled off
```

## Blank the leds when idle

The keyboard's own firmware has no sleep timeout we can reach, and macOS only
manages the built-in keyboard's backlight, so this is done from userspace:
`idle` polls `HIDIdleTime` on the `IOHIDSystem` IORegistry node (nanoseconds
since the last input on any HID device), blanks the leds past the timeout, and
replays the given lighting on the next input.

```bash
./kbdled idle --timeout 300 --poll 1 static '#ff2800'
./kbdled idle --timeout 60 effect rainbow
```

Runs in the foreground and restores the lighting on ctrl-c. `--timeout`
defaults to 300s and `--poll` to 1s; `--poll` only applies while the leds are
off, since while they are lit the loop sleeps straight to the deadline.

The restore state has to be passed on the command line: the mode/brightness
packet is write-only here, so the tool cannot read back what was set before.

To run it at login, wrap it in a launchd agent with `KeepAlive` — the terminal
running it needs Input Monitoring either way.

If `hid_open_path` fails, grant Input Monitoring to the terminal.
