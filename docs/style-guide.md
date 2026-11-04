# Style Guide

This guide defines the ReRevved Tools conventions for C++23, CMake, tests, UI
code, and repository documentation. Existing source may not yet follow every
rule, and its current state does not redefine a rule. This guide does not define
format semantics, ownership, user workflows, or evidence. Use the
[architecture](architecture.md), [file format support](file-formats.md),
[user guide](user-guide.md), and
[Evidence and claims](ai_agents/evidence-and-claims.md) as their canonical
contracts.

## Scope and layout

- `api/` contains public core headers for classification, inspection, parsing,
  document models, assembly, and extraction.
- `src/core/` implements those headers without Dear ImGui, GLFW, or OpenGL.
- `src/app/` owns ImGui state, drawing, formatting, navigation, previews, and
  pure layout helpers. Keep parser behavior out of this layer. The application
  shell sits at its root. `map/` holds map drafting and export, `inspect/` holds
  file inspection, `ui/` holds shared widgets and windows, and `smoke/` holds the
  smoke test support. Include these headers by their path from `src/app/`.
- `tests/` contains synthetic fixtures, parser checks, and GPU free UI helpers.
  `res/` contains project assets. Dependency licenses belong in `licenses/`.

Add source files to the matching target in `CMakeLists.txt`, or
`tests/rerevved_tools_tests.cmake` for tests, preserving its grouping and
ordering. Keep target include directories and links explicit. Dependencies use
the exact revisions recorded in `cmake/dependencies.cmake`. Dependency changes
also require a license review.

## C++

### Names and interfaces

- Use C++23 and the `rerevved::tools` namespace. Keep implementation helpers in
  an unnamed namespace in their `.cpp` file. Use lowercase `snake_case`
  filenames and namespaces. Use `UpperCamelCase` for types, scoped enum values,
  and namespaced/public functions. Use `lowerCamelCase` for variables,
  parameters, fields, and private helpers. Use `k` plus `UpperCamelCase` for
  constants (for example, `kMaximumZoom`). Unscoped enum values use
  `UPPER_SNAKE_CASE`.
- Keep names concise and semantic. Do not repeat the product or enclosing enum
  name when the type already supplies that context. Change public C++ APIs and
  their in repository consumers together. Preserve names owned by the standard
  library, dependencies, external decoder/UI interfaces, serialized keys,
  runtime output, error strings, filenames, and scripts' external names.
- Common verb prefixes are `Parse` (byte spans), `Load` (paths), `Inspect`
  (summaries), `Draw` (UI), `Format` (display text), `Calculate` (layout or
  geometry), `Encode` (bytes in memory), and `Write` (new files).
- Prefer fixed width integers for format data, `std::byte` for raw bytes,
  `std::span` for bounded views, and `std::filesystem::path` for paths. Mark
  nonoptional public results `[[nodiscard]]` and use `const` when it clarifies
  ownership. Match the pointer style: `Type* value` and `const Type& value`.

Keep public headers small and self sufficient: include direct needs, use
`#pragma once`, define document/error/result types in API headers, and keep
parsing helpers private to the implementation.

### Formatting and wrapping

Use four spaces, no tabs, Allman braces, left pointer alignment, sorted
includes, and no fixed column limit. The repository `.clang-format` implements
these rules and requires clang-format 22.x. Keep its settings consistent with
this guide and run its check on changed C++ files.

Keep the formatter's include order: matching local headers, other project
headers, then a blank line and standard or external headers. Vertically align
related consecutive declarations, including fields and local variables, and
retain assignment and inline comment alignment. Let the formatter align each
group. Do not add padding across unrelated groups. Let it choose continuation
indentation and wrapping. Long calls and aggregate initializers may span lines.

Use Allman braces for function bodies and control flow, including single
statement bodies. Apply this rule when editing a body. Existing unbraced guards
are not a separate preferred style. Use early returns when they make validation
or error handling flatter.

`.editorconfig` requires UTF-8, final newlines, and trimmed trailing whitespace.
C++ and documentation use LF. PowerShell scripts use CRLF.

### Comments and explanations

Use [Comments and prose](ai_agents/comments-and-prose.md) for comment placement,
wording, and documentation ownership. Use
[Evidence and claims](ai_agents/evidence-and-claims.md) for format observations
and provenance.

## Parser and document code

Follow [Architecture](architecture.md) for ownership and invariants and
[File format support](file-formats.md) for format specific contracts.

- `Parse*` accepts a bounded `std::span<const std::byte>` and returns
  `std::expected<T, Error>`. `Load*` owns path I/O and its existing public
  error form. Bound reads, arithmetic, allocations, offsets, references, and
  indices before use.
- Preserve exposed raw and unknown values. Skip unsupported data only after it
  is bounded. Never guess encodings, normalize values, repair input, or turn
  unsupported content into a successful parse.
- Keep structural parse, model assembly, and presentation failures distinct.
  Revalidate archive ranges for embedded parsing and extraction. Extraction
  creates only a new caller selected destination and never overwrites one.
- Leave source spans and source files unchanged. Do not use temporary retail
  files as a parsing shortcut.

Use typed error enums at parser and assembly boundaries. Keep each
`*ErrorMessage` mapping with its owner. Exact messages are observable behavior
and should have owning tests.

## Application and UI

Application code consumes parser produced values and does not reinterpret
unknown bytes or hide parse and assembly errors. Keep state owned by each opened
asset or archive, clearing selection dependent state on selection changes. Use
the shared Inspector and wireframe presentation for direct and embedded NIFs as
described by [Architecture](architecture.md).

Keep reusable GPU free calculations in small helpers such as inspection
formatting and NIF preview layout. Use available ImGui region sizes for
responsive rows and wrapped text. Preserve deliberate preview, spacing, and
navigation constants. Keep OpenGL lifetime in the DDS viewer. MP3, MAP, and NIF
preview data stays CPU side.

Use `Draw*` functions for ImGui presentation and small state structs for
selection, navigation, extraction, and preview. Keep byte escaping and numeric
formatting in presentation helpers. Add pure helper coverage for regressible
layout or state transitions.

## Tests and fixtures

Tests use the local `expect` helper and named `test*` functions. Register a new
test in its executable's `main()`. The
[test target table](testing.md#test-targets) identifies both CTest registrations
and their coverage.

- Use only synthetic repository owned bytes or reproducibly generated fixtures.
  Never add retail files, extracted assets, captures, decoder output, or bulk
  analysis output.
- For parser changes, cover success, malformed and truncated boundaries,
  unsupported variants, range and reference validation, exact errors, and source
  immutability.
- Cover document ownership, embedded routing, extraction refusal to overwrite,
  and parse/assembly/presentation state separation when those behaviors change.
- Keep parser, document, and pure UI helper tests GPU free and window free.
  Filesystem tests may use a temporary destination for an explicit I/O contract,
  but must remove it and leave no repository output.
- Prefer public behavior and semantic state over private implementation details.
  Do not add tests that only mirror current helper branches.

## Scripts, data, and provenance

The repository owns source, documentation, project assets, synthetic fixtures,
and reproducible scripts. Build output and fetched dependencies stay under
`out/build/<preset>` or CMake's configured content directories and are not
documentation or test inputs. Keep retail access bounded and read only.

Use the [Evidence and claims](ai_agents/evidence-and-claims.md) policy for
format semantics and title specific observations. Record a stable source, the
explicit input range, and limiting scope. Put settled facts in
[File format support](file-formats.md) and link to them elsewhere. Do not
promote machine local paths, captures, or bulk analysis output into tracked files.
Research manifests and analysis state belong in `rerevved-research`.

## Verification

Use [Development](development.md#build-and-check) for the verification commands
and [Testing](testing.md) for synthetic fixtures and desktop checks. State which
stages ran and which were skipped. Parser tests alone do not establish desktop
behavior.
