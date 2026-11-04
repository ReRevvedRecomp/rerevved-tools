# Getting started

ReRevved Tools starts on the Map page. It does not find a game installation,
download retail content, or start the game.

## Start the application

On Windows, unzip a [release](https://github.com/ReRevvedRecomp/rerevved-tools/releases)
and run `rerevved-tools.exe`.

To build it yourself, follow [Development](development.md). From the repository
root, start the Windows Debug executable:

```powershell
.\out\build\win-amd64-debug\rerevved-tools.exe
```

For Linux Debug:

```sh
./out/build/linux-amd64-debug/rerevved-tools
```

Use the matching build directory for a different preset. For a check without
retail files, create the
[synthetic fixtures](testing.md#generate-workspace-fixtures) and open
`valid.dds`.

## Draft a map

Follow the [Map page](user-guide.md#map-page) to draw a map, set its scenario
details, and export a map pack. Tools generates the terrain images itself. The
user guide covers where to put the pack and what blocks export.

## Inspect a file

1. Choose `Open File` in the prompt bar on the Files page (`Ctrl+2`), or
   press `Ctrl+O` there.
2. Pick a file in the system dialog.
3. Read the status in Inspector before Preview. A file can open for generic
   inspection even when its format parser fails.
4. Use Open Files at the top of Sources to switch files, and `Close File` to
   close one.

You can also drop files onto the window or pass file paths after the executable
name. [Browse Folder](user-guide.md#browse-folders) lists a directory without
loading its files.

## Inspect an archive entry

Opening an FPK does not open any embedded document. Select an entry, choose the
intended format in `Open As`, then select `Open Selected in Memory`. A header
hint is not a parser choice. See
[Archive Explorer](user-guide.md#archive-explorer) for navigation and
extraction.

Keep original files and extracted outputs separate. Tools does not overwrite an
existing extraction destination. See [Configuration and files](configuration.md)
for saved state and backups, or [Troubleshooting](troubleshooting.md) when a
file has no preview.
