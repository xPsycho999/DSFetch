# DSFetch

A [fastfetch](https://github.com/fastfetch-cli/fastfetch)-style system information
tool for the Nintendo DS & DSi. It shows an ASCII clamshell logo (DS Lite or DSi)
on the top screen and a column of real system info — read straight from the
console — on the bottom screen.

Port of [3ds-fastfetch](https://github.com/xPsycho999/3ds-fastfetch) to the DS/DSi,
built with libnds.

## Features

- Auto-detects **DS vs DSi mode** at runtime; the DS Lite / DSi logo is chosen to match
- Accurate hardware model, running mode, ARM9/ARM7 clocks, RAM size
- Firmware username, language, favorite color, birthday, and greeting message
- Battery level, RTC date & time, SD-card storage usage
- **MAC address** (read from firmware); in DSi mode also **region** and **serial number**
- neofetch-style color palette strip
- Honest by design: any value that can't be read on the hardware is simply omitted

## Running

Copy `DSFetch.nds` anywhere on your SD card **except** the `/_nds/` folder
(e.g. a `roms/nds/` folder) and launch it from
[TWiLight Menu++](https://github.com/DS-Homebrew/TWiLightMenu) — or via a
forwarder or any DS homebrew launcher.

## Building

Requires [devkitARM](https://devkitpro.org/) and libnds (libnds 2.0 / calico).

```sh
make            # -> DSFetch.nds
```

## Credits & inspiration

A port of my own [3ds-fastfetch](https://github.com/xPsycho999/3ds-fastfetch),
itself inspired by [fastfetch](https://github.com/fastfetch-cli/fastfetch). Built on
[libnds](https://github.com/devkitPro/libnds) / [devkitPro](https://devkitpro.org/).

## License

Copyright (C) 2026 xPsycho999

This program is free software: you can redistribute it and/or modify it under
the terms of the **GNU General Public License v3.0** as published by the Free
Software Foundation. See the [LICENSE](LICENSE) file for the full text, or
<https://www.gnu.org/licenses/gpl-3.0.html>.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.
