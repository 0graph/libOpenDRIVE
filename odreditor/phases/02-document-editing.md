# Phase 02 Document editing

Status: implemented. Dependencies: phase 01.

Document/history integration passes natively and with AddressSanitizer/UBSan
(leak detection is unavailable under the environment tracer). Chromium checks
cover property editing, raw XML, malformed drafts, undo, download, and reopen.

## Deliverable

Complete XML tree/attribute editing with preservation, stable selection, save, and undo/redo.

## Work

1. Implement XodrDocument using persistent pugixml ownership.
2. Retain unknown elements, attributes, ordering, comments, declarations, and CDATA.
3. Add internal NodeIds, safe lookup, and descendant invalidation.
4. Implement the raw mutation primitives listed in [Architecture](../ARCHITECTURE.md).
5. Add atomic grouped edits and inverse records; preserve identities through undo.
6. Connect tree, search, inspector, optional values, and child-record controls.
7. Add XML-fragment replacement with a parseable-draft/apply boundary.
8. Implement open, new, save, and modified-state handling.

## Edit behavior

- Unsupported elements receive generic controls.
- Source numbers remain strings until edited.
- Absence, empty values, and zero are separate states.
- Raw XML edits apply supplied structure without semantic corrections.
- Malformed drafts remain editable text; applying them does not destroy the previous parsed document.
- Semantic OpenDRIVE errors do not block saving.
- Normal ID-changing domain commands gain their correction behavior in phase 03; do not ship silent half-correct renames.

## Acceptance

- Add, duplicate, move, edit, and remove arbitrary subtrees.
- Save/reopen retains unrelated and unknown content in meaning.
- Deleted NodeIds fail lookup safely, including after another node reuses an XML ID.
- Undo/redo restores subtree order, optional attributes, and the relevant selection identity.
- One grouped action creates one history entry.
- No document is reconstructed from native road classes.

## Verification

Keep one compact preservation/history integration family. Cover a nested extension, CDATA, missing versus zero, subtree deletion, and atomic failure.
Use temporary UI checks for control wiring.
Do not create one test per attribute setter.

## References

[Edit rules](../EDIT_RULES.md), [Interface](../UI.md), [Engineering](../ENGINEERING.md).
