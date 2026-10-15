# Phase 03 Deterministic corrections

Status: implemented. Dependencies: phase 02.

Native and AddressSanitizer/UBSan checks cover signed lane remapping, all road-end
contact combinations, junction scopes, validity membership, duplication, atomic
ambiguity failures, and history. Chromium verifies corrected keyboard deletion,
save, and undo. Direct-junction lane corrections remain explicit raw XML edits.
Validity spans use `validLength` and repeat intervals; physical object dimensions
do not implicitly expand lane applicability.

## Deliverable

Normal edits maintain supported identities and references, including lane compaction across linked sections and roads.

## Work

1. Implement a reference index resolving known XML fields to NodeIds.
2. Add road/section/lane scope and road contact-point resolution.
3. Implement GetPredecessorLane, GetSuccessorLane, and scoped junction lane-link resolution.
4. Implement DeleteLane, InsertLane, ReorderLanes, and supported identity renames.
5. Resolve targets before editing; calculate remaps and deletions simultaneously.
6. Update affected reference fields and validity sets within one transaction.
7. Add defined reference-cleanup behavior for entity deletion and duplication.
8. Expose concise ambiguity diagnostics without modifying the document on failure.

## Required semantics

- Deleting right lane -1 compacts surviving right lanes in that section.
- Left lanes and neighboring sections retain their own numbering.
- Other lanes' links targeting a survivor follow its new ID.
- A survivor's outgoing link changes only when its target changes.
- A link to the deleted identity is removed; it must not attach to the lane inheriting the number.
- Resolve road-end orientation and junction mappings; do not infer targets from matching numbers.
- Preserve validity membership; reject a correction that cannot be represented safely.
- Preserve unknown reference-like attributes; do not perform global text replacement.

The complete policy and worked example live in [Edit rules](../EDIT_RULES.md).

## Acceptance

- Normal tree, inspector, and keyboard deletion use the same corrected command.
- Raw XML remains available for intentional incompatible edits.
- Unrelated existing errors do not trigger map-wide repair.
- Undo restores both the requested change and every automatic correction.
- Multi-ID edits cannot collide because of sequential substitution.

## Verification

Keep a table-driven remapping algorithm family covering both signs, road contact points, junction scopes, and deleted-target aliasing.
Add a small validity-range family and reuse phase 02 history integration.
Expected reference identities must be authored independently of the resolver under test.

## Out of scope

Curve alignment, traffic-lane inference, junction redesign, automatic routing around deletions.
