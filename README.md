# M5StackMDX

M5StackMDX is a simple MDX music player for the ***M5Stack CoreS3*** series.

It plays X68000 MDX music files using YM2151 (OPM) emulation.

## Features

- MDX music playback
- PDX sample data support
- 48 kHz audio output
- Automatic playback of the next file
- Repeat playback after all files have finished

## Requirements

- M5Stack CoreS3
- microSD card
- Visual Studio Code
- PlatformIO IDE extension

## Usage

1. Place the MDX and PDX files in the root directory of the microSD card.
2. Insert the microSD card into the M5Stack CoreS3.
3. Turn on the device. Playback will begin automatically.

After all files have finished playing, playback will restart from the first file.

## Build

1. Clone the repository, including its submodules:
```shell
git clone --recurse-submodules https://github.com/yokomumu1/M5StackMDX.git
```
2. Open the project folder in Visual Studio Code.
3. Install the PlatformIO IDE extension.
4. Build the firmware and upload it to the M5Stack CoreS3.

## License

See the LICENSE file for license information.

Third-party components are subject to their respective licenses.

## Acknowledgements

M5StackMDX uses the following open-source software:
- mdx2wav — Originally developed by mitsuman.
- gamdx — Developed by GORRY.

Special thanks to the original authors for making their excellent work available to the community.

Their contributions have made this project possible.
