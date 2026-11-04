# Architecture

ReRevved Tools is a content workbench. It separates file parsing, document
ownership, and desktop presentation. Source data remains read only, and parser
behavior can be tested without a GPU or window system.

## Product ownership

ReRevved Tools owns offline map and scenario drafting and inspection of
supported legally owned content. The [user guide](user-guide.md) describes the
pages and the requirements for export.

It provides archive inspection and extraction, transformed static wireframes,
and read only material and texture source inspection. External NIF texture
loading and normal dependent presentation are unsupported.

ReRevved Tools does not install, activate, launch, or replace game content.
ReRevved and the SDK own runtime replacement, overlay and filesystem precedence,
installation, launching, and runtime validation.

ReRevved Tools does not link generated guest code or inspect executable code or
live guest memory. Executable analysis belongs in `rerevved-research`. ReRevved
Tools implements the format contracts in this repository.

## Layers

### Core library

`rerevved_tools_core` owns file classification, parsers, document models, model
assembly, single entry extraction, texture import and validation, DDS encoding
(legacy, Xbox layouts, and the named map heights, blends and lightmap roles),
terrain generation from map tiles, asset pack writing and validation, map bundle
writing, and scenario XML and FPK writer APIs. It depends on neither Dear ImGui
nor GLFW/OpenGL. Writer availability in core does not imply that a creator pane
calls it.

- Each parser consumes an explicit byte range and produces a format specific
  document or a typed failure.
- FPK documents retain validated archive bytes and indexes. Embedded routing
  revalidates one selected range and invokes only the explicitly selected
  existing span parser.
- DDS and MP3 documents perform their supported CPU decoding without a GPU,
  window, audio device, or temporary retail file.
- NIF documents own all retained block metadata and geometry arrays. Model
  assembly produces ordered index only descriptors back into that document,
  validates renderability separately from parsing, and copies no geometry
  arrays.
- Extraction revalidates the selected source span and creates only a new caller
  selected destination.

Detailed byte layouts and evidence qualifications live in
[File format support](file-formats.md).

### Application

`rerevved-tools` owns Dear ImGui windows, opened document lifetime, selection,
navigation, layout, presentation formatting, and Preview interaction.

- A GPU free document workspace owns monotonic app IDs, lexical path reuse,
  ordered documents, deterministic active selection, and bounded in memory
  Activity. Typed paths, dropped files, and command line arguments use the same
  open operation.
- Each opened asset owns its generic file inspection, optional parsed documents,
  exact outcome diagnostic, and format specific UI state. A loader failure
  retains generic inspection rather than discarding the document.
- Application composes a fixed shell over a blue backdrop: a title band, a two
  row tab band, a centred page panel, a help strip, a prompt bar and notices. It
  selects one page (Map or Files) and routes the prompt bar's actions and keys
  to that page. The Files page hosts a tab free dockspace that cannot be
  undocked for InspectionPane's Sources, Preview and Inspector windows. Format
  viewers draw the active reference Preview content rather than separate
  windows. A small widget kit (`ui_widgets`) and theme (`ui_theme`, embedded
  Inter and Lucide fonts) give every page the same controls, and `native_dialog`
  wraps the Windows file and folder pickers with typed path fallbacks.
- CreatorWorkspace is a GPU free state machine. It tracks map revisions, checks
  navigation prerequisites, keeps results for the current revision, and maintains
  a separate identity for inspection.
- MapEditModel owns separate immutable source and editable byte snapshots,
  stroke history and selection. Scenario form helpers validate and encode/read
  back XML in memory. Readiness combines validation results tied to the current
  map revision.
  ExportPanel collects map pack settings, shows MapPackBlockers and records each
  ExportMapPack receipt in Activity. map_pack_export assembles dlc.fpk from the
  draft and writes it through the asset pack writer. The windowless
  `rerevved-map-suite` executable reuses these draft, image and export sources
  to write a suite of map packs from a plan file.
- Each archive owns its selected entry, explicit embedded format choice,
  extraction input/results, optional opened document, and embedded preview
  state.
- Direct and explicitly opened embedded NIFs share the same Inspector and
  wireframe presentation.
- Presentation reads parser produced values. It does not reinterpret unknown
  bytes or make a parse failure disappear.

### Preview resources

The DDS viewer is the only current asset preview that owns an OpenGL texture.
The application clears that resource while its context remains current before
destroying or replacing the owning document. MP3, MAP, and NIF previews use CPU
data and ImGui drawing only. NIF wireframes create no model GPU resources.

## Document ownership

| Owner | Retained state |
|---|---|
| Application | Creator workspace, inspection pane, creator view state, layout, the open map's file, recovery copies and the recent map list |
| Creator workspace | The map draft, its revisions, steps, scenario fields and artifact readiness |
| Inspection pane | Document workspace, reference dialogs, status and reference DDS viewer |
| Map editor / scenario editor / export panel | Editable MAP model/history, scenario selection/readiness inputs, destination plan and latest receipt |
| Sources | Session directory metadata, highlighted path, expansion state, and bounded enumeration requests |
| Document workspace | Stable app IDs, ordered assets, active selection, and latest 256 session outcomes |
| Asset document | File inspection plus optional format document, model result/error, and direct NIF navigation state |
| Archive document | FPK bytes/index, metadata filter and original index rows, selected entry, optional explicit format, extraction state, optional embedded document, and embedded NIF navigation state |
| NIF document | Validated block metadata, transforms, geometry, materials, texturing properties, and source carriers |
| NIF model | Ordered stable indices and composed static mesh transforms. No copied arrays or GPU state |

