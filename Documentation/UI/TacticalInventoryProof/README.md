# Tactical spatial inventory proof

Select **CkTests → CkUI → UI: Tactical Inventory**. The screen is a code-created
CommonUI UMG host backed by AngelScript model state, authored markup/styles and
the registered native `inventory-grid`. No Widget Blueprint is required.

Drag an item into a free rectangle in the stash, backpack or open document case.
R rotates a selected item or the active drag; E quick-transfers between stash and
pack; O opens a selected container; I/Escape closes the inventory. The gym info
panel exposes 1 open/close, 2 rotate, 3 transfer, 4 case, 5 reset, 6 source reload
and 7 invalid-overlap rejection. The left equipment pane is a loadout preview;
this proof's mutable model is the three storage containers.

The [reference and widget breakdown](reference-and-contract.md) records the design
target, anatomy, ownership and assets. [Verification](verification.md) records
the exact native gate and remaining limits.

## Shared pipeline addition

The grid owns cell geometry, atlas rendering, selection, pointer drag preview,
rotation and placement proposals. The model owns item/container identity and
acceptance. It validates complete record snapshots and keeps the last accepted
layout on malformed retained updates. Invalid current revisions suppress input.
Moves never mutate collection records inside the control. Cross-grid drags carry
weak document ownership and captured revisions; reload, release, changed records
and a foreign Slate user invalidate them.

The grammar and record schema are documented in CkFoundation's AUTHORING.md.
Use the canonical UI authoring rules before extending this proof.
