# File format support

ReRevved Tools identifies known formats by filename extension. It reads at most
16 bytes for the generic header display. Recognizing an extension does not mean
ReRevved Tools can parse that file.

| Format | Current support |
|---|---|
| FPK | Version 6 indexing, selected entry extraction, and embedded document opening from a validated entry |
| DDS | Standard and DX10 metadata parsing. Legacy 32 bit RGB/RGBA and DXT1/3/5 preview with mip levels |
| GFX | Uncompressed GFX v2 movie metadata and external image inspection |
| BIK | Extension recognition and generic file inspection |
| MP3 | Decoded waveform preview limited to ten seconds |
| NIF | Gamebryo 20.3.0.9 scene, geometry, material, texturing, source carrier, assembly, and static wireframe support |
| MAP | Xbox DLC 1088 byte envelope inspection |
| Other regular files | Generic file inspection with an unknown kind |

## FPK version 6

The core parser accepts an explicit range of bytes and returns an entry list.
All integers use little-endian byte order. The 14 byte header contains a 32 bit
version, the `FPK_` signature, two preserved unknown bytes, and a 32 bit entry
count.

Each table record has variable length. It contains a 32 bit byte count, that
many uninterpreted bytes aligned to four bytes, two preserved unknown 32 bit
fields, a 32 bit payload size, and a 32 bit payload offset. A record therefore
occupies `20 + align4(byte_count)` bytes. Payload offsets and sizes use bytes.

The parser rejects truncated tables, arithmetic overflow, records that overlap
the table, and payload ranges outside the archive.