Parser failures, unsupported capabilities, preview decoding, NIF assembly, and
presentation failures have separate app statuses. DDS classification matches
complete canonical public error mappings. It uses no substring inference or UI
side parsing. Load errors without a public category remain unclassified, with
the verbatim diagnostic. GFX is metadata only and BIK has no preview capability.
Activity records outcomes independently of the current document and survives
closing that document for the remainder of the session.

Each archive retains one opened entry. Changing entry releases it and resets the
explicit parser choice to Choose parser. Changing top level selection preserves
each archive's state. Preview texture replacement and release run on the UI
thread with the GL context current, before drawing the replacement source.
Application shutdown destroys the DDS viewer before shutting down the context.

Closing or replacing one document discards only state owned by that document.
Separate files and archives do not share NIF mesh, projection, pan, zoom, opened
document, or extraction state.

Archive filtering caches conservative header hints and a source ordered vector
of original entry indices. It applies a case insensitive ASCII substring to
retained metadata only. It neither selects a parser nor opens entries. Previous
and Next follow matching original indices. Numeric jumps retain their original
index meaning even outside the filter. Filter changes preserve selected/opened
state. Selectable limits bound the app rows without narrowing the core index.
Hint discovery, filter updates, and explicit document opening currently run on
the UI thread. The CPU state helpers remain GPU free. Native handlers clear the
DDS viewer before releasing or replacing embedded state.

## Filesystem discovery

The app owned Sources browser enumerates directory metadata separately from
document loading. Only explicit file Open or double-click calls the existing
document workspace. Highlighting and expansion have no parser dependency.

One worker enumerates immediate children and returns values through bounded
queues. The UI thread owns the visible tree and applies matching results during
frame polling. Request identity changes on cancellation, retry, and root
replacement. Late results cannot populate a newer tree. Shutdown invalidates
requests before releasing UI state. Workers neither capture Application nor call
ImGui, GLFW, OpenGL, document loaders, or parsers.

Windows reparse attributes block junctions as well as symbolic links. The
non-Windows path uses link status. Ancestors and the requested directory are
checked before enumeration. Listed reparse points remain leaves, and explicit
enumeration rechecks the path rather than trusting cached metadata.

Cancellation and elapsed work limits are cooperative between filesystem calls.
An operating system call already in progress can still block. These limits do
not promise a deadline for an unresponsive filesystem. Limits bound retained
entries and queues, and partial results carry an explicit reason.

| Browsing limit | Default |
|---|---|
| Immediate children per result | 4,096 |
| Retained tree nodes | 32,768 |
| Native path storage in the retained tree and each result | 8 MiB |
| Outstanding requests and queued completion messages | 32 each |
| Cooperative enumeration work | 250 ms |

Path storage counts the bytes occupied by native path characters, separately
from node overhead and queue storage. One worker prevents concurrent scans. The
entry cap limits UI application cost, while the tree/path caps bound cached
metadata. See [Testing](testing.md#measure-folder-browsing) for the recorded
measurements and reproduction command.

## Map image generation

Application runs at most one background job that builds a map's heights, blends
and lightmap images from a copy of its tiles. The job touches no
CreatorWorkspace, ImGui or other shared state. The UI thread polls it while
the Map page draws and records the result only when the open map's tiles equal
the tiles the job started from. Any other result is discarded. A map opened
later with identical tiles may take it, because the images match. Map pack
blockers report missing images and images generated from other tiles as still
generating. Destroying Application waits for a running job, and smoke runs wait
for it every frame so scripts need no timing. The map suite generates
synchronously through GenerateMapTerrain.

## Parser and presentation boundaries

Parsing and presentation answer different questions:

- Parsing asks whether bytes satisfy the supported structural contract. It
  bounds every read, validates references required by that contract, preserves
  retained raw values, and requires exact selected block consumption where the
  format defines it.
- Model assembly asks whether a structurally valid NIF scene has supported,
  safely addressable geometry. Semantic renderability failures do not invalidate
  the parsed Inspector document.
- Presentation formats validated values, handles responsive layout and input,
  and owns preview transforms and resources. It does not add file format
  semantics.

External NIF texture source names illustrate the boundary. The parser retains
and validates their exact source carriers, while Inspector escapes the bytes.
ReRevved Tools does not implement title lookup roots, archive relationships, a
target texture format, or a loading policy.

## Invariants

Every change must preserve these constraints:

1. Source reads are bounded and nonmutating.
2. Extraction never overwrites an existing destination.
3. Retained unknown values remain observable and unsupported input fails
   explicitly. Data documented as outside the retained API may be checked and
   skipped. Nothing is guessed, normalized, or repaired.
4. Core parsing, document models, assembly, and extraction remain independent of
   ImGui, GLFW, and OpenGL.
5. A parser is covered by wholly synthetic repository owned tests and remains
   usable without a GPU or window system.
6. Retail files, extracted payloads, captures, and bulk analysis output are never
   repository dependencies.
7. Every field meaning not defined by a public standard carries its evidence
   locator. New format semantics require a documented observation of legally
   owned content or reproducible parser evidence, with scope and provenance
   preserved.

## Dependencies and warnings

Dear ImGui docking, GLFW, dr_libs (dr_mp3), and stb (PNG decoding and writing)
are fetched at exact revisions. The Inter and Lucide font files are fetched by
URL with a SHA-256 check and embedded in the executable. The core, application,
and tests use `/W4 /permissive-` under MSVC or `-Wall -Wextra -Wpedantic`
otherwise. Fetched targets keep their upstream warnings so dependency
diagnostics do not redefine the ReRevved Tools build contract.

See [Development](development.md) for configure, build, and verification
commands. See the [User guide](user-guide.md) for current application workflows.
