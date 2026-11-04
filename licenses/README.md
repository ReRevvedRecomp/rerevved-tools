# Dependency licenses

ReRevved Tools uses the following dependencies and vendored adaptations. They
remain under their own licenses. Dependencies are fetched from their upstream
repositories during CMake configuration; the Xenia texture layout adaptations
are vendored in the source tree.

| Dependency | Pinned revision | License |
|---|---|---|
| [Xenia](https://github.com/xenia-project/xenia) texture layout adaptations | `95a5c3ee250f80c3b9d139658649d9ffb6db3eec` | [BSD-3-Clause](Xenia-LICENSE.txt) |
| [Dear ImGui](https://github.com/ocornut/imgui) | `fd13a1e8923a0a7077b404fc36fd063b25a0c0b5` | [MIT](ImGui-LICENSE.txt) |
| [GLFW](https://github.com/glfw/glfw) | `a74efa0d5628b74adc0426af4c5710e287fa7c2c` | [zlib/libpng](GLFW-LICENSE.md) |
| [dr_libs](https://github.com/mackron/dr_libs) | `5690d4671d7ad07ae6021756d7222eb159745f06` | [Public domain or MIT-0](dr_libs-LICENSE.txt) |
| [stb_image and stb_dxt](https://github.com/nothings/stb) | `013ac3beddff3dbffafd5177e7972067cd2b5083` | [MIT or public domain](stb-LICENSE.txt) |
| [Inter](https://github.com/rsms/inter) font | release `v4.0` | [SIL Open Font License 1.1](Inter-OFL.txt) |
| [Lucide](https://github.com/lucide-icons/lucide) icon font | `lucide-static` `0.460.0` | [ISC](Lucide-LICENSE.txt) |

The license files are copied verbatim from those pinned revisions. The Inter
and Lucide fonts are embedded in the executable.
