# Configuration and files

## Layout and session state

Every page has the title and tab bands, a centred page panel, a help strip and
a prompt bar. The Files page arranges its panes inside the page panel.
The [user guide](user-guide.md#layout) describes their controls. The Files
panes cannot be undocked and their splitters resize them, so there is no layout
reset.

The layout is saved in `layout.ini` in the Tools data folder:
`%LOCALAPPDATA%\ReRevved Tools` on Windows, and `$XDG_DATA_HOME/rerevved-tools`
(or `~/.local/share/rerevved-tools`) elsewhere. It holds the Files pane sizes.
The current page is not saved. Tools writes no `imgui.ini` in the working
directory. While `layout.ini` does not exist yet, it reads an `imgui.ini` found
there as its starting layout. When no data folder is available, the layout is
not saved. Every launch starts on the Map page unless a command line reference
opens, which shows Files. `--smoke-test` never reads or writes the file and
round trips layout in memory. Tools has no portable profile directory or game
configuration file.

## Display scale

The interface is a 1600 by 900 layout scaled to fit the window, and text is
rasterized at the physical resolution. The window keeps a 16:9 shape while you
drag its edges, down to 800 by 450. When it is maximized or snapped to another
shape, the layout grows wider or taller to fill it. Tools first opens at the
largest 16:9 size that fits in most of the primary monitor's work area. Smoke
tests open a 1600 by 900 window and report the scale they ran at. On Windows the
framebuffer matches the window, so the scale is 1.

Opened documents, source directory listings, archive selections, extraction
inputs, and NIF preview controls last for the current session. Tools keeps no
log or list of recently opened game files.

## Map files

A map file is a `.rrmap` text file saved wherever you choose, written to a
temporary sibling and renamed over the previous save. It holds the 1088 grid
bytes and the bytes the map started from, the scenario and the export form. It
never holds images. They are generated from the grid again when the map opens.
If a map file has several scenario entries, Tools opens the first and uses its
title and description in every language. A map file of 1 MiB or more is not
written, so a map with very long scenario text keeps its previous save and Tools
reports the failure.

Beside `layout.ini`, `recent-maps.txt` lists the last ten maps opened or saved,
and the `Recovery` folder holds a `.rrmap` copy of a map's unsaved changes while
it has them. Maps cannot be saved into that folder. Where the system has no file
dialog, `Save` puts a new map in the `Maps` folder there and maps open from the
Recent tab. `--smoke-test` keeps no data unless
`--smoke-data <existing directory>` names a folder to use instead, and saves
maps without a dialog, like a system without one.

## Source and output locations

You choose source paths through Open, Sources, drag and drop, or command line
arguments. There is no required game data directory or automatic installation
scan. [Development](development.md) owns the build output locations.

For [single entry FPK extraction](user-guide.md#extract-one-entry), choose a
new destination in an existing writable directory. ReRevved Tools does not
create parent directories or overwrite an existing destination. A write or close
failure can leave an incomplete new file. Inspect it before removing it or
retrying to another new path.

[Map pack export](user-guide.md#map-page) writes a new pack ID child under an
existing output parent, containing `asset-pack.toml` and
`assets/map-pack/dlc.fpk`. Existing destinations and overlap with an open game,
source or reference folder are rejected. The writer stages and validates the
pack before publishing without replacement, and attempts staging cleanup on
failure. Cleanup failure can leave staging files. A published pack remains if
later cleanup or validation fails. Inspect the diagnostic and any remaining
output before retrying to a new destination.

## Backups

Keep a separate backup of original game files and any extracted files you want
to retain. ReRevved Tools neither changes the source through its inspection
workflows nor creates backups for you. An archive entry opened in memory is not
an exported file. Closing it discards the session state, not the source file.

The fixture generator described in [Testing](testing.md) is a development tool,
not the application's extraction workflow.
