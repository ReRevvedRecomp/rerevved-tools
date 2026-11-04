# Testing

Use [Development](development.md#build-and-check) for the automated verification
command. These checks exercise synthetic files and application behavior. They do
not establish compatibility with every retail file.

## Test targets

| Executable | CTest name | Coverage |
|---|---|---|
| `rerevved_tools_tests` | `rerevved_tools` | Parsers, extraction, workspace state, and GPU free presentation helpers |
| `rerevved_source_browser_tests` | `rerevved_source_browser` | Filesystem browsing without the core/parser library |
| `rerevved_tools_texture_tests` | `rerevved_tools_texture` | Synthetic PNG/DDS import and validation, and the DDS encoding and read back the map images use |
| `rerevved_tools_asset_pack_tests` | `rerevved_tools_asset_pack` | Asset pack schema, writing, validation and nonoverwrite behavior |
| `rerevved_tools_fpk_writer_tests` | `rerevved_tools_fpk_writer` | FPK encoding, writing and read back |
| `rerevved_tools_map_bundle_writer_tests` | `rerevved_tools_map_bundle_writer` | SHA-256, bundle publication and manifest, preflight refusals, verifier failure, destination races, session ownership and manifest escaping |
| `rerevved_tools_xbox_texture_tests` | `rerevved_tools_xbox_texture` | Xbox tiling, endian swaps, DXT block encoding and decoding, mip chains and invalid input |
| `rerevved_tools_xbox_dds_tests` | `rerevved_tools_xbox_dds_tests` | Xbox DDS headers, linear and tiled round trips, legacy mip tolerance, pixel limit, mip generation, import errors and file writing |
| `rerevved_tools_heights_dds_tests` | `rerevved_tools_heights_dds` | Named map heights DDS encoding, parser rejections and file writing |
| `rerevved_tools_terrain_dds_tests` | `rerevved_tools_terrain_dds` | Named map blends and lightmap DDS encoding, parser rejections and file writing |
| `rerevved_tools_terrain_generator_tests` | `rerevved_tools_terrain_generator` | Terrain generation from map tiles: sizes, repeatability, sea depth, peaks, masks and mountain ranges |
| `rerevved_tools_scenario_tests` | `rerevved_tools_scenario` | Scenario model, strict XML reader/writer and diagnostics |
| `rerevved_tools_creator_tests` | `rerevved_tools_creator` | Map draft state, inspection independence, revision prerequisites and discard state |
| `rerevved_tools_map_image_tests` | `rerevved_tools_map_image` | Map images generated from the tiles, with their DDS read back |
| `rerevved_tools_smoke_input_tests` | `rerevved_tools_smoke_input` | Scripted smoke parsing, item matching and click hover checks |
| `rerevved_tools_map_editor_tests` | `rerevved_tools_map_editor` | Byte masks, immutable source/footer, stroke undo/redo and grid coordinates |
| `rerevved_tools_map_wizard_tests` | `rerevved_tools_map_wizard` | Starting land generation (codes, polar ice, markers, start spacing, team starts, city sites, retail terrain group, run and block limits for new and randomized terrain, climate terrain shares, landmasses, repeatability), name checks, free scenario ID, map letter and pack ID suggestions, map pack usage from manifests, MAP and texture names and lenient scenario XML, and FPK entry limits |
| `rerevved_tools_map_draft_tests` | `rerevved_tools_map_draft` | Map file round trip, damaged file refusal, save, listing, the recent map list, file names and age text |
| `rerevved_tools_scenario_form_tests` | `rerevved_tools_scenario_form` | Scenario fields, signed slots, XML read back and artifact readiness |
| `rerevved_tools_map_pack_export_tests` | `rerevved_tools_map_pack_export` | Map pack blockers, dlc.fpk assembly and read back for one or several maps, nonreplacing pack writes and protected folders |
| `rerevved_tools_map_suite_tests` | `rerevved_tools_map_suite` | Map suite plan parsing, clash checks, writing nothing when one map fails, nonreplacing writes, repeatable bytes, saved map files and imported maps |

These targets run without an application window or GPU. The app smoke and
manual desktop checks exercise a different boundary and require a display and
OpenGL context.

## Creator app smoke

The verification script's app-smoke stage runs `rerevved-tools --smoke-test`.
It renders a hidden window for 16 scheduled frames, then two frames with a new
ImGui context loaded from the first context's in memory layout. It never reads
or writes `layout.ini` (see
[Configuration and files](configuration.md#layout-and-session-state)). Without
capture enabled, it invokes no filesystem writer and retains the same window
size and frame counts. ImGui's ID conflict highlight and error recovery tooltips
appear only in smoke runs, so captures show mistakes the normal window hides.

To save every scheduled state for visual review, pass an existing empty output
directory outside tracked content:

```powershell
.\out\build\win-amd64-debug\rerevved-tools.exe --smoke-test README.md --smoke-capture C:\scratch\tools-smoke
```

`--smoke-capture` requires `--smoke-test`. It never creates directories and
exclusively creates each PNG, failing smoke if a capture already exists or
cannot be written. A failed write can leave a partial PNG. Capture uses a hidden
window targeting a 1600 by 900 framebuffer, compensates for DPI scaling, and
reports the effective framebuffer size. The back buffer is read as RGBA after
the second frame of each two frame state, before swapping, and its rows are
flipped for PNG output. The 9 files run from `00-map-start.png` through
`08-restart.png`. `src/app/smoke/smoke_capture.cpp` owns their label/frame table.

When capture is enabled and a `README.md` reference argument is supplied, the
first session also calls `Application::DropPaths` after capturing the Map start
page, which exercises the reference open route. Application exposes no public
drop outcome, so this call has no routing assertion. It does not generate a
native OS drop event.

The schedule visits both pages and the map views, with synthetic in memory MAP
bytes and scenario entries. It includes the empty start page, an edited corner
tile and non-0xFF footer, a scenario whose map letter blocks export, blocked
readiness and export, and the save changes sheet. The export button stays
disabled. Final checks compare the edited corner and preserved source/footer
bytes. It does not fabricate a successful write receipt.

The restart checks that the page returns to startup state (Map, or Files when
a reference opened) with no map draft. A failed scheduled transition or restart
check returns nonzero. CLI reference paths remain optional. No retail files are
required.

Hidden smoke checks rendering and lifetime boundaries. Native file and folder
dialogs, native OS drop delivery, window resizing and letterboxing, partial
Sources interaction and in game acceptance require separate verification. See
the [user guide](user-guide.md) for current authoring behavior.

## Scripted input smoke

`--smoke-input <script>` replaces the scheduled states with a script of pointer
and keyboard steps, run in one session in the same hidden window. The
verification script runs `tests/smoke/input-basics.txt`. On Windows it also
runs `input-chrome.txt`, which checks that the custom title bar's buttons take
the pointer. It runs `input-map-save.txt` and then `input-map-open.txt` with
`--smoke-data` on one new folder under the build directory, copying the saved
map into the folder's `Recovery` folder between them. They check that a new map
asks before exit, that a saved map closes without asking and is listed under
Recent, that the copy is offered for restore and deleted by `Don't Save`, and
that an opened map has no unsaved changes. It runs `input-wizard.txt` on another
folder, which walks the start page: the disabled `Create Map` before a name, the
starting land groups, several lands and sizes, `Randomize`, the player count
and mirror, and a created map with randomized terrain, an undo, and an Export
page whose Advanced section stays closed until opened. Last, it runs
`input-map-folder.txt` from inside a third new folder that holds an empty
`packs` folder. The script exports two maps with the same name into it, and the
verification script then checks for two packs with different slots:

```powershell
.\out\build\win-amd64-debug\rerevved-tools.exe --smoke-test --smoke-input tests\smoke\input-basics.txt
```

While a script runs, Dear ImGui's test engine item hooks record each frame's
item labels and rectangles. `src/app/smoke/ui_probe.cpp` holds the hooks, which are
compiled in but inactive in normal runs. A target names an item by its full
label, the text before `##`, the id after `##`, or the caption without its
icon glyph. A target that matches several visible items needs `@n`. Pointer
positions are converted to physical window pixels and pass through the
application's scaled cursor callback, so a wrong canvas mapping misses its
target. A click presses only once its target is hovered. Buttons, keys and text
are queued directly as Dear ImGui events, and drops call the drop handler.
Native OS input, file dialogs and drop delivery still require separate
verification.

| Step | Effect |
|---|---|
| `wait <frames>` | Draws 1 to 600 frames |
| `move "<target>"` | Moves the pointer to the item's center |
| `click "<target>"` | Moves, presses and releases the left button over three frames |
| `key <chord>` | Presses and releases a key such as `escape` or `ctrl+z` |
| `type "<text>"` | Queues text input |
| `close` | Requests exit as the window close button does |
| `drop "<target>" "<path>"` | Drops a file once the item is visible. Relative paths resolve from the script |
| `expect "<target>"` | Waits up to 60 frames for a visible item |
| `expect-missing "<target>"` | Waits up to 60 frames until no visible item matches |
| `expect-hovered "<target>"` | Waits up to 60 frames until the item is hovered |
| `expect-exit` | Waits up to 60 frames for the application to exit |
| `capture <name>` | Saves `<name>.png` into the `--smoke-capture` directory, which the script then requires |

A failed step prints its script line and exits nonzero. Lines starting with `#`
are comments. A run stops after 6000 frames.

## Compare captures

`--smoke-compare <baseline> <candidate> <diff>` compares same named PNGs in two
capture directories. It prints `same`, `changed` with the changed pixel share,
largest channel difference and bounding box, `resized`, `missing` or `added`
for each file, and writes a highlight image for every changed pair into the
existing empty diff directory, which must differ from both inputs. It exits 0
when every pair matches, 2 when anything differs and 1 on an error, including
two directories without captures. Modal dimming fades with frame time, so sheet
captures can differ by a few levels between builds.

## Generate workspace fixtures

Generate fixtures with the built test executable:

```powershell
.\out\build\win-amd64-debug\rerevved_tools_tests.exe --write-workspace-fixtures C:\scratch\tools-fixtures
```

Pass an absolute path to a directory that does not exist yet, outside tracked
content. The generator creates it and its parent folders. It rejects a relative
or existing path and exits nonzero.

## Check documents and previews

Exercise a mixed valid/missing/malformed CLI or drop batch and read the notices.
Submit a missing typed path. Select and close same basename files through the
list and tabs. Inspect direct and explicit embedded parser failures, DX10
capability limits, truncated DDS pixels, metadata only GFX, unsupported BIK, and
retained NIF Inspector after assembly/presentation failure. Switch DDS and CPU
previews, then close their owners. Check keyboard selection and button
activation, the empty state, and archive open/extract actions at 1280 by 720
with a 280 pixel docked Inspector. Compare fixture bytes and existing extraction
destinations before and after.

## Check the archive work area

Open `multi-entry.fpk` plus another archive document and a direct DDS. Filter on
`DDS` and select original entry 6. Confirm hints leave Open As at Choose parser
and opening disabled. Choose DDS, explicitly open, and inspect its capability
error. Navigate matching Previous/Next without renumbering. Jump numerically to
a hidden entry and inspect its notice and exact range. Test decimal/hex range
and unknown byte filters and a no match filter. Open entry 1 as DDS, switch
documents and return, then change entry and confirm the parser reset and image
release. Exercise an embedded NIF and its controls, switch away/back, then close
or change its owner. Extract before opening and after parse failure to new
destinations, and confirm an existing destination remains unchanged. Test the
table and explicit actions at the default narrow Inspector layout, including
keyboard activation.

The application also accepts ordinary file paths as command line arguments, but
retail files are never required for tests or smoke tests.

## Check folder browsing

Filesystem browsing has a separate GPU free test target,
`rerevved_source_browser_tests`, which does not link the parser/core library.
Its semantic checks cover immediate child enumeration, lazy discovery, bounded
partial listings, directory errors/retry, cancellation, stale results after root
replacement or shutdown, reparse blocking, and source immutability. Controlled
enumerators hold and release completions deterministically rather than relying
on timing races. The normal verification script runs this target through CTest.

For a browsing desktop check, use a wholly synthetic folder with a regular
asset, a malformed asset, nested directories, and a junction or symbolic link to
another synthetic directory. Browse the root, highlight files, and expand only
one child. Confirm no document opens until Open or file double-click. Exercise
directory errors and Retry, root replacement, Cancel, the visible partial
listing notice for a large directory, blocked links and linked ancestors, and
keyboard activation at the default narrow Sources layout. Keep an opened
document while changing roots. Compare source bytes before and after.
Deterministic test coverage establishes late completion behavior that a fast
local desktop browse may finish too quickly to reproduce manually.

## Measure folder browsing

To reproduce browsing measurements, use an absolute synthetic output base:

```powershell
.\out\build\win-amd64-debug\rerevved_source_browser_tests.exe --measure C:\scratch\tools-browse-measure
```

The utility creates a new owned child, writes 1 byte files in directories of
1,000, 10,000, and 50,000 entries, and removes only that owned fixture. It
reports single run end to end time, accumulated UI Poll/apply time, retained
count, partial status, and reason. The full profile raises test only limits. The
default profile uses the shipped limits in
[Architecture](architecture.md#filesystem-discovery). Filesystem caching, path
length, and machine load affect these measurements.

Single run Windows synthetic measurements at 1,000, 10,000, and 50,000 files
retained 608-756 entries with the defaults and used 0.56-0.75 ms in Poll/apply.
Full 50,000 entry application took 45.53 ms with test only raised limits. These
measurements selected a bounded partial listing rather than applying a full
large directory to the UI. They are not filesystem deadlines or frame rate
guarantees.

## Content and result policy

- Use only wholly synthetic repository owned bytes and generated fixtures.
- Do not add retail files, extracted assets, captures, machine paths, bulk
  analysis output, or build output.
- Keep parser, document, and pure UI helper tests GPU free and window free.
- Compare source bytes and any existing extraction destination before and after
  checks that could expose a mutation or overwrite regression.
- Report configure, build, CTest, app smoke, formatting, and whitespace checks
  separately when any stage fails or is skipped.
