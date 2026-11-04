# Troubleshooting

Start with the exact status and diagnostic in Inspector and Preview. In a drop
or command line batch of several files, each file that fails to open shows its
own notice. Notices fade after a few seconds. Reopen the file to see its error
again. Remove local paths from reports and do not attach retail files or
extracted assets.

## The window does not open

ReRevved Tools needs an OpenGL 3.3 core context, even for metadata inspection.
Run it from a terminal to see GLFW diagnostics. Check that the display session
and graphics driver provide that context.

For build failures, use the individual stages in [Development](development.md).

## Open rejects the path

Open accepts regular files, not directories. Use `Browse Folder` for a
directory. Check the spelling and read permissions. When the system dialog is
unavailable, a rejected typed path stays in the field with its error so you can
correct it.

Reopening the same path selects its existing document rather than reloading its
bytes. Close that document and open it again to read an externally changed file.
Different path spellings can open separate documents for the same file.

## Sources is incomplete or cannot browse a path

A partial listing shows the limit that stopped it. Browse a smaller directory.
A retry can return a different subset. Use `Retry / refresh` after correcting a
directory error. Symbolic links, junctions, reparse points, and linked
ancestors are blocked in Sources. Browse the real directory instead.

`Cancel Listing` is cooperative. It cannot interrupt a filesystem call already
in progress. See [folder browsing](user-guide.md#browse-folders) for its limits.

## A file opens but has no preview

An extension identifies a candidate format, not a supported variant. Read the
reported stage: parsing, capability, preview decoding, model assembly, or
presentation. If a load error has no stage classification, its original
diagnostic remains available.

- GFX provides metadata, not movie rendering.
- BIK and unknown file kinds provide generic inspection, not a preview.
- DDS preview supports only the legacy layouts in
  [File format support](file-formats.md#dds). A metadata valid DX10 header is
  not a supported image preview. An OpenGL texture size limit is a presentation
  failure, not proof of invalid DDS bytes.
- MP3 provides a waveform, not audio playback.
- NIF is a static wireframe. A valid parsed document can still fail assembly or
  projection. Its retained Inspector data remains available. Try another
  projection or `Fit/Reset` for navigation problems, not as a repair for a
  reported structural or geometry error.

## An archive entry will not open

Select its intended parser in `Open As`, then select `Open Selected in Memory`.
Filtering and header hints never choose or run a parser. A failed parse does not
prevent extraction of the selected range. Check the original entry number. A
numeric jump can select an entry hidden by the current filter.

## Extraction fails

Use a new destination path in an existing writable parent directory. An existing
destination is refused, even when you intend to replace it. A write or
finalization failure can leave an incomplete new file. Check the output and
choose a new path or remove only the failed output you have verified is yours.
See [Configuration and files](configuration.md#source-and-output-locations).

## Report a reproducible problem

Include the preset or executable version, platform, exact steps, status, and
diagnostic. Where possible, reproduce it with a [synthetic fixture](testing.md)
and state the expected result. Say whether the parse and the preview each
succeeded, and which checks you ran or skipped.
