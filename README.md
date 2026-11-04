<p align="center">
<img width="640" src="https://github.com/ReRevvedRecomp/rerevved/raw/main/res/rerevved_banner.png" alt="ReRevved banner">
</p>

<p align="center">
Map maker and game file inspector for ReRevved.
</p>

<p align="center">
<a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPL v3"></a>
<a href="https://github.com/ReRevvedRecomp/rerevved-tools/actions/workflows/checks.yml"><img src="https://github.com/ReRevvedRecomp/rerevved-tools/actions/workflows/checks.yml/badge.svg" alt="Checks"></a>
<a href="https://discord.gg/j6VttvdnMD"><img src="https://img.shields.io/badge/Discord-Join-5865F2?logo=discord&amp;logoColor=white" alt="Join Discord"></a>
</p>

## Install

[Download the latest release](https://github.com/ReRevvedRecomp/rerevved-tools/releases),
unzip it, and run `rerevved-tools.exe`. There is no installer.
[Getting started](docs/getting-started.md) covers your first map.

## System requirements

- **Windows x64** with a graphics driver that supports OpenGL 3.3.
- **Linux x64:** build from source with the [development guide](docs/development.md).
- **Game files:** making a map needs none. Inspecting game files needs your own
  legally obtained copy, and playing a map needs
  [ReRevved](https://github.com/ReRevvedRecomp/rerevved).

No minimum RAM is established. File loaders can keep whole files in memory.

## Features

- **Make maps:** draw a 32 x 32 map, set up its scenario, and export a map pack
  the game loads from its maps folder. Terrain images are generated from the
  tiles.
- **Browse archives:** open version 6 FPK archives and extract an entry to a new
  file, never over an existing one.
- **Preview files:** legacy 32 bit RGB/RGBA and DXT1/3/5 DDS images, and up to
  ten seconds of an MP3 waveform.
- **Inspect files:** uncompressed GFX v2 movie metadata without running the movie,
  Gamebryo 20.3.0.9 NIF scenes as static wireframes, and the Xbox DLC 1088 byte
  MAP file as the game draws it.

Tools does not install or launch ReRevved, activate mods, rewrite archives, or
replace game content. [File format support](docs/file-formats.md) lists the exact
limits. A file extension alone does not mean Tools can read the file.

## Documentation

- [Getting started](docs/getting-started.md): install and first run
- [User guide](docs/user-guide.md): draw a map, export a pack, and inspect files
- [Configuration](docs/configuration.md): what is saved and where
- [Troubleshooting](docs/troubleshooting.md): opening, preview, and extraction help
- [Development](docs/development.md): prerequisites, builds, checks, and pull requests
- [Documentation index](docs/README.md): everything else

## Support

Ask for help and report problems on [Discord](https://discord.gg/j6VttvdnMD).

## Acknowledgements

- [Dear ImGui](https://github.com/ocornut/imgui), [GLFW](https://github.com/glfw/glfw),
  [dr_libs](https://github.com/mackron/dr_libs), [stb](https://github.com/nothings/stb),
  [Inter](https://github.com/rsms/inter) and [Lucide](https://github.com/lucide-icons/lucide).
  Their terms are in [Dependency licenses](licenses/README.md).
- [ReXGlue](https://github.com/rexglue/rexglue-sdk) and Xenia authors. Format
  references are recorded in [File format support](docs/file-formats.md).

## License

<a href="LICENSE"><img src="https://upload.wikimedia.org/wikipedia/commons/9/93/GPLv3_Logo.svg" width="88" alt="GPLv3 License"></a>

ReRevved Tools contains no game assets and does not distribute retail game files
or extracted assets. It is an unofficial project and is not affiliated with or
endorsed by Firaxis Games, 2K or Take-Two Interactive. Civilization Revolution
and all other trademarks belong to their owners.
