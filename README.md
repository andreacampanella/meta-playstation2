# meta-playstation2

A Yocto layer that builds a modern Linux system for the Sony PlayStation 2.

It gives you a 5.4 kernel with drivers for the console hardware, a root file system on a USB drive or the internal hard disk, an OpenGL 1.1 library that draws with the console's vector unit and Graphics Synthesizer, an SDL2 video driver, ALSA sound, and an optional X desktop. Doom, Duke Nukem 3D, Quake and Descent build from their open source ports with no change to their game code.

## What works

| Hardware | Status |
|---|---|
| Graphics Synthesizer (GS) and vector unit 1 | Text console, OpenGL 1.1 fixed function (`libGL.so.1`), SDL2, a mappable `/dev/fb0` for X |
| CPU floating point unit | On, for single precision. Doubles run in software, or fast and less precise for the games |
| Network adapter (Ethernet) | DHCP and an SSH server at boot |
| Hard disk in the network adapter | APA partitions (the format of the console software, uLaunchELF and OPL). The system can run from the disk |
| USB 1.1 | Storage, keyboard, mouse, USB network adapters |
| Sound (SPU2) | ALSA, 48 kHz stereo, analog and optical |
| Real-time clock | Yes |

Not done: the gamepad, the DVD drive, the memory cards, i.Link.

## Performance

Measured on a PAL SCPH-3x000, at 320x240, with the wait for the vertical sync off (`PS2GL_VSYNC=0`):

| Program | Frame rate |
|---|---|
| Descent (D1X-Rebirth, OpenGL, 640x480) | about 50 fps, with some dips |
| Duke Nukem 3D (Polymost renderer, OpenGL), the first level | about 18 fps |
| Quake (QuakeSpasm, OpenGL), the hall of the start map | about 15 fps |
| Doom (doomgeneric, software renderer, scaled by the GS) | plays well, not measured |

## Limits

- **RAM.** The console has 32 MB. After the kernel takes its part, Linux manages 28 MB, and about 20 MB is free for programs after boot. Quake barely fits. The X desktop takes 7 to 10 MB, which leaves too little to run a game under it.
- **CPU.** The R5900 at 294 MHz has no double precision, and a load from main memory takes about 130 ns. Software that was written for a PC is slow here.
- **Storage.** USB 1.1 moves about 1 MB/s. The hard disk driver uses PIO, about 3 MB/s.
- **The X desktop** draws with the CPU into RAM, and the kernel copies the changed rows to the GS. It is usable, but not fast.

This is a project to explore the hardware, not a daily system.

## What you need

- A "fat" PlayStation 2 with the expansion bay (SCPH-10000 to SCPH-50xxx). It was tested on a PAL SCPH-3x000. Might work on slims.
- A way to start an ELF file, for example uLaunchELF from a memory card with FreeMcBoot.
- A USB drive of at least 2 GB, and a USB keyboard.
- Optional: the network adapter, for SSH, and a hard disk in it.
- A Linux PC for the build, with about 100 GB of free disk space. The first build takes a few hours.

## Quick start

