# Evidence and claims

ReRevved Tools records file behavior from public specifications, scoped
observations, and parser results. Retail inputs, extracted assets, captures,
decoder output, and bulk analysis state do not belong in this repository.

## What supports a claim

- A specification identifies its exact source and applicable format profile.
- An observation identifies the input range, tool or method, observed structure,
  and unresolved ambiguity.
- A parser result identifies the synthetic input, expected behavior, and
  boundary exercised. Tests establish behavior only for their inputs.
- A build or runtime claim identifies the command and result actually observed.
  Code inspection alone establishes implementation intent, not a completed run.

Extensions and magic values help identify a candidate format. They do not prove
its structure. Parsing one file does not establish support for every variant.
Automated output, summaries, and uncited notes are leads, not evidence.

## Keep the boundary visible

Make the narrowest supported claim. Distinguish standard format facts, title
specific observations, parser contracts, and runtime behavior. Preserve unknown
fields and conflicting interpretations until evidence resolves them.

Do not infer guest behavior from an asset layout. Executable analysis belongs in
`rerevved-research`. Runtime behavior belongs in `rerevved` or `rerevved-sdk`
according to ownership. The ReRevved Tools parser contract lives in
[File format support](../file-formats.md).

## Evidence and citations

Keep exact evidence locators where they support a claim. A public specification
citation does not widen the claim it supports.

Keep settled conclusions and reproducible methods, not raw evidence dumps or
machine local paths. Preserve source, reference, permission, and provenance
citations verbatim, including dates embedded in external citations. Preserve
addresses, hashes, measurements, and stable claim identifiers as well.
