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

If `hid_open_path` fails, grant Input Monitoring to the terminal.
