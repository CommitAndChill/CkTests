# Owned authored-session input and focus

The shared `UCk_UI_AuthoredSession_UE` now supports an optional CommonUI owner,
player-specific authored button focus/navigation and input suspension without
retiring the accepted view/model. This removes another native-only responsibility
from future code-authored menus. The actual Pause screen still uses its native
host; this proof establishes the shared seam before that migration.

See the [authoring API](../../../../CkFoundation/Source/CkWidgets/AUTHORING.md),
[session header](../../../../CkFoundation/Source/CkWidgets/Public/CkWidgets/Authoring/CkAuthoredSession.h),
[real PIE proof](../../../Source/CkTests/Private/UnitTests/CkYoga/Test_PauseMenuProof.cpp)
and [concrete CommonUI fixture](../../../Source/CkTests/Private/UnitTests/CkYoga/CkUiOwnedSession_TestOwner.h).

| Pipeline responsibility | Shared behavior | Focused evidence |
| --- | --- | --- |
| Admit a player's menu before activation | Freeze exact owner, LocalPlayer, world and existing Slate user; require a surface in that owner's tree | Invalid owner and foreign first-mount reject without publishing or replacing accepted views |
| Cover and uncover a menu | End input while retaining the model/view; begin again on the same active owner | Real CkUI stack covers the owner, suppresses all four callback kinds, allows model edits, then resumes the same view/revision |
| Focus nested authored buttons from script | Resolve current ordinary button IDs for the admitted user; wrap navigation and skip disabled controls | A second real local player routes Enter; focus changes preserve user zero; invalid ID sets/directions preserve focus |
| Survive focus reentry | Snapshot caller arguments; refresh attributes; recheck admission/view/revision/widget identity | Real Slate callbacks redirect focus, mutate the supplied ID and destroy/reset the supplied navigation array |
| Reload while preserving navigation | Use existing native stable-ID focus restoration | Accepted reload replaces the ordinary button and restores focus; rejected markup retains revision, widget and focus |
| Avoid held-input releases reaching gameplay after suspension | Weak native guard observes held gestures, then drains matching repeats/releases | Lower-priority observer receives no held owning-user key/pointer release; foreign-user releases remain available |
| Retire retained native controls | Disable dispatch before focus teardown; terminal weak owner release | Retained action/edit controls are inert after explicit shutdown and after owner/session GC |

The existing `Ck.UiAuthoring.PauseMenuProof.PIE` name contains these added
scenarios after its original Pause scenarios/captures. The second player is
created in a separate latent stage and receives normal unpaused world frames
before use. Its real player UI layer overlays the primary scene view using
temporary split-screen/widget-layer settings. Cleanup removes only the exact
test-owned player and Slate user, then restores both settings without saving
configuration. Two-view split-screen rendering is outside this proof.

## Verification

The focused fresh Development Editor gate passed **13/13 selected tests** under
the real renderer. Its [receipt](verification-receipt.json) names every test,
the frozen 47-file source snapshot, build/gate hashes and diagnostics. The final
startup/test log contained no Unreal errors, CK ensures or stale script errors.
The receipt separately records one Chromium USB property error and eleven CRL
verification messages.
Seven fresh compatible Pause captures and five card captures were inspected;
their dimensions/hashes are recorded from ignored `Saved` output. Existing
historical visual artifacts and receipts remain unchanged.

The original plan was one fresh boot. The first run passed twelve siblings and
failed the new fixture: its CSS used an unsupported element selector, and it
created/removed the second player in the same world frame, leaving nine deferred
relay actor constructions without their actors. The final fixture uses supported
class selectors and normal latent world frames. A separately reviewed caller
argument aliasing defect was repaired before the second boot. That run eliminated
the relay ensures, but revealed a discarded temporary Slate root retiring the
prepared session and an engine limitation when rendering two scene views. The
third gate lets CommonUI retain the root and overlays real player widget layers
on one scene view. Three fresh boots were used. Two earlier fixture compile
failures and all build attempts remain in the receipt;
no diagnostics were suppressed, no caches/artifacts were deleted and no user
Editor was terminated.

## Limits and next work

This gate invokes the new reflected API from C++. A real AngelScript menu
consumer remains the next proof. Ordinary button callbacks do not carry the
input user's identity, so complete foreign-user action rejection remains
unproven. The owning-user routing and foreign-user release tests establish only
their named behavior.

The fixture explicitly releases its session before layout teardown. Automatic
retirement after removal/clear/layout destruction, pause acquisition and prepared
Settings child rollback remain separate ownership work. The
[Pause migration plan](../menu-scripting-migration-plan.md) records the retained
parent requirement and the limits of sampled membership checks. Physical
controllers, packaged execution and loose-file packaged lookup remain unverified.

The subsequent [layer membership proof](../LayerMembershipProof/README.md)
verifies a shared admission-generation observer, including an actual AngelScript
delegate consumer, in its own focused gate. This input proof's receipt remains
the historical record of its earlier C++ scenarios; automatic session cleanup
and screen pause policy still require the actual Pause migration.

The [scalar card](../ScriptingCardProof/README.md) and
[default-font repair](../DefaultFontProof/README.md) retain their independent
historical receipts. This focused seam supports continued expansion of the
existing HTML/CSS pipeline; it does not establish a reason to deprecate it.
