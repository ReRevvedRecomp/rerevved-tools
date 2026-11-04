# Comments and prose

Write for the owner or a contributor who has not followed the implementation.
Use direct sentences and describe current behavior, not intended future work.

## Wording

These rules cover UI text, comments, and documentation alike.

- Do not use semicolons in prose. Split the sentence or join it with a comma
  and a plain conjunction.
- Avoid hyphenated compounds where plain words read as well: "read only", "top
  right corner", "16 bit". Keep hyphens that belong to identifiers, file names,
  command line flags, quoted source text, and terms always spelled with one.
- Prefer short, plain sentences in UI text, which people read in passing.

## Comments

Explain a nonobvious reason, invariant, boundary, or source. Do not restate the
next statement in English. Put a shared explanation above the enum or field
group it qualifies. Reserve trailing comments for a local unit, sentinel, raw
value, or index.

Keep format evidence next to the contract it supports, with its exact locator.
Preserve unknown values and uncertainty. A plausible interpretation is not a
field name or parser rule. Longer explanations belong in the owning guide.

Do not put assignments, work logs, machine paths, or unsupported conclusions in
comments. A synthetic fixture may retain its generation command and hash when
needed for reproducibility.

## Documentation

- Use plain ASCII, direct verbs, and literal UI labels, paths, and commands.
- Wrap prose at about 80 columns. Keep lines under 100 columns except tables,
  URLs, and code. Leave a final newline and no trailing whitespace.
- Keep identifiers, addresses, measurements, hashes, and source citations exact.
- Describe success, failure, and unsupported behavior separately.
- Do not add roadmap promises, duplicated release notes, or assistant
  narratives.
- Link through the [documentation index](../README.md). Keep detailed workflow,
  format, architecture, and build facts in their respective guides.

A comment correction needs code or test evidence. A new format meaning needs
[evidence with an explicit scope](evidence-and-claims.md), not just clearer
wording. Review relative links and anchors whenever a heading or page moves.
