# Guidance for coding agents

Read these rules before making automated changes. The
[documentation index](../README.md) separates user guides from development and
technical references. Assisted changes follow the same rules as other changes,
and the contributor must be able to explain the source, scope, and limits of
every claim.

Read the [style guide](../style-guide.md) and
[Development](../development.md) before editing. These pages own commit,
verification, and pull request rules. Keep format parsing, model assembly, and
presentation within their [architectural boundaries](../architecture.md).

Keep retail inputs, extracted assets, captures, credentials, machine paths, build
output, prompts, session notes, and private repository references out of tracked
files. Preserve unrelated work, source attribution, and the addresses, offsets,
sizes, hashes, identifiers, and uncertainty that support a claim.

Each claim needs a useful source locator and a clear limit. A header hint, a
synthetic test, or generated prose does not establish unsupported format or
runtime behavior. A build proves compilation, not application or retail file
behavior. Report exactly which stages ran, failed, or were skipped. Read
[Evidence and claims](evidence-and-claims.md) and
[Comments and prose](comments-and-prose.md) for those boundaries. Read
[Test authoring](test-authoring.md) before adding or changing tests.
