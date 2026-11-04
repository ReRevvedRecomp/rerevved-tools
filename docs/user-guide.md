# User guide

ReRevved Tools has two pages: **Map** creates content, and **Files** opens
game files read only for reference. Source files are never modified in place.
See [Configuration and files](configuration.md) for session persistence and
output locations.

## Layout

Tools opens on the Map page, or on Files when a command line reference opens.
The window is laid out like the game's menu screens:

- The title band shows the ReRevved wordmark at the left. It is the window's
  title bar on Windows: it names the open map, with `*` while it has unsaved
  changes, or the file open on Files, and holds minimise, maximise and close.
  Drag it to move the window and double-click it to maximise. The window edges
  resize it and Snap works as usual. Elsewhere the system title bar stays.
- The tab band's first row switches pages: `Map` (`Ctrl+1`) and `Files`
  (`Ctrl+2`). Its second row holds the Map tabs, and is empty on Files.
- The page sits in a centred panel.
- The help strip under the panel gives a hint for the current tab, with a
  warning in amber when one applies.
- The prompt bar at the bottom holds the current tab's actions with their keys.
  `Menu` (`Esc`) always holds `About ReRevved Tools` and `Exit`. While a map is
  open it also holds `New Map`, `Open Map...` (`Ctrl+O`), `Save` (`Ctrl+S`) and
  `Save As...` (`Ctrl+Shift+S`).