1. Build the USB drive image with [kas](https://kas.readthedocs.io):

   ```
   pip install kas
   git clone https://github.com/<you>/meta-playstation2
   kas build meta-playstation2/kas/ps2.yml
   ```

   `kas-container build meta-playstation2/kas/ps2.yml` does the same in a container, if you do not want to install the Yocto host tools.

2. Write the image to the USB drive. Replace `sdX` with the device name of the drive. All data on the drive is lost.

   ```
   zcat build/tmp/deploy/images/ps2/ps2-image-usb-ps2.rootfs.wic.gz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
   ```

3. Connect the drive to the console. In uLaunchELF, start `mass:/VMLINUZ.ELF`.

4. Log in as `root`. The account has no password, also over SSH. Set one with `passwd`.

The login banner lists the commands: `ps2-doom`, `duke3d`, `duke3d-gl`, `quake`, `descent`, the demos `ps2demo-gl`, `ps2demo-gs`, `ps2gl-world` and `ps2gl-cubes`, and `ps2-disk` for the hard disk.

## Game data

The layer builds the game engines and Freedoom. It does not include the data files of the commercial games. Copy them from your own copies or from the shareware releases:

| Game | Files | Folder on the console |
|---|---|---|
| Duke Nukem 3D | `DUKE3D.GRP` | `~/games/duke3d/` |
| Quake | `pak0.pak` (and `pak1.pak`) | `~/games/quake/id1/` |
| Descent | `descent.hog`, `descent.pig` | `~/games/descent/` |
| Doom | `doom1.wad`, `doom.wad` or `doom2.wad` | `~/games/doom/` (Freedoom without one) |

Each folder has a `README.txt` that says what goes in it. Copy the files with `scp -O`, or put them on the FAT partition of the USB drive from a PC: the games look there too.

## More

- **The desktop:** `kas build meta-playstation2/kas/ps2.yml:meta-playstation2/kas/x11.yml` builds the image with Xorg, the matchbox window manager and panel, and a terminal. On the console, `ps2-x` starts it.
- **Linux on the hard disk:** `ps2-disk apa-format` and `ps2-disk install` put the system on the internal disk, in APA partitions next to the ones of the console software.
- **A compiler on the console:** `kas build meta-playstation2/kas/ps2.yml:meta-playstation2/kas/dev.yml` builds `ps2-image-dev`, with GCC, G++, make, autotools, CMake, git, gdb and the headers of every library in the image.
- **A disc image:** `kas build meta-playstation2/kas/ps2.yml --target ps2-iso`.

## Layout

| Directory | Contents |
|---|---|
| `conf/` | The `ps2` machine and the R5900 tunes |
| `recipes-kernel/` | The kernel with its patches, the sound driver, the GS device header |
| `recipes-bsp/` | The IOP modules and the disc image |
| `recipes-core/` | The images, the initramfs `/init`, zram, BusyBox and glibc changes |
| `recipes-graphics/` | The OpenGL library, SDL2, the demos, the X desktop |
| `recipes-games/` | The games and their data folders in `~/games` |
| `recipes-support/` | The hard disk tools, pfsshell, the fast double library |
| `recipes-devtools/` | The toolchain patches for the single-float ABI |
| `classes/` | Build flags of the games, the fast double library, profiling |
| `kas/` | The kas configurations |
| `tools/` | PC-side scripts: release, profiling, the double-precision report |

## Credits

- **Fredrik Noring (frno7)** ported Linux to the PlayStation 2. The layer builds his kernel fork ([frno7/linux](https://github.com/frno7/linux)) and his IOP module framework ([iopmod](https://github.com/frno7/iopmod)).
- **hubix94** wrote the DEV9 and Ethernet (SMAP) support, from pull request 96 of that fork.
- [ps2sdk](https://github.com/ps2dev/ps2sdk) and [pfsshell](https://github.com/ps2homebrew/pfsshell), for the sound module and the hard disk formats.
- [PCSX2](https://pcsx2.net), the reference for the details of VU1 and the GS.
- The games: [doomgeneric](https://github.com/ozkl/doomgeneric), [Freedoom](https://freedoom.github.io), [JFDuke3D](https://www.jonof.id.au/jfduke3d), [QuakeSpasm](https://quakespasm.sourceforge.net), [DXX-Rebirth](https://www.dxx-rebirth.com).
- SDL2, Mesa, Poky and the Yocto Project.
- My friend Bryan and my wife Helen for listening to me babbling about this.

## License

The recipes, classes, scripts, the OpenGL library and the demos are under the MIT license. Each patch has the license of the project that it changes. The games are separate projects under their own licenses, and Descent (D1X-Rebirth) does not allow commercial use. See [`LICENSE`](LICENSE).

This layer contains no Sony code or firmware. The kernel uses the modules in the ROM of your own console at run time.

This project is not affiliated with or endorsed by Sony Interactive Entertainment. "PlayStation" is a registered trademark of Sony Interactive Entertainment Inc.