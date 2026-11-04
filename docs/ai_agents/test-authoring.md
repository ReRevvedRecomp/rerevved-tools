# Test authoring

A test must catch a concrete defect or protect an uncovered contract.
Count fixture setup, runtime, and source volume before adding a case.

## When to add a test

- A change fixes a demonstrated defect. Use the narrowest case that fails
  without the fix.
- A change adds a branch, error message, byte layout, or saved map field that
  existing assertions do not cover.
- A parser gains a range or reference check. Cover the accepted boundary and the
  first rejected value.
- A writer can fail partway. Check that existing files survive and that a refused
  export writes nothing.
- A desktop flow broke in a way no helper can reach. Add a step to a scripted
  input smoke.

## When not to add a test

- Another value or format takes the same branch with the same assertions.
- Expected output comes from the function or table being checked.
- A guard returns before the behavior under test runs.
- The test mirrors a private helper's branches or only checks file names or source
  spelling.
- An app smoke only repeats a check a GPU free test already makes.

## Choose the cheapest fixture that proves the claim

Pick the first level that can expose the defect:

1. Literal bytes passed to a `Parse*` or `Encode*` function, as in the
   [scenario](../../tests/scenario_tests.cpp) and
   [FPK writer](../../tests/fpk_writer_tests.cpp) tests.
2. A GPU free model or helper, as in the
   [map editor](../../tests/map_editor_tests.cpp) and
   [creator workspace](../../tests/creator_tests.cpp) tests.
3. A temporary directory for an explicit file contract, as in the
   [map draft](../../tests/map_draft_tests.cpp) tests. Remove it before the test
   exits.
4. A whole export, as in the
   [map pack](../../tests/map_pack_export_tests.cpp) and
   [map suite](../../tests/map_suite_tests.cpp) tests. These are the slowest suites
   and take minutes in Debug. Add to them only when no smaller seam shows the
   defect.
5. A [scripted input smoke](../testing.md#scripted-input-smoke) in
   [tests/smoke](../../tests/smoke/), which needs a display and an OpenGL context.

A synthetic fixture or hidden window does not establish retail file or in game
behavior. [Evidence and claims](evidence-and-claims.md) defines that boundary.

## Products and matrices

Check every row of an authored table once at the data level. Choose integrated
representatives by distinct format, mip layout, size, or failure mechanism, and
keep literal expected values. Repeat faults only for distinct failure paths or
partial states. When reducing a matrix, state the lost coverage and retain its
data checks in the same change.

## Scaffolding

The [style guide](../style-guide.md#tests-and-fixtures) owns test structure and
the content allowed in fixtures. Reuse a suite's existing builders before writing
new ones. Share a header such as [synthetic_mp3.h](../../tests/synthetic_mp3.h)
only when another suite needs the same bytes. Keep separate behaviors in separate
`test*` functions, so that a failure names what broke.

## Registration and checks

Add a new test file to its target in
[rerevved_tools_tests.cmake](../../tests/rerevved_tools_tests.cmake). A new
suite needs its own executable and CTest name, and a row in the
[test target table](../testing.md#test-targets). Add a new smoke script to
[verify.ps1](../../scripts/verify.ps1), which runs each one explicitly.

[Development](../development.md#build-and-check) owns commands and prerequisites.
Select one suite with `ctest --preset win-amd64-debug -R <name>`. Report skipped
stages, and report synthetic checks and in game observations separately.