The whole layout scales with the window. See
[Display scale](configuration.md#display-scale). Short confirmations and failed
opens appear as notices in the top right corner.

The colours follow the game's own menus: royal blue panels, bright blue
highlighted rows, light blue section headings over a rule and light keycaps
in the prompt bar. Labels, tabs and buttons use Title Case. `Ctrl+Tab` does
not switch windows.

The Map page keeps its open map when you switch pages. See
[Map files](#map-files) for saving it.

## Map page

Before a map is open, the second tab row offers Create and Recent. Once it
exists, the row offers Grid and Export.

1. **Start:** On Create, type the map's name in `Map Name`, pick the starting
   land and press `Create Map` (or `Enter`). The name becomes the map's title.
   Tools picks a free scenario ID of 100 or higher, a one letter map name, a
   pack ID and the lowest free scenario file slot that recent maps and the
   folders they export to do not already use. The first three show in the Pack
   rows, and the slot is under Advanced on the Export tab. Starting land is
   grouped into six tabs:

   | Group | Land | Shape |
   |---|---|---|
   | Landmasses | `Blank` | All sea to draw on |
   | | `Pangaea` | One landmass |
   | | `Continents` | Three continents and a few small islands |
   | | `Small Continents` | Six smaller continents and a few small islands |
   | | `Oval` | One rounded landmass in the middle |
   | | `Fractal` | Ragged land with long ridges of mountains |
   | | `Terra` | An old world in the east where every player starts, and an empty new world in the west |
   | | `Tilted Axis` | Three continents, with the climate running west to east instead of north to south |
   | | `Shuffle` | Any land that grows with Land Size, at a random size |
   | Islands | `Archipelago` | Many small islands |
   | | `Large Islands` | A few large islands |
   | | `Tiny Islands` | A scatter of very small islands |
   | | `Four Corners` | Four landmasses in the corners and a small one in the middle |
   | Inland Water | `Ring` | Five lobes of land joined in a ring around a sea |
   | | `Donut` | A ring shaped landmass around a lake |
   | | `Inland Sea` | A ring of land around a central sea |
   | | `Lakes` | Nearly all land, with lakes |
   | Matchups | `Skirmish` | Central land whose left and right halves mirror each other |
   | | `Hemispheres` | Two continents, west and east |
   | | `North vs South` | Nearly all land, split by a desert band across the equator |
   | | `West vs East` | Two halves of land split by a channel |
   | Climates | `Highlands` | Mostly land, heavy with hills and mountains |
   | | `Arborea` | Three continents under forest |
   | | `Sandstorm` | Three continents, mostly desert |
   | | `Ice Age` | Land only in a band along the equator, with wide polar ice |
   | | `Frontier` | One landmass with mountains scattered across it |
   | | `Great Plains` | Mostly open grassland and plains |
   | | `Boreal` | Three continents of forest and hills, with no desert |
   | Real World | `Earth` | The world's continents |
   | | `Japanese Mainland` | Japan's four main islands |
   | | `British Isles` | Great Britain, Ireland and the isles around them |
   | | `Mediterranean` | The sea between southern Europe and North Africa |
   | | `Americas` | North, Central and South America |
   | | `Europe` | Europe and its seas |
   | | `Africa` | Africa and the coasts across its seas |
   | | `Australia` | Australia and the islands near it |
   | | `Caribbean` | The Caribbean Sea, its islands and the coasts around it |

   The `Land Size` arrows pick `Small`, `Medium` or `Large`, which sets how
   much of the 32 x 32 grid is land. Blank, Shuffle and the Real World lands
   ignore it. `Randomize` picks another layout. On a Real World land it keeps the
   coastline and changes the terrain and start markers.

   `Players` picks 2 to 7 start markers, one per player. 5 is the default and
   what every retail map has. The game never fills a missing start, so play the
   map with no more players than it has starts. `Mirror` copies one half of the
   land onto the other as the grid shows it: `Left to Right` copies the left
   half onto the right, and `Top to Bottom` copies the top half onto the
   bottom. On a mirrored map the starts come in mirrored pairs. With an odd
   number of players, the last one starts on the mirror line, on land raised
   for it where the line is sea. Skirmish always mirrors left to right, and
   Terra, the Real World lands and Blank never mirror. For those lands the Mirror
   buttons are greyed out. With a mirror on, Shuffle picks only lands that
   mirror.

   Every choice puts polar ice on file columns 0-1 and 30-31, as the game's
   maps do. Every choice but Blank places its start markers on grassland or
   plains and gives each one at least two grassland, two sea and two forest
   tiles among the eight tiles around it. Starts keep four tiles apart, and
   further when the land per player allows, or three where the land leaves no
   room for four. With teams, teammates start near each other and away from the
   mirrored opponents.

   Each start's share of the land also gets good city sites: one for every 20
   land tiles beyond the start's own neighbourhood, up to three. A good site
   has at least two forest, two grassland or plains, and two sea or desert
   tiles around it, and keeps three tiles from starts and other sites.
   Land terrain groups no more than on the game's own maps: a four-connected
   group holds at most 11 grassland, 9 plains, 10 forest, 4 hill, 10 desert
   or 16 mountain tiles, a straight run at most 6, 7, 6, 3, 5 or 7 of them,
   and no land terrain fills a three by three block. Two by two blocks of
   grassland, plains and forest come at most at the game's rates of about
   0.7, 0.57 and 0.4 per map, and fewer on a mirrored map. A Climates land's
   own terrain, with r times its usual share, may run r times as far, group r
   squared times as large and form r to the fourth times as many two by two
   blocks. Arborea's and Boreal's forest and Great Plains' plains form any
   number of them. Its solid squares grow with its share but never reach
   five by five. Boreal has no desert at all. North vs South's desert band is
   left whole. The same land, size, layout, players, mirror and teams always
   give the same grid.

   The name may use printable ASCII except `&`, `<` and `>`, the characters
   the game's scenario files allow. Runs of spaces become one. Used values are
   reread every few seconds while the page is open and again when the map is
   created. When all 26 letters are taken, or a pack or recent map cannot be
   read in full, a warning appears in the help strip. Choose the letter under
   Advanced on the Export tab before exporting.

   Recent lists the maps you last opened or saved, newest first. `Open` opens
   one, and `Open Map` (`Ctrl+O`) finds any other.
2. **Grid:** Pick a tool in the toolbar: Select, Terrain or Start Marker. The
   Terrain brush lists the game's eight terrains by name: Sea, Grassland,
   Plains, Forest, Hill, Desert, Mountain and Polar Ice. The Start Marker brush
   adds or removes markers. The game reads the first eight start markers in file
   order as civilization start positions. The grid numbers them 1 to 8 and marks
   later ones with x. Which marker each player gets is unproved. Brushes keep
   the tile's other bits and the 64 byte footer unchanged. `Undo` and `Redo` in
   the prompt bar (`Ctrl+Z`, `Ctrl+Y`) work per stroke. The grid always fits
   its canvas.

   `Randomize Terrain` at the right of the toolbar rerolls the land's terrain
   into grassland, plains, forest, hills, desert and some mountains, keeping the
   coastline, polar ice, terrain codes 8-15, start markers and every tile's
   other bits. A start marker on land keeps grassland or plains under it and,
   where its land neighbours allow, two grassland and two forest tiles around
   it. Its sea neighbours are left as drawn. Where the start markers leave
   room, the terrain keeps the group, run and block limits of a new map, and
   each start's share of the land gets city sites the same way. When the land
   is mirrored left to right or top to bottom, the new terrain is mirrored the
   same way. Each press gives a new mix, and one `Undo` restores the
   previous terrain.

   The tile details sit in a column beside the grid. They show the selected
   tile's position and terrain, with any advisory finding for it, such as a
   start marker after the first eight or a terrain code 8-15 (which has no game
   terrain, but a draft that holds one keeps it). Hovering a tile
   shows its terrain. The grid and the Files preview draw the map as the game
   shows it, the transpose of file order: file rows run left to right (the top
   numbers) and file columns top to bottom (the left numbers), with north up.
   Arrow keys move on screen.

3. **Export:** Writes a map pack: a folder with
   `asset-pack.toml` and `assets/map-pack/dlc.fpk`. Details holds the
   `Title`, shown in the game's scenario list in every language and used as
   the pack's name, an optional `Description`, also in every language,
   `Multiplayer` and an optional `Author`. `Save To` picks the folder to save
   in with `Choose`.

   Advanced holds the values Tools picked for the map. It opens by itself when
   one of them blocks export. `Pack ID` takes lowercase letters, digits and
   single dashes, up to 39 characters, and follows the title until you change
   it. `Scenario ID` is 100 or higher, since the game's own scenarios use lower
   IDs, and `Map Letter` is one letter that no other enabled pack uses.
   `Version` and the `Scenario File Slot`, 5 to 31, complete it.

   Choosing a `Save To` folder that already holds map packs, such as
   ReRevved's `maps` folder, replaces any slot, scenario ID, map letter or
   pack ID of this map that a pack there already uses, and a message names the
   change. If no slot or letter is free, or a pack cannot be read in full, a
   warning says so and you choose under Advanced. A folder restored with a saved
   map is not a new choice and changes nothing.

   Game Rules, also closed at first, lists the scenario's Xbox variator
   levels with readable names. Hovering a row shows its XML name and default,
   and `Use Defaults` resets them all. What most of them do in game is not
   yet known.

   `Before You Export` lists what still blocks export, such as a map with fewer
   than two start markers, then advice that does not: fewer than five start
   markers (every retail map has five, and the game never fills a missing one,
   so that player starts at leftover coordinates from an earlier game) and
   counts of tiles with advisory findings. `Export Map Pack` in the prompt bar
   (or `Enter`) packs `DLCScenarioData<slot>.xml`, `<letter>.map` (the grid
   with a 0xFF footer) and the three DDS images into `dlc.fpk`, writes the pack
   without overwriting an existing folder, validates it and reads `dlc.fpk`
   back. The receipt shows the folder and map. `Copy Receipt` copies the full
   text, including the size and SHA-256.

   To play it, put the pack folder in ReRevved's `maps` folder (next to
   `rerevved.exe`), add its ID to the profile's `map_order.txt` and restart.
   The map then appears in Choose Scenario. Each enabled map pack needs its own
   slot, map letter and scenario IDs, which choosing the `maps` folder as
   `Save To` arranges. ReRevved refuses a pack that repeats an
   earlier one and names it in its log.

### Map images

A map pack carries three images: `Map<letter>_Heights.dds`,
`Map<letter>_MountainHill_Blends.dds` and `Map<letter>_Lightmap.dds`. Tools
generates all three from the grid, so there is nothing to draw or import. They
are generated when the map is created or opened, and again when you leave the
Grid tab after editing the tiles. Generation runs in the background and takes a
moment, during which the window stays responsive. `Export Map Pack` becomes
available once the images match the grid. Until then `Before You Export` lists
"Generating map images...". Sea depth follows the distance to land, hills rise
as smoothed peaks, neighbouring mountains join into ranges with a ridge along
the middle, blends mark hills in red and mountains in green with blue deep
inside, and the lightmap shades the relief. The levels follow the game's own DLC
maps, in which the image is the file grid transposed: image row i, column j is
file row j, column i. Generation is deterministic for a given grid. Heights use
the 512 x 512 16 bit layout every shipped heights file uses. Blends (2048 x
2048) and the lightmap (4096 x 4096) are DXT1, like the shipped files. The game
loaded all three in a map pack trial. Its height scale and alignment with the
tile grid are not yet measured.

## Map files

A map is saved as a `.rrmap` file wherever you choose. `Save` (`Ctrl+S`) writes
it to its file, asking where the first time. `Save As...` (`Ctrl+Shift+S`)
always asks. An opened map returns with its grid, scenario, export form and open
tab, and its images are generated again from the grid. Undo history starts
empty. `Export Map Pack` is separate: it writes the game's map pack, not the
editable map. The file format is described in
[Configuration and files](configuration.md#map-files).

`Open Map...` also opens an exported map pack: choose its `asset-pack.toml` or
its `assets/map-pack/dlc.fpk`. Tools reads the grid and scenario from the
archive and the export form from the manifest, and generates the images again.
The pack itself is never changed. The map opens unsaved, so `Save` asks where
to write its `.rrmap` file. Exporting it again needs a new Pack ID or the old
pack folder removed, because export never replaces a pack.

A new map counts as unsaved until its first save. While a map has unsaved
changes, `New Map`, `Open Map...`, `Exit` and the window close button ask whether
to save them: `Save` saves first, `Don't Save` drops the changes and `Cancel`
returns to the map. `New Map` ends on the Create tab.

While a map has unsaved changes, Tools also keeps a copy of them about a second
after each change, and deletes it once the map is saved or closed. If Tools
stops before that, the next launch offers to restore the changes. `Discard`
deletes the copy instead. Restored changes stay unsaved until you save them.

## Map pack suites

`rerevved-map-suite` writes several map packs from a plan file, without a
window. It sits beside `rerevved-tools.exe` in the build directory:

```powershell
.\out\build\win-amd64-debug\rerevved-map-suite.exe suite.plan .\packs C:\Games\ReRevved\maps
```

The second argument is an existing output folder. Any further folders are maps
folders whose pack IDs, scenario IDs and `MAP` names the plan must not reuse.
With `--save-maps <folder>` before the plan, each map is also saved in that
existing folder as `<pack id>--<MAP>.rrmap`, a map file the editor opens.
The plan holds `[pack]` sections, each followed by the `[map]` sections it
packs, with one `key = value` per line. Lines starting with `#` are comments.

```text
[pack]
id = my-duels
name = My Duels
slot = 6

[map]
land = West vs East
players = 4
mirror = Left to Right
teams = yes
id = 101
map = rr_west_east_2v2
title = West vs East 2v2
```

A `[pack]` takes `id`, `name` and `slot`, and optionally `version` (default
`1.0`), `author` and `description`. A `[map]` takes `land`, `id`, `map` and
`title`, and optionally `description`, `size`, `players`, `mirror`, `seed`,
`teams`, `multiplayer` and `displaycard`. Lands, sizes and mirrors are named as
on the Create tab, and the defaults match a new map there. `map` is the map's `MAP` name, up
to 20 letters, digits and underscores. `teams = yes` needs a mirror and an even
player count, and numbers the starts on the first side of the mirror before
their mirrored partners. `multiplayer = yes` also lists the map in multiplayer.
`displaycard` sets the scenario's `DISPLAYCARD` variator.

A `[map]` with `source` imports a map instead: a map file or a pack that holds
one map, relative to the plan. It keeps the source's grid, `MAP` name, ID,
texts and scenario settings unless the plan sets `id`, `map`, `title`,
`description`, `multiplayer` or `displaycard`, and takes none of the land keys. `randomize =
<seed>` rerolls its land terrain as `Randomize Terrain` does, with that seed.

Each `[pack]` becomes one pack whose `DLCScenarioData<slot>.xml` lists every
map in it. The plan is checked for repeated or used pack IDs, slots, scenario
IDs and `MAP` names, and every pack is assembled once, before any is written.
A map without room for every start, or a pack whose `dlc.fpk` would pass
95 MiB (about 9 maps), stops the run before any writing, which keeps each pack
under GitHub's 100 MiB file limit. Existing folders and map files are never
replaced, and the same plan writes the same bytes. `Open Map...` opens only
packs that hold one map.

## Files page

Files arranges Sources, Preview and Inspector as side by side panes. Open Files
at the top of Sources lists the open files: click one to show it, or its `x` to
close it. Two files with the same name also show the nearest folder that tells
them apart. `Close File` (`Ctrl+W`) in the prompt bar closes the selected one.
The folder you browse is listed under it. The panes have no tab bars and cannot
be undocked. Drag the borders between them to resize. Switching pages preserves
their documents and selections, and opening a reference never replaces the open
map.

## Open and close files

Use any of these entry points:

- Choose `Open File` in the Files prompt bar or press `Ctrl+O` on the Files
  page. The system file dialog opens. When it is unavailable, a path field
  appears instead.
- Drop one or more files onto the window. They open on the Files page.
- Pass one or more file paths on the `rerevved-tools` command line. Dropped and
  command line references switch to the Files page.
- Double-click a regular file in Sources, or select it and choose `Open`.

A typed path rejected by file I/O leaves the field, input, and exact error
available for correction. Recognized regular files whose loader fails still open
as inspection only documents: source path, size, and header remain visible, with
the exact diagnostic in Inspector and Preview. Their status does not claim a
successful parse. Reopening the same lexical path selects the existing document.
Aliases are not canonicalized. Displayed source paths are captured as absolute
paths at open time, while lexical input spelling remains the reuse key.

The tooltip for each Open Files item shows its complete source path. The active
Inspector and Preview show source identity and status. Closing the active middle
document selects its successor. Closing the last selects its predecessor.
Closing the only document returns to the empty state. Other documents retain
their independent archive selection and CPU preview controls.

A dropped, command line or dialog chosen file that fails to open shows a notice
naming the file and its error, one per file in a batch. Sources and the typed
path field show their errors in place. See
[Configuration and files](configuration.md) for saved state and output
locations.

The keyboard can select documents and activate buttons, including Open File in
the empty Preview.

ReRevved Tools identifies FPK, DDS, GFX, BIK, MP3, NIF, and MAP by extension.
Other regular files receive generic header inspection. An extension does not
guarantee parsing or preview support.

## Browse folders

Choose `Browse Folder` in the Files prompt bar and pick a folder in the system
dialog. When that dialog is unavailable, enter a directory path and choose
`Browse` or press Enter. This replaces the current Sources root without closing
opened documents. Use a fully qualified
path or an ordinary relative path. Windows drive relative paths are rejected.
Paths and directory listings are kept only for the session.

Selecting a source row highlights it and shows its full path. Expanding a
directory requests only its immediate children. It never reads an asset header,
parses a file, or opens a document. Regular files open only through `Open` or
double-click, using the same inspection and outcome flow as other file inputs.
Directory double-click expands or collapses its row.

Directory errors identify the affected path and preserve the filesystem's
message. Select that directory and use `Retry / refresh` after correcting the
cause. `Cancel Listing` cancels that directory's queued or running request. It
can be retried. Changing the root cancels old requests and rejects their late
results. Browsing does not follow directory reparse points, junctions, symbolic
links, or linked ancestors. Such paths remain visibly blocked. Linked files are
also excluded from Sources Open. Explicit Open and double-click recheck source
metadata. This is not an atomic guarantee against external filesystem changes
during an operation. An explicitly typed file path retains the existing file
open contract.

Listings are bounded. A partial listing has a visible reason and is never
presented as complete. Browse a smaller directory to reduce its scope. The
retained subset is sorted by filename after enumeration. Filesystem enumeration
order determines which entries fit before a limit, so the subset may differ
after retry. Collapsing a loaded directory retains its listing.
`Retry / refresh` refreshes it. Browsing never recursively indexes the root.

## Archive Explorer

Opening a valid version 6 FPK displays the archive toolbar and entry table above
Preview and the `FPK archive` Inspector. A nonempty archive initially selects
entry 1. An empty archive remains unselected. The work area breadcrumb shows the
archive filename, with the full path in its tooltip, and the original entry
number.

Initial selection does not infer a format, open an embedded document, decode a
preview, extract a file, or write data.

### Navigate entries

- Select an `Entry N` row in the entry/header hint/byte range table.
- Use the `Previous` and `Next` arrow buttons to select the preceding or
  following matching entry.
- Enter a one based number in the `Entry number` field, then press Enter or
  select `Go to entry`.

Invalid, zero, empty, or out of range jumps preserve the current selection and
show the navigation error. Very large indexes use clipped row rendering and an
explicit selectable entry limit rather than silently narrowing a parser result.

`Filter metadata` matches a case insensitive literal substring of the original
entry number, header hint, decimal or hexadecimal range, or raw unknown bytes.
It filters retained metadata without opening entries. Matching rows stay in
source order and keep their original numbers. They are never renumbered. The
table shows offset + size = exclusive end in decimal and hexadecimal. Scroll
horizontally if the full range does not fit.

Numeric jumps address the original index even when the requested entry does not
match the filter. A hidden selection notice identifies that entry. Inspector,
Open, and Extract still address it. Filtering alone preserves the selected
entry, parser choice, opened document, and preview. With no matches, Previous
and Next are disabled. Clear the filter to restore all selectable rows.

The Inspector's `Entry N` section displays the entry number and validated byte offset,
size, and exclusive end in decimal and uppercase hexadecimal. `Copy byte range`
copies that exact summary. The two unknown record fields remain uninterpreted.

### Open an embedded document

Archive records do not provide a proved filename or format. The entry list and
`Entry N` metadata show a conservative header identification when an entry
starts with the recognized DDS, GFX/CFX, NIF, or ID3 signature. ID3 is labeled
as tagged audio rather than a validated MP3. Other entries remain `Unknown`.
Raw MP3 and MAP data have no identification rule. A header identification does
not validate the complete document.

`Open As` starts at `Choose parser`, with opening disabled. Select the intended
type, then select `Open Selected in Memory`. Choosing a parser does not open the
entry. Hints never select or invoke parsers. Available formats are:

- DDS texture
- GFX movie
- Gamebryo NIF
- MP3 audio
- Xbox DLC map

ReRevved Tools parses the selected byte range directly from archive memory and
does not create a temporary retail file. A mismatch reports the selected
parser's exact malformed, truncated, or unsupported content reason in both
Inspector and Preview, while retaining the selected range and generic header.
Correct the format choice or select another entry. ReRevved Tools never guesses.

`Close Opened Entry` releases only the opened embedded document and its preview
state. The archive, selection, explicit format choice, metadata, extraction
path, extraction result, and source bytes remain available. The same entry can
be opened again immediately. Each archive retains one opened embedded document.
Switching top level documents preserves each archive's independent filter,
selection, parser choice, extraction
input/results, opened document, and CPU preview controls. Selecting another
entry resets Open As to `Choose parser` and releases its previous opened
document and preview. Reselecting the same entry preserves those states.

### Extract one entry

Select `Extract Selected...`, enter a new destination path, and press Enter or
select `Extract`. Both submission paths perform the same operation.

- An existing destination is never overwritten.
- Failure keeps the dialog open, preserves the typed destination, and displays
  the exact extraction error.
- Success clears the destination and closes the dialog.
- `Cancel` closes the dialog without erasing the typed destination.

Extraction revalidates the selected range before writing. There is no bulk
extraction, archive rewriting, or repacking workflow. Extraction is available
without choosing a parser and after a parse or preview failure. For destination
requirements and incomplete outputs, see
[Configuration and files](configuration.md#source-and-output-locations).

## DDS, GFX, MP3, and MAP views

### DDS

Supported legacy 32 bit RGB/RGBA and DXT1/3/5 DDS files display in Preview. RGB
without declared alpha is shown opaque. DDS headers do not record Xbox tiling,
so a direct DDS file offers `Linear` and `Xbox tiled` above the preview. Files
open as Linear, and the other choice rereads the file. The wrong choice shows
scrambled pixels or an error, and the choice stays available after a failed
read. Embedded DDS entries in archives always read as Linear. When a file holds
several mip levels, the arrows step through them. A problem with a later level
is shown as a warning while the readable levels stay available. The Inspector
lists the payload layout and decoded level count.

Unsupported compression, DX10 pixel formats, cubemaps, volume textures, mask
layouts, bit counts, or truncated pixels produce an explicit preview error
instead of a guessed image. DDS metadata errors are labeled parse failures.
Canonical unsupported encoding errors, including a metadata valid DX10 header,
are capability limits, as are tiled dimensions that are not powers of two,
packed Xbox mip tails and images over the 64 megapixel preview limit. Invalid
dimensions, mip counts, size overflow, and truncated pixels are preview decoding
failures. Generic path/header inspection remains available when the loader
returns no DDS document. No additional DDS metadata is promised for those
failures.

### GFX

Supported uncompressed GFX v2 files expose movie and external image metadata in
Inspector. Names are escaped byte strings because no text encoding or path
policy is proved. ReRevved Tools does not execute, decompress, render, export,
or write GFX content.

### MP3

Preview displays a peak waveform for at most the first ten seconds of an MP3
stream after complete frame structure validation. ReRevved Tools does not play,
transcode, export, or write audio.

### Xbox DLC MAP

Preview displays the retained 1024 byte core as a 32 by 32 byte grid in game
view (file rows left to right, file columns top to bottom) and the separate 64
byte footer as raw bytes. Cell color is one of 16 palette entries keyed by the
low nibble. The legend names codes 0-7, and codes 8-15 have no game terrain. A
corner dot marks flag `0x40`, a corner triangle flag `0x80`, an inset outline
raw bit `0x10` and a diagonal hatch raw bit `0x20`. Hover details expose the
raw byte and high bit states without assigning terrain semantics. This
inspection preview stays read only. Use the [Map page](#map-page) for a separate
editable map.

## Direct and embedded NIF wireframes

Open a supported NIF directly, or explicitly open an FPK entry as
`Gamebryo NIF`. Both routes use the same read only Inspector and wireframe
presentation. Selecting an archive entry never opens or previews it as NIF.

The initial view selects the first assembled mesh, `Auto` projection, and a
fitted view. Preview controls are:

- `Mesh` selector: select any assembled mesh or `All Meshes`. Individual labels
  contain only the mesh number, shape block index, and data block index.
- `Projection` segmented control: choose `Auto`, `XY`, `XZ`, or `YZ`. Auto
  chooses the raw axis pair whose transformed bounds have the greatest area.
- Pan: drag the wireframe with the left mouse button.
- Zoom: use the mouse wheel while the wireframe region is hovered.
- `Fit/Reset` button beside the projection control: restore the centered, aspect
  preserving fitted view.

Changing mesh or projection restores a fitted view. `All Meshes` draws every
supported assembled mesh in common transformed space and fits their combined
transformed bounds. It does not combine the document owned geometry arrays.
Separate direct documents and archives retain independent mesh, projection, pan,
and zoom state.

Preview applies retained static node and shape transforms to positions while
generating two dimensional edges. It does not transform normals, fill faces,
shade materials, load textures, infer winding, handedness, up axis, or
coordinate space, or create GPU model resources.

Parse, model assembly, nonfinite position, unrepresentable transform, empty
bounds, and degenerate projection failures remain explicit. An assembly or
presentation failure does not hide the parsed Inspector or Archive Explorer
controls. The common context labels the failed stage and retains the exact
reason. An unclassified loader failure remains labeled as such.

## NIF material and texture source Inspector

Inspector presents the complete retained document inventory, not only the
property references of the selected Preview mesh. Direct and explicitly opened
archive NIFs share the same Inspector presentation.

### Material properties

Every retained `NiMaterialProperty` appears in source block order and is
identified by source block index. Fields appear in this order:

1. Ambient color - three retained f32 components
2. Diffuse color - three retained f32 components
3. Specular color - three retained f32 components
4. Emissive color - three retained f32 components
5. Glossiness
6. Alpha

Finite values use up to nine significant decimal digits. Positive and negative
zero remain distinct, and infinity and NaN are named explicitly. The text does
not clamp, normalize, replace, or convert these values. It makes no claim about
valid ranges, lighting, blending, shading, or appearance. An empty inventory is
reported directly.

### Texturing properties and sources

Every retained `NiTexturingProperty` and `NiSourceTexture` appears in source
block order. Texturing rows preserve the fixed nine slot sequence, raw presence
bytes, conditional descriptors and transforms, Bump and Parallax extensions, and
ordered shader records. Raw values are shown as neutral decimal, hexadecimal, or
binary32 text without assigning enum or renderer meaning.

Source rows show both retained carriers. Printable ASCII filename bytes remain
literal, backslashes are doubled, and NUL or other nonprintable bytes use
uppercase `\xNN` escapes. This is byte safe metadata display, not path or URL
interpretation. Only the exact validated combination of Use External equal to
one, a valid nonnull filename index, and a null Pixel Data reference is labeled
`Supported external source`. Every other retained combination is labeled
`Unsupported source combination`.

ReRevved Tools does not resolve these names, search archives or directories, load or
decode external textures, interpret embedded pixel data, or change Preview.

## Narrow panels

Archive Explorer actions, extraction controls, NIF Preview controls, and NIF
Inspector text use the active panel or work area width. Controls stay reachable
by shrinking or stacking when needed, and long values wrap instead of requiring
horizontal clipping.

For parser layouts, evidence qualifications, and unsupported format boundaries,
read [File format support](file-formats.md). For failed operations, start with
[Troubleshooting](troubleshooting.md).