The unknown fields remain uninterpreted, and the variable data is skipped rather
than exposed. The header field order and record alignment are also documented by
the public
[QuickBMS Civilization script](https://mirror.aluigi.org/bms/civilization.bms).

Archive entry inspection recognizes only exact leading DDS, GFX/CFX, NIF, and
ID3 signatures. ID3 is identified only as tagged audio, not as a validated MP3.
The result is a header identification, not a filename or a complete parse.
Entries without one of those signatures remain unknown. Entry length alone does
not identify MAP data, and raw MP3 frames are not inferred.

The core can write one selected entry to an explicit destination chosen by the
caller. It checks the selected byte range again, creates only a new file, and
reports selection, range, destination, open, write, and finalization failures
separately from parse errors. It never modifies the source bytes or overwrites
an existing destination.

The embedded document API can explicitly route one validated selected payload to
the existing DDS, GFX, NIF, MP3, or MAP span parser. The retained archive bytes
and selected range are unchanged, and no temporary retail file is created. A
format mismatch preserves the selected parser's exact malformed, truncated, or
unsupported content reason. Automatic format naming, bulk extraction, archive
rewriting, and repacking are outside this capability. Archive navigation and
extraction usage are documented in the
[User guide](user-guide.md#archive-explorer).

## FPK v6 archive writing

`EncodeFpkArchive` accepts an owning `FpkArchive` of explicit names and opaque
payload bytes. `WriteFpkArchive` writes it to a new destination. Empty archives
and empty payloads are allowed. Names contain 1 to `0x7FFFFFFC` bytes, each in
`0x01` through `0x7F` except slash and backslash. This admits ASCII controls.
Names are not filesystem paths. NUL and non-ASCII bytes are rejected. Names must
be unique after ASCII case folding, and distinct folded names with the same
registered lookup CRC are rejected. That CRC uses reflected polynomial
`0xEDB88320`, initial value `0xFFFFFFFF`, and no final XOR.

Encoding preserves caller record and payload order. It emits little-endian
version 6, `FPK_`, two zero header bytes, and a 32 bit entry count. Each record
contains the counted, nonterminated name, zero padding to `align4(nameLength)`,
two zero metadata words, a 32 bit payload size, and a 32 bit absolute offset.
Record size is `20 + align4(nameLength)`. Payloads follow the table
contiguously without alignment, gaps, trailing bytes, compression, or a footer.
The same model produces identical bytes.

`CalculateFpkArchiveLayout` checks sizes without requiring payload allocation.
Encoding additionally validates name bytes and uniqueness. Entry count, table
end, each payload size and offset, and total archive size must fit unsigned 32
bit values. Vector capacity and stream length limits also apply. These are tool
limits, not established practical guest allocation limits.

File writing validates before creating an exclusive temporary sibling. It reads
back the exact bytes, rejects trailing data, and uses the existing FPK index
parser to verify entry counts, sizes, offsets, and payloads. It checks counted
names separately because the parser skips them. Only verified output is
published with a nonoverwriting hard link. The destination parent must exist and
its volume must support hard links. Existing destinations and occupied temporary
names are preserved. Cleanup attempts to remove only the temporary file created
by the call.

The
[FPK authoring evidence](https://github.com/ReRevvedRecomp/rerevved-research/blob/99d0b9f52ed5fd83759c617ca59a7fee7ee99e10/manifests/fpk-v6-authoring-boundary.json)
(RVA-F-0074, `grammar`, `nameLookup`, `payloadRules`, and `writerBoundary`)
supports this new archive subset. Imported archive mutation, donor repacking,
and nonzero metadata authoring are outside this API. It produces intermediate
offline artifacts and establishes no payload validity, runtime acceptance, or
playability.

## Map bundle transaction

The UI free `WriteMapBundle` API takes an existing parent directory, a new
bundle directory basename, ordered artifact names and bytes with read back
verifiers, and a caller supplied tool version. It creates a unique sibling
staging directory. Each artifact is written to a new file, closed, read back
from disk, compared byte for byte, and passed to its verifier in caller order.
The transaction writes `bundle-manifest.json` last and compares it byte for
byte, then publishes the complete staging directory with one nonreplacing
directory rename. Even an existing empty destination is rejected. Windows and
Linux use their native nonreplacing rename operations. Other platforms fail
publication without replacing a destination. This is a publication transaction,
not a power loss durability guarantee.

Artifact and bundle directory names are separator free ASCII filesystem
basenames of 1 through 255 bytes. Windows reserved names, control bytes,
`<>:"/\|?*`, and trailing dots or spaces are rejected. Artifact names must be
unique after ASCII case folding on every platform. The manifest name is
reserved. Paths are checked for symlinks and Windows reparse points before
filesystem operations. Cleanup attempts to remove only the session's recorded
files and its empty staging directory, without recursion. An unowned entry,
reparse point, or filesystem error can prevent cleanup and produces a cleanup
failure. Existing destination and sibling contents are preserved.

The Tools owned manifest is UTF-8 JSON without a BOM, with LF line endings and
one final newline. It uses this field order and indentation:

```json
{
  "schemaVersion": 1,
  "label": "intermediate - not playable",
  "toolVersion": "0.1.0",
  "artifacts": [
    {"name": "example.bin", "size": 3, "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}
  ]
}
```

The example artifact contains the three ASCII bytes `abc`. Artifact records
retain caller order. `size` counts bytes and `sha256` is a 64 character
lowercase SHA-256 digest of those bytes. The manifest does not list itself. An
empty list is serialized as `"artifacts": []` on one line. The version must be
valid UTF-8. Quotes and backslashes are escaped, and control characters use
lowercase `\u00xx` escapes. `EncodeMapBundleManifest` exposes these
deterministic bytes independently. `MapBundleFileSession` supplies the create
new, read, publish, and owned cleanup operations for application transaction
seams. Callers of those individual operations own verification and manifest
ordering.

The manifest describes offline artifacts only. Publication does not establish
scenario or DDS semantics, installation, activation, game delivery, runtime
acceptance, or playability.

## DDS

The core parser accepts an explicit range of bytes and returns a metadata
record. It supports the standard 128 byte DDS description and the 148 byte form
with a `DDS_HEADER_DXT10` extension. All fields are 32 bit values in
little-endian byte order. The base layout is the `DDS ` signature, a 124 byte
`DDS_HEADER`, and its embedded 32 byte `DDS_PIXELFORMAT`. The parser reads the
DX10 extension only when the pixel format flags include `DDPF_FOURCC` and the
FourCC is `DX10`.

The parser rejects truncated metadata, an invalid signature, and incorrect
header or pixel format structure sizes. It preserves header values, FourCC
bytes, masks, reserved bytes, capabilities, and raw DX10 fields without guessing
their meaning.

The GPU free DDS document decoder previews legacy 2D textures as RGBA8. It
accepts 32 bit `DDPF_RGB` data with optional `DDPF_ALPHAPIXELS`, nonoverlapping
contiguous eight bit RGB masks, and a matching alpha mask when alpha is
declared. RGB data without declared alpha becomes opaque. It also decodes FourCC
`DXT1`, `DXT3`, and `DXT5` through the role neutral Xbox block codec.

The document retains complete mip levels' dimensions, absolute source byte
ranges, and RGBA8 pixels. Its `rgba8` member retains the top level pixels for
existing callers. Additional levels are read only when `DDSD_MIPMAPCOUNT`
(`0x20000`) is set. A zero mip count still means one level. Dimensions halve
with a minimum of one. A count beyond the chain ending at 1x1, a truncated later
level, or an unsupported later packed tail leaves the complete levels available
and records a typed, nonfatal `mipIssue`. Truncated top level data still fails
decoding. Every source range is checked before pixel decoding. The sum of
complete levels' pixel counts must fit 67,108,864 pixels (64 Mpx). An allocation
free check of header dimensions and source ranges returns `PixelLimitExceeded`
before allocating descriptors or pixels.

Linear rows and block rows are tightly packed regardless of the header pitch
hint. Payload layout is never detected. The default is linear little-endian. An
Xbox tiled file read as linear decodes to scrambled pixels. Callers select the
layout explicitly as described in
[Xbox DDS layouts and writing](#xbox-dds-layouts-and-writing).

DX10 formats, other FourCC encodings, other bit counts or masks, cubemaps,
and volume textures remain unsupported for preview. The application viewer
owns OpenGL upload, presentation, and texture lifetime. Preview never modifies
or writes a source file.

The layout and minimum validation follow Microsoft's DDS documentation:

- [DDS programming guide](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide)
- [DDS pixel-format reference](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-pixelformat)
- [DX10 extension reference](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-header-dxt10)

## Texture import and legacy DDS writing

The UI free core imports an explicit byte range as the caller selected PNG or
DDS kind. PNG import accepts 8 bit truecolor and truecolor with alpha, and 1, 2,
4, or 8 bit palette images expanded to RGBA8. Palette and truecolor transparency
chunks retain source alpha availability. RGB input without alpha imports with
opaque pixels and reports missing alpha during logo validation. 16 bit and
grayscale PNGs have distinct rejection reasons. Invalid chunk ranges, CRCs, zlib
checksums, truncated data, unsupported color layouts, and trailing bytes are
rejected. Encoded input and inflated PNG scanlines must each fit a signed 32 bit
decoder length. PNG dimensions are limited to 67,108,864 pixels (64 Mpx),
checked from IHDR before decoding. Larger images return `PngPixelLimitExceeded`
with the exact pixel limit in the message. Before allocating scanlines, import
rejects an expected inflated size greater than
`1032 * compressed IDAT bytes + 1024` as `InvalidPng`. Source color type and
bit depth remain available in the imported image. Allocation failures at the PNG
or DDS import boundary return `AllocationFailed` with an insufficient memory
message.

DDS import reuses the metadata parser and document decoder described above. A
parsed header retains dimensions, mip count, and format metadata even when
decoding fails or multiple mip levels are declared. Decode failures retain the
exact DDS parser error and no pixels. Header failures return the exact metadata
parser error. Decodable inputs retain top level RGBA8 pixels. Validation returns
every width, height, mip count, encoding, source alpha, and RGBA storage
mismatch with its expected and actual value. A declared mip count of zero means
one level. DDS source encoding must match the target's legacy A8R8G8B8 layout.
Import and validation never resample, crop, or pad, and writing requires decoded
pixels and a fully matching contract.

The writer supports only `file-data/GFX_MainMenu_logo.dds`: 1024 by 256 pixels,
an alpha channel, legacy uncompressed A8R8G8B8, and no mipmaps. Output is
exactly 1,048,704 bytes: a 128 byte legacy DDS description followed by tightly
packed BGRA pixels. It uses header size 124, zero depth and mip count, pixel
format size 32, flags `0x41`, zero FourCC, 32 bits per pixel, and masks
`0x00ff0000`, `0x0000ff00`, `0x000000ff`, and `0xff000000` for R, G, B, and
A. Caps declare only a texture. Complex, mipmap, and secondary caps are absent.
This matches the main menu logo validator in rerevved. It establishes no other
runtime key.

File writing creates a temporary sibling, reads it back and compares every
encoded byte, then parses it through the DDS parser and compares every RGBA8
byte with the imported image. Only a verified temporary file is published with a
nonoverwriting hard link. Existing destinations are refused. Failures remove
only the temporary file. A filesystem without hard link support reports
`FinalizationFailed` with a message that the destination volume must support
hard links. These core operations do not install, activate, or load an override
in the game.

PNG decoding uses
[stb_image v2.30](https://github.com/nothings/stb/tree/013ac3beddff3dbffafd5177e7972067cd2b5083),
compiled for in memory PNG decoding only, under its
[MIT or public domain terms](../licenses/stb-LICENSE.txt). DXT encoding uses
stb_dxt from the same revision and terms, except that a DXT1 block with
transparent texels uses a simple bounding box fit.

## Xbox DDS layouts and writing

The UI free `EncodeXboxDds` and `WriteXboxDds` APIs accept explicit RGBA8 mip
levels and an `XboxDdsSpec` naming A8R8G8B8, DXT1, DXT3, or DXT5 and a payload
layout. Level zero comes first. Subsequent dimensions are the exact halving of
the previous level with a minimum of one. The writer requires the exact pixel
byte count and never resamples. `GenerateXboxMipLevels` is an explicit opt-in
helper for power of two images. Its count includes the source level. Each
additional level uses a 2x2 box average independently per RGBA channel, with
round half up. A one pixel axis replicates its edge. Channels are averaged
without color space conversion or alpha weighting.

`LinearLittleEndian` is the parser and writer default. Levels follow one another
without padding after the 128 byte header. A8R8G8B8 uses B,G,R,A bytes. DXT uses
little-endian BC blocks, including complete blocks for partial edges. This
layout supports nonsquare levels, levels whose dimensions are not powers of two,
and 1x1 levels.

`XboxTiledBigEndian` requires power of two dimensions. For each declared level
before the packed tail, storage begins at `128 + regionOffset` and occupies
`tiledSize` bytes from the codec's `XboxMipChain`. Only declared regions are
emitted. Padding is zero. Writing encodes blocks, calls `TileSurface`, then
`SwapEndian`. Reading swaps each padded region before `UntileSurface` and pixel
decoding. ARGB swaps bytes within 32 bit words. DXT swaps bytes within 16 bit
units. The header stays little-endian. The DDS header does not identify this
payload layout, so the caller must select it. The parser never detects or
guesses it from bytes.

Writing any declared level at or beyond `packedTailBaseLevel` returns
`PackedMipTailUnsupported`. Reading rejects a base level already in the tail. A
later tail sets `mipIssue` and preserves the complete preceding levels. Those
levels share a region and require the tail base pitch and packed block origins
rather than separately tiled surfaces. Their container placement is unresolved
in this API. This restriction keeps the standalone level placement consistent
with the codec without guessing a packed tail representation. Unsupported
formats and layouts, invalid level dimensions or storage, size overflow, and
tiled inputs whose dimensions are not powers of two also have typed errors.

Output uses a standard 128 byte DDS description: signature `DDS `, header size
124, pixel format size 32, zero depth, secondary caps, and reserved fields.
A8R8G8B8 uses flags `0x100f`, row pitch `width * 4`, pixel format flags `0x41`,
32 bits per pixel, zero FourCC, and R/G/B/A masks `0x00ff0000`, `0x0000ff00`,
`0x000000ff`, and `0xff000000`. DXT uses flags `0x81007`, top level linear
block size, pixel format flags `0x4`, the selected FourCC, and zero bit count
and masks. Multiple levels add `DDSD_MIPMAPCOUNT` (`0x20000`), the level count,
and `DDSCAPS_MIPMAP | DDSCAPS_COMPLEX` (`0x400008`) to `DDSCAPS_TEXTURE`
(`0x1000`). A single level has zero mip count and neither added capability.

File writing shares the legacy DDS writer's exclusive temporary sibling,
complete byte read back, parser proof, and nonoverwriting hard link publication.
Parser read back uses the selected layout and compares every level's dimensions
and pixels: exact source RGBA8 for ARGB, and the codec's
`DecodeBlocks(EncodeBlocks(level))` result for DXT. Failed proofs are not
published. The destination parent must exist and its volume must support hard
links. Publication failures retain typed diagnostics and attempt to remove only
the owned temporary sibling.

These APIs are role neutral. They establish no CivRev role's dimensions, format,
mip policy, byte order, tiling, channel meaning, or runtime acceptance. The main
menu logo import contract still requires legacy A8R8G8B8. DXT preview support
does not satisfy its encoding requirement.

## Named map heights DDS

`EncodeHeightsDds`, `ParseHeightsDds`, and `WriteHeightsDds` handle the
`Map%s_Heights.dds` role in one fixed layout: the layout shared by every shipped
numbered map heights file (`Map%d_Heights.dds`). The input is exactly 512 x 512
unsigned 16 bit samples, row major in file row order. Output is 524,416 bytes: a
fixed 128 byte header followed by one tight level of little-endian 16 bit
samples.

The header has signature `DDS `, header size 124, flags `0x1007`, height and
width 512, zero pitch, depth, and mip count, pixel format size 32, pixel format
flags `0x20000` (luminance), zero FourCC, 16 bits per sample, R mask `0xffff`,
zero G/B/A masks, caps `0x1000`, and zero for every other field. The parser
accepts only those exact 128 header bytes and the exact total length, so the
writer and parser fail closed for any other size, format, or mip declaration.

File writing uses the shared exclusive temporary sibling, complete byte read
back, and nonoverwriting hard link publication. The parser proof compares every
read back sample with the input. `HeightsBundleWriter` adapts the same encoder
and sample exact read back to a map bundle payload.

This layout matches every shipped file. It is not a proved loader requirement.
The title's sample byte order transfer, the mapping from samples to map tiles
and world positions, the vertical scale, and runtime acceptance remain unproved.
A [map pack](#map-pack-output) carrying this layout loaded and played in game.

## Named map blends and lightmap DDS

`EncodeTerrainDds`, `ParseTerrainDds`, and `WriteTerrainDds` handle the
`Map%s_MountainHill_Blends.dds` and `Map%s_Lightmap.dds` roles. Every shipped
numbered map file of each role is one linear DXT1 level: blends are 2048 x 2048
(2,097,280 bytes) and lightmaps 4096 x 4096 (8,388,736 bytes), each a 128 byte
header followed by little-endian DXT1 blocks.

The header has signature `DDS `, header size 124, flags `0x1007`, height and
width equal to the side, zero pitch, depth, and mip count, pixel format size 32,
pixel format flags `0x4` (FourCC), FourCC `DXT1`, caps `0x1000`, and zero for
every other field. Every shipped blends file uses this header, as do most
shipped lightmaps. A few lightmaps also declare the linear size (flags
`0x81007`, pitch `0x800000`), which the writer does not produce. The parser
accepts only the writer's exact 128 header bytes and the role's exact length.

The encoder takes an RGBA image of the role's exact side, forces every alpha to
255 so no block uses DXT1 punch through, and encodes with the shared role
neutral DXT1 encoder. File writing uses the shared nonoverwriting publication.
The parser proof compares the read back blocks with the encoded blocks.

The Map page encodes the generated blends and lightmap this way (see
[Generated named map terrain](#generated-named-map-terrain)). A
[map pack](#map-pack-output) carrying both roles loaded and played in game.

## Generated named map terrain

`GenerateTerrainFromTiles` builds the heights samples and the blends and
lightmap images from a 1024 byte tile core. Image tile (row i, column j) covers
file tile (row j, column i), the transpose observed in the game's DLC maps. Each
tile's low nibble selects its kind: 0 sea, 4 hill, 6 mountain, 7 the polar code,
and every other value flat land.

| Kind | Height | Blends | Lightmap level |
|---|---|---|---|
| Sea | 20500 next to land, 11000 two tiles out, 1200 beyond | black | 175 / 174 / 188 |
| Flat land | 29200 | black | 207 / 206 / 217 |
| Hill | 33000 plus a peak of about 5000 | red disc | 148 / 148 / 165 |
| Mountain | 39500 plus a ridge that rises with depth into the range | green rim, blue deep inside a range | 92 / 92 / 120 |
| Polar | 30000 | black | 133 / 132 / 151 |

Sea distance is the Chebyshev distance to the nearest land tile. Per tile levels
are smoothed across tile borders. Each hill tile places its peak and its red
disc at one centre jittered within the tile. Mountain tiles merge into ranges
with a ragged edge. Their height rises with the distance inside the range, so
each range has a ridge along its middle, and ridged noise adds crags. Value
noise scaled by kind roughens all heights, and heights above 54000 are
compressed smoothly toward 65000. Blends are drawn at full size with hard, noisy
mask edges, and blue replaces green only deep inside a range, so channels never
mix in one pixel, as in the shipped numbered maps. The lightmap multiplies its
levels by relief shading lit from the image's top left, computed on the 512 grid
and bilinearly upscaled. Noise is seeded from the core bytes, so the same grid
always produces the same images. Levels follow the per kind averages of the
shipped DLC maps, and mountain and hill mask coverage and relief follow the
shipped numbered maps. They are not the game's own terrain construction.

## Asset pack output

The UI free core `WriteAssetPack` API writes a complete override pack under an
existing caller selected destination root:

```text
<destination>/<id>/
  asset-pack.toml
  assets/<key path>
```

The manifest contains integer `manifest_version = 1` and an `[asset_pack]` table
with `id`, `name`, and `version`. Optional `author` and `description` values
are strings. IDs contain lowercase ASCII letters and digits separated by single
hyphens, with no leading or trailing hyphen and a maximum of 63 characters. The
ID matches the output directory. Names contain at least one nonwhitespace
character. Versions contain exactly two nonempty ASCII decimal components,
`major.minor`. Leading zeros are allowed. There is no format level payload size
or name length limit.

A map pack's manifest adds display metadata that names its scenario file slot
and each scenario's ID and name. The pack's `dlc.fpk` stays authoritative:

```toml
[map_pack]
slot = 7

[[map_pack.scenarios]]
id = 105
name = "Island Duel"
```

`slot` and each `id` are decimal integers and each `name` is a string.

Keys are relative forward slash paths inside `assets/`. Absolute paths,
traversal, backslashes, empty or dot segments, embedded NUL, and drive like
roots are refused. On every platform the writer also refuses colons, components
ending in a dot or space, reserved Windows device names such as `CON`, `COM0`,
`CONIN$` or `NUL .txt`, the characters `< > " | ? *`, and control characters.
Keys and metadata use UTF-8. The writer emits deterministic ASCII TOML using
escaped Unicode scalars. At least one asset file is required. Zero byte assets
are allowed. Duplicate keys compare exact case. Case distinct names can fail
creation on a case insensitive filesystem.

The writer validates the description before writing and refuses an existing
`<destination>/<id>`. It writes assets into a unique staging holder inside the
destination root, writes the manifest last, validates the staged pack, and reads
every asset back for exact byte comparison. A no replace rename publishes the
completed pack. Failure cleanup targets only the newly created staging holder.
An OS cleanup failure is reported explicitly and can leave that holder. A
published pack is never removed during cleanup.

`ValidateAssetPack` returns all discovered violations and warnings with paths
and field names. It checks manifest fields, directory identity, regular files,
links/reparse points, allowed runtime paths, and nonempty assets. The only files
allowed outside `assets/` are `asset-pack.toml`, `icon.png`, `README.md`, and
root filenames beginning with `LICENSE`. Unknown manifest fields are warnings.
The standalone reader supports single line basic/literal strings, bare keys,
simple tables, the `[[map_pack.scenarios]]` array of tables, comments, and
scalar schema values. Other TOML forms report unsupported syntax. It is not a
general TOML validator. It does not read or write ZIP archives.

Pack validation establishes filesystem and manifest structure. Asset bytes are
opaque: it does not prove that the title consumes a key or accepts an asset's
encoding. Installation, selection, activation, and runtime acceptance remain
outside this API.

## Map pack output

A map pack is an [asset pack](#asset-pack-output) whose only asset is
`assets/map-pack/dlc.fpk`. `AssembleMapPack` builds that FPK v6 archive from
one or more map drafts with these entries, in order:

| Entry | Content |
|---|---|
| `DLCScenarioData<slot>.xml` | Canonical scenario XML with every map's entries. Slot 5 to 31 |
| `<MAP>.map` | The 1024 byte grid followed by 64 bytes of 0xFF |
| `Map<MAP>_Heights.dds` | Heights DDS |
| `Map<MAP>_MountainHill_Blends.dds` | Blends DXT1 DDS |
| `Map<MAP>_Lightmap.dds` | Lightmap DXT1 DDS |

The four map entries repeat for each map. Each map has its own `MAP` name, and
every scenario entry uses an ID of 100 or more that no other entry in the pack
uses. The game's own scenario files use slots 0 to 4 and lower IDs. Assembly
refuses an archive over ReRevved's 256 MiB limit. The pack ID is at most
39 characters. Each payload is parsed back before packing, and the archive is
parsed back entry by entry. `ExportMapPack` writes the pack without replacement,
validates it and compares `dlc.fpk` byte for byte. The receipt records its size
and SHA-256.

ReRevved registers each enabled map pack before the game starts and refuses
one whose archive entry names, `MAP` names or scenario IDs repeat, within the
pack or against an earlier enabled pack. A single map pack with a one letter
name loaded, listed in Choose Scenario and played several turns in game.

## Xbox DLC map envelope

The ReRevved Tools parser accepts exactly 1088 bytes and preserves them as two
separate ranges: a 1024 byte map core and a 64 byte footer. Shorter inputs
report a truncated record. Longer inputs report an unsupported length. The
parser does not modify the source or assign a semantic purpose to the footer. A
non-`0xFF` footer remains structurally inspectable. See the
[User guide](user-guide.md#xbox-dlc-map) for grid and hover behavior.

The UI free edit model exposes static version 1.3 facts from the
[tile-semantics evidence](https://github.com/ReRevvedRecomp/rerevved-research/blob/76ba2694527a5df65cc7b8065d8af9c99b6d4464/manifests/xbox-map-tile-semantics-boundary.json).
`MapNativeCell` converts zero based file tile `(row, column)` at byte offset
`32 * row + column` to native `(x, y) = (row, 31 - column)` and native cell
index `i = 40 * x + y`. Both file coordinates must be in 0 through 31. This
fixes the native stride 40 mapping. An in game run of an asymmetric probe map
showed the game drawing file rows left to right and file columns top to bottom,
so north is file column 0 (native `y = 31`). The editor grid, minimap and
preview use that transposed view and `MapTileAtPoint` maps screen x to file row.

`MapTerrainName` returns only the six canonical names in the
[Research terrain discriminator](https://github.com/ReRevvedRecomp/rerevved-research/blob/76ba2694527a5df65cc7b8065d8af9c99b6d4464/manifests/unique-era-ability-effect-ownership.json):
0 sea, 2 plains, 3 forest, 4 hill, 5 desert and 6 mountain
(`/terrainYieldBoundary/terrainDiscriminator`). Values 1 and 7 through 15 have
no Research name here. `MapTerrainInferredName` labels code 1 grassland and code
7 polar ice from retail DLC map usage: across 30 retail map sets only codes 0-7
occur, 122 of 150 start markers sit on code 1, and code 7 fills file columns 0-1
and 30-31 next to sea only. These labels are inferred, not Research names. The
editor does not warn on them. The loader stores all low nibble values without a
local range check or clamp. That admission does not prove authoring validity,
safe table membership or final retained terrain.

`DescribeMapTileBits` exposes all four high bit descriptions and their set/clear
states. The writes below apply when the corresponding file bit is set. The
[corrected loader encoding](https://github.com/ReRevvedRecomp/rerevved-research/blob/76ba2694527a5df65cc7b8065d8af9c99b6d4464/manifests/map-authoring-loader-boundary.json)
and tile semantics evidence distinguish the native halfword flag plane, byte
edge plane and coordinate arrays:

- `0x10` sets halfword flag `0x400` and clears halfword flag `0x80` on this
  cell. Nonzero terrain also sets halfword flag `0x10`, increments a loader
  counter and advances linear congruential state. Terrain 0 skips that
  additional write. A selected artifact presence consumer tests `0x400`, and
  selected consumption conditionally clears it. A selected resource callback's
  negative resource arm requires halfword `0x10` set and `0x400` clear. The
  loader's nonzero terrain `0x10` write coexists with `0x400`, which suppresses
  that arm. The loader written state alone does not trigger it
  (`/highBits/halfwordFlags/0/qualification`). The counter and state effects and
  complete consumers remain unresolved. This gives no universal meaning to file
  bit `0x10` (`/highBits/halfwordFlags/0`, `/dependencies/4/use`).
- `0x20` appends `(31 - column, row)` to two eight entry coordinate arrays for
  the first eight markers in file order, without deduplication or a terrain
  predicate. Later markers are ignored. This establishes a starting coordinate
  storage seam, with player assignment, successful placement and unmarked
  entries unresolved. It is not halfword flag `0x20`
  (`/highBits/coordinateMarkers`).
- `0x40` ORs byte edge flag `0x2` on cell `i` and `0x8` on cell `i + 40`,
  corresponding to file tile `(row + 1, column)`.
- `0x80` ORs byte edge flag `0x4` on cell `i` and `0x1` on cell `i - 1`,
  corresponding to file tile `(row, column + 1)`. Both directional pairs have
  no local clipping or wrapping. Edge geometry and safe combinations remain
  unresolved (`/highBits/edgePairs`).

`MapDraftWarnings` returns advisory warnings with typed kinds and tile offsets:
`0x40` on file row 31 writes into native padding row 32. `0x80` on file column
31 crosses the native row boundary at `y = 0`, including `E[-1]` at native
`x = 0`. Each marker after the first eight in file order receives an ignored
marker warning. Terrain 0 with file `0x10` receives the distinct handling
warning, and every terrain code with neither a Research nor an inferred name
receives a missing name warning. `MapStartMarkerAdvice` separately reports a
draft with fewer than five `0x20` markers, the count in every retail map set.
Warnings neither block edits nor alter bytes, and their absence does not prove
safe or successful authored map loading. `FormatMapTileBits` retains its raw bit
breakdown.

The loader requests 1024 bytes without a returned count equality check and has
no direct footer read or rejection in this owner. Conditional stack buffer
lookahead can reach index 1056. This is not delivery of footer bytes
(`/lengthBoundary`). Footer meaning, required footer presence, validity of a
bare core, safe acceptance of trailing bytes and deterministic short file
behavior remain unproved. These static facts do not establish runtime loading or
a safe general writer contract. Tests use generated synthetic records and
contain no retail bytes.

## Scenario XML writing

`EncodeScenarioXml` accepts a `ScenarioDocument` of ordered entries.
`WriteScenarioXml` writes it to a new destination. Each entry contains `MAP`
(default `NONE`), boolean `bMultiplayer` (default false), signed 32 bit `ID`
(default -1), `TITLE` and `DESCRIPTION` literals in ENG/FRA/ITA/DEU/ESP order,
and 64 signed 16 bit variator slots. The 35 case exact names and defaults are
returned by `ScenarioVariators`. `DefaultScenarioVariators` initializes all
slots. Unnamed slots must remain zero. Duplicate IDs, including repeated -1, are
rejected. An empty document and one default entry are allowed. Signed storage
ranges do not establish safe gameplay ranges.

`MAP` is `NONE` or a name of up to 20 ASCII letters, digits and underscores
that starts with a letter, preserving case. Retail packs use names this long,
such as `Mountains_of_Madness`. `NONE` produces no named map handoff, and no
other casing of it is a name. A name derives `<MAP>.map`, `Map<MAP>_Heights.dds`,
`Map<MAP>_MountainHill_Blends.dds`, and `Map<MAP>_Lightmap.dds`. Nonempty literals
use printable ASCII `0x20` through `0x7E`, excluding `&`, `<`, `>`, leading
or trailing spaces, and repeated spaces. Empty strings omit their locale
children. An entirely empty literal group is omitted.

Encoding preserves entry order and deterministically emits root `S`, explicit
`MAP`, `bMultiplayer`, and `ID`, then literal groups in the order above and
nondefault named variators in slot order. Output uses LF newlines and canonical
signed decimals. Multiplayer is `0` or `1`.

`ParseScenarioDocument` is a strict subset reader, not a general retail XML
reader. It accepts an arbitrary bare ASCII XML root with direct `EntryInfo`
children and supplies defaults for omitted fields. The first scalar, literal
group, locale child, and `specs` child is retained. Later duplicates must still
validate. Repeated `VARIATOR` elements apply in order. Each requires a `specs`
child with `text` and `level` attributes. Unknown fields, locales, attributes,
and variator names are rejected. `EntryType`, `EntryTag`, and sibling
`MAPNUMBER`, `STARTLOCME`, and `STARTLOC0` through `STARTLOC4` are excluded.
`MAPNUMBER`, `STARTLOCME`, and `STARTLOC0` through `STARTLOC4` remain valid
variator names. Empty present scalars and out of range integers are rejected, as
are plus signs, leading zeros, negative zero, and trailing text in decimals.
Entities, declarations, comments, DTD, CDATA, namespaces, mixed content, and
non-ASCII XML are rejected.

Tool limits are 1 MiB XML, 4096 entries, and 4096 bytes per locale literal. The
reader also limits depth to four, elements to 65,536, and attributes per element
to two. These are independent of game limits. File writing validates before
creating an exclusive temporary sibling, reads back and compares every byte,
rejects trailing data, and parses and compares the entire model before
publishing with a nonoverwriting hard link. The destination parent must exist
and its volume must support hard links. Existing destinations and occupied
temporary names are preserved. Cleanup attempts to remove only the temporary
file created by the call.

The
[scenario schema evidence](https://github.com/ReRevvedRecomp/rerevved-research/blob/99d0b9f52ed5fd83759c617ca59a7fee7ee99e10/manifests/scenario-schema-boundary.json)
(RVA-F-0075, `record`, `variators`, `localization`, and `mapName`) supports
this closed representation. This writer produces intermediate offline artifacts
and establishes no runtime acceptance or playability.

## GFX

The core parser accepts an explicit range of bytes containing an uncompressed
`GFX` movie. It requires the 32 bit declared file length to match the supplied
range exactly. The parser preserves the file version, signed frame rectangle
coordinates in twips, raw 8.8 frame rate, and frame count. The application shows
both the encoded rectangle and its derived pixel dimensions.

The first tag must be Scaleform ExporterInfo tag 1000 with an exporter version
whose major byte is 2. The parser preserves the raw exporter version, flags,
bitmap format, prefix bytes, and SWF name bytes. It then walks standard short
and long SWF tag records, checks every payload against the declared file
boundary, and skips tag codes it does not inspect. A zero length End tag must
finish exactly at the file boundary.

Scaleform DefineExternalImage tag 1001 records preserve the character ID, raw
bitmap format, target dimensions, export name bytes, and filename bytes. The
records remain in source order, including duplicates. Length prefixed names
remain byte strings because the available layout evidence does not establish a
text encoding or runtime path resolution rule. Presentation escapes bytes that
cannot be shown safely.

`CFX` compression, other signatures, non-v2 exporter records, truncated fields,
inconsistent lengths, malformed tag streams, and data after the End tag report
clean failures. Decompression, execution, rendering, export, and writing are
outside the GFX document contract. Inspection behavior is documented in the
[User guide](user-guide.md#gfx).

The public format references are:

- [Adobe SWF file format specification](https://open-flash.github.io/mirrors/swf-spec-19.pdf)
  for the movie header, bit packed rectangle, frame fields, and tag framing.
- [GFxExport documentation](https://help.autodesk.com/cloudhelp/ENU/Scaleform-Help/scaleform_help/gfxexport_reference/usage.html)
  for external image export behavior.

The proprietary tag numbers, field layouts, and exporter major version
requirement come from these pinned Scaleform GFx v2 sources:

- [ExporterInfo](https://github.com/sigmaco/scaleform-gfx-v2/blob/ac3bd279ad368be80133c858ae8b34355c86839d/Src/GFxPlayer/GFxLoaderImpl.cpp)
- [Tag identifiers](https://github.com/sigmaco/scaleform-gfx-v2/blob/ac3bd279ad368be80133c858ae8b34355c86839d/Src/GFxPlayer/GFxTags.h)
- [DefineExternalImage](https://github.com/sigmaco/scaleform-gfx-v2/blob/ac3bd279ad368be80133c858ae8b34355c86839d/Src/GFxPlayer/GFxTagLoaders.cpp)

Only those narrow format facts were used. No Scaleform source code, dependency,
or asset was copied into ReRevved Tools.

## MP3

The core passes an explicit byte range to
[dr_mp3 0.7.3](https://github.com/mackron/dr_libs/tree/5690d4671d7ad07ae6021756d7222eb159745f06)
and returns the decoded sample rate, channel count, and a waveform window. The
core first validates that the complete audio region is a sequence of
structurally valid MP3 frames. It then decodes PCM only for at most the first
ten seconds, retaining 100 signed peak points per second. This bounds PCM
decoding and retained waveform data while leaving the source bytes unchanged.

The document API produces the waveform without an audio device or GPU resource.
Invalid, truncated, or unsupported streams report a clean decode failure.
Playback, transcoding, export, and writing are outside the MP3 contract.
Presentation is documented in the [User guide](user-guide.md#mp3). Tests use
only an embedded stream generated from a synthetic sine source. No retail audio,
capture, or decoder output is included.

The decoder is fetched at the exact pinned revision above under its
[public domain or MIT-0 terms](../licenses/dr_libs-LICENSE.txt). ReRevved Tools
uses the dependency through its public API and does not copy codec
implementation into the document layer.

## NIF

### Container

The core accepts an explicit byte range containing the supported Gamebryo NIF
variant. It requires matching textual and binary version 20.3.0.9, big-endian
type 0, and user version 0. The binary version, user version, and block count
are stored little-endian. Subsequent standard numeric header and footer fields
follow the declared big-endian byte order.

The parser preserves exact block type and string bytes, block type indices and
sizes, group values, and footer root references. It derives each block offset
from the validated header boundary and preceding block sizes. Every count,
length prefixed byte string, type index, block range, and root reference is
checked against the supplied range, and the footer must end exactly at the file
boundary. Unsafe bytes are escaped only for application display.

The standard field order and mixed-endian types follow these sources from the
pinned NifTools revision:

- [Header schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1963-L1982)
- [Footer schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1732-L1736)
- [20.3.0.9 user-version profile](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L220)
- [Endian values](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1228-L1231)

This support is limited to the documented Gamebryo 20.3.0.9 variant. Other title
NIF variants are outside the documented contract.

### Scene objects

For exact `NiNode` and `NiTriShape` block types, the core also reads the
standard 20.3.0.9 `NiObjectNET` and `NiAVObject` fields: string table name
index, extra data and controller references, flags, translation, rotation,
scale, property references, and collision object reference. Nodes preserve
ordered child and effect references. Triangle shapes preserve their geometry
data and skin instance references and the complete standard material name,
material extra data, active material, and update fields. Reference and string
indices must be null or remain inside the corresponding table, array counts must
fit inside the declared block, and each selected block layout must consume its
declared size exactly. Float values remain observable without geometry validity
rules.

### Material property

For exact `NiMaterialProperty` blocks, the core first validates the inherited
`NiObjectNET` name, extra data references, and controller reference. It then
retains the unconditional 56 byte derived payload as ambient, diffuse, specular,
and emissive colors of three ordered f32 components each, followed by one
glossiness f32 and one alpha f32. Inventories preserve source block index and
block order. The selected layout has no obsolete Flags, Emissive Mult, presence
value, or version branch, and must consume its declared block exactly. The
parser preserves each decoded f32 bit pattern without arithmetic, clamping,
normalization, replacement, color conversion, finite value policy, or range
policy. Research manifest `RVA-F-0129` documents this title specific retention
contract. It does not establish color space, lighting, blending, shading,
texture interaction, or visual equivalence.

The standard type and profile conditions are corroborated by:

- [NiMaterialProperty schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3978-L3988)
- [Color3 definition](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1583-L1588)

### Texturing property and source texture

For exact `NiTexturingProperty` blocks, the core validates the inherited
`NiObjectNET` payload before retaining the raw u16 Flags and requiring the
supported Texture Count of nine. The nine standard slots remain in the fixed
Base, Dark, Detail, Gloss, Glow, Bump, Normal, Parallax, and Decal 0 order.
Every raw presence byte is retained. A descriptor body follows only when that
byte equals one. Each retained descriptor contains its validated null or in
range Source block reference, raw u16 Flags, raw Has Texture Transform byte, and
the optional transform selected only when that byte equals one. A transform
retains two translation f32 values, two scale f32 values, rotation f32, raw u32
Transform Method, and two center f32 values in serialized order. Present Bump
and Parallax slots additionally retain their unconditional six f32 and one f32
tails. The raw shader count controls an ordered shader record sequence. Each
record retains its raw Has Map byte and includes a descriptor plus raw u32 Map
ID only when that byte equals one.

For exact `NiSourceTexture` blocks, the same inherited validation precedes one
packed 24 byte derived payload: raw Use External byte, validated File Name
string index, validated Pixel Data block reference, raw Pixel Layout, Use
Mipmaps, and Alpha Format u32 values, then raw Is Static, Direct Render, and
Persist Render Data bytes. Both source carriers occupy those positions for
either Use External value. A retained source is classified as the supported
external relationship only when Use External equals one, File Name is valid and
nonnull, and Pixel Data is the standard null reference. Other structurally valid
combinations remain retained but unsupported. A nonnull Pixel Data target stays
opaque.

Both inventories preserve source block order, raw bytes, component order, and
f32 bit patterns without the title's Flags mask, regenerated presence values,
discarded Map ID, normalization, clamping, enum policy, or finite value policy.
Counts must fit the remaining declared block before allocation, every reference
and string index must be null or in range, and each selected block must be
consumed exactly.

Research manifests `RVA-F-0130` and `RVA-F-0131` document these title specific
retention contracts. They do not establish external path lookup, embedded pixel
decoding, DDS encoding, UV behavior, sampling, filtering, wrapping, color space,
shader or material behavior, rendering, editing, conversion, export, or writing.

These inherited layouts follow the pinned NifTools revision:

- [NiFixedString definition](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L352-L354)
- [NiObjectNET schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3359-L3367)
- [NiAVObject schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3440-L3494)
- [NiNode schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L4384-L4389)
- [MaterialData and NiGeometry schemas](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3840-L3869)
- [NiTriBasedGeom schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3872-L3874)
- [NiTriShape schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L5278-L5280)

### Geometry data

For exact `NiTriShapeData` blocks, the core preserves the existing scalar
geometry inventory: group ID, vertex count, raw keep, compression, data,
consistency, and presence flags, bounding sphere, additional data reference,
triangle and triangle point counts, and match group count. It also retains
ordered three float vertex positions, ordered three float normal vectors,
ordered three u16 triangle selectors, and every nested raw u16 normal sharing
group. Absent conditional arrays produce empty containers.

The low six data flag bits derive the UV set count, while bit `0x1000` and the
raw normals flag determine whether tangent and bitangent arrays are present.
Those two arrays, vertex colors, and UV coordinates remain bounds checked skips
outside the retained API. One byte presence fields retain their raw values and
use nonzero truthiness. Every retained or skipped array and nested group must
fit inside the declared block before allocation or reading, and the selected
layout must consume the block exactly.

The parser does not impose rules on group IDs, float values, bounds, flag
values, triangle point arithmetic, topology, selectors, winding, or UV ranges.
In particular, triangle and normal sharing selectors retain their source order
and raw values without a vertex count range check. Safe selector use remains a
later model renderability boundary. ReRevved Tools does not generate, normalize,
repair, deduplicate, reorder, or reinterpret the retained geometry.

The inherited layout, flag widths, fixed element widths, and conditional arrays
follow these sources from the pinned NifTools revision:

- [NiGeometryData and NiTriBasedGeomData schemas](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3876-L3925)
- [NiTriShapeData schema](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L5282-L5289)
- [Data-flag layout](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1629-L1633)
- [One-byte boolean rule](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L296-L298)
- [Divinity-II user-version condition](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L30-L32)
- [Extra geometry fields](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L3897-L3898)

The accepted user version is zero, so the extra geometry fields do not apply.

The pinned schema describes
[MatchGroup values as vertex indices](https://github.com/niftools/nifxml/blob/970a6238218a106daaeb89a61bcda0eeaf9d08c4/nif.xml#L1745-L1749),
but title consumer evidence
([RVA-F-0122](https://github.com/ReRevvedRecomp/rerevved-research/blob/76ba2694527a5df65cc7b8065d8af9c99b6d4464/manifests/nif-trishapedata.json))
establishes that each nested list selects normal records that receive a shared
normalized contribution. It does not establish that group members finish with
equal normals. ReRevved Tools retains these ordered normal sharing groups
without applying a vertex count range rule. Model assembly does not consume or
range check their selectors. The parser still imposes no float, bounding sphere,
topology, winding, duplicate selector, color, normal, or UV validity rules.

### Model assembly and presentation

Block payloads not covered above are skipped without interpreting animation,
skinning, or unsupported scene semantics. Model assembly walks validated footer
roots and ordered, nested `NiNode` children. Exact `NiTriShape` blocks become
mesh descriptors only when their data reference resolves to parsed
`NiTriShapeData`, positions and triangles are present and nonempty, every
triangle selector addresses a retained position, and any present normal array
has the position count. Null references and valid unsupported child types are
skipped. Active path node cycles, wrong type data references, inconsistent
retained geometry, out of range triangle selectors, and scenes with no supported
meshes are explicit assembly errors. They do not make structural parsing reject
the document.

Each assembled mesh records its footer root index, ordered node path, parsed
shape and data indices, and directly referenced `NiMaterialProperty` block
indices in source order. It also records one double precision world transform
composed from the shape local followed by its node path from immediate parent to
root. The retained f32 translation, column major 3 by 3 rotation, and uniform
scale remain unchanged in `NifDocument`. Composition follows
`s_world = s_parent * s_local`, `R_world = R_parent * R_local`, and
`t_world = t_parent + s_parent * (R_parent * t_local)`. Applying a transform is
`translation + scale * (rotation * position)`. Research manifest `RVA-F-0125`
documents this title specific contract. The descriptor contains no geometry
arrays, transformed vertex copies, or GPU state.

Direct and explicitly opened archive NIFs use the same wireframe and Inspector
behavior. The application draws transformed positions as wireframe edges and
presents retained material, texturing, and source inventories as read only text.
Presentation does not alter the format contract, copy geometry arrays, transform
normals, evaluate materials, resolve source names, load textures, decode pixel
data, or create GPU model resources. Current controls, deterministic fitting,
numeric formatting, byte escaping, and empty state behavior are documented in
the [User guide](user-guide.md#direct-and-embedded-nif-wireframes).

ReRevved Tools makes no winding, front face, handedness, up axis, or coordinate
space claim and does not convert, export, or write NIF content. Tests construct
wholly synthetic containers, including one complete material positive untextured
model. No retail assets or captures are included.
