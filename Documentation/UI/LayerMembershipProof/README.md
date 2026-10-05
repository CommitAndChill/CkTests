# CommonUI layer admission lifetime

`UCk_UI_LayerMembership_UE` observes one accepted admission to a player's CkUI
layer. Covering/uncovering the widget preserves that admission; removing and
re-pushing the same UObject creates a different admission. This supplies a
shared lifetime boundary for code-authored menus whose sessions must survive
covered deactivation. The actual Pause screen remains native in this slice.

The companion lives in CkUI, with no CkWidgets dependency. It does not own input,
pause, focus or the widget. See its
[header](../../../../CkFoundation/Source/CkUI/Public/CkUI/Layout/CkUI_LayerMembership.h),
[implementation](../../../../CkFoundation/Source/CkUI/Public/CkUI/Layout/CkUI_LayerMembership.cpp),
[stack generation hook](../../../../CkFoundation/Source/CkUI/Public/CkUI/CustomWidgets/WidgetStack/CkStack_Widget.cpp),
[native scenarios](../../../Source/CkTests/Private/UnitTests/CkYoga/CkUiLayerMembership_TestScenarios.h)
and [actual script probe](../../../Script/CkUiAuthoring/CkUiLayerMembership_Probe.as).

| Responsibility | Shared contract | Focused evidence |
| --- | --- | --- |
| Reject incomplete entry | Require game thread/Slate, realized owner already admitted to the exact player/world/layout/layer; failed admission publishes nothing and may be retried | Null/unadmitted owner and invalid tag reject before a valid retry |
| Retain a covered parent | Active status does not determine membership | Cover/uncover keeps the owner, cached root and generation |
| Distinguish pooled reuse | Generation is assigned before stack Slate construction/activation and stays monotonic across resource resets | Same-frame inherited remove/re-push and actual production class-pool reuse invalidate old admissions |
| Retire terminal membership | Pop/clear/displayed/layout signals reconcile identity; CoreTicker covers missed edges and resource resets | Removal, automatic CommonUI deactivation, paused-world clear, resource release and layout replacement retire once |
| Keep queries predictable | `Get_IsCurrent` checks exact weak identity and generation without callbacks | Foreign real-controller reassignment immediately rejects and cannot revive after reconciliation |
| Avoid stale callback cleanup | Invalidate/unsubscribe before deferred `OnRetired`; each successful watcher is terminal | Callback pushes a fresh admission, drops the old external root and runs GC; replacement stays current |
| Avoid retention leaks | Native callbacks/ticker are weak; owner/context/Slate references are weak | Unrooted watcher and removed owner collect; old watcher survives only through its callback self-keepalive |
| Reach scripts through reflection | Callable/pure/assignable native API, script-created retained UObject, script delegate | Property-based native `ProcessEvent` plus an actual AngelScript consumer observes, queries, receives retirement and stops |

## Consumer use

Prepare the widget and its authored session before pushing. After a successful
push, create and retain a fresh membership object, call `TryObserve`, and reject
the entry if it fails. Bind `OnRetired` to terminal cleanup. Always compare the
incoming membership object with the consumer's current object before touching
its session or pause ownership: a callback can create a newer admission.

While covered, suspend authored input and retain the session and pause lease.
Before resuming input or performing an action, use `Get_IsCurrent` to verify the
same admission. The query is immediate; retirement notification is deferred to
CoreTicker after invalidation/unsubscription. A missed stack edge requires one
reconciliation invocation and a later delivery invocation. Do not infer
immediate committed-removal notification or use callback timing to permit work
during that interval.

`Request_Stop` is silent, terminal and idempotent; it cancels pending retirement.
Successful observation cannot be restarted, including after retirement. A new
entry requires a new watcher. The caller must retain the watcher to receive its
deferred notification. Rejection before successful admission remains retryable.

## Verification

The focused fresh Development Editor gate passed **13/13 selected tests** with
the real renderer. The existing `Ck.UiAuthoring.PauseMenuProof.PIE` now contains
the native membership and actual AngelScript probe scenarios after its original
Pause and shared-input cases. Its real second player retains its own controller
and nonzero Slate user; widget layers overlay one rendered scene view using
temporary settings restored during cleanup.

The [receipt](verification-receipt.json) records the 54-file source snapshot,
build/gate hashes, exact selected tests and startup diagnostics. Twelve fresh
compatible Pause/card captures were inspected. Historical proof receipts and
tracked visual artifacts remain unchanged. One fresh Editor boot was planned
and used; the incremental build waited for the shared engine build lock.
The final startup/test log contained no Unreal errors, ensures, fatal errors or
stale script errors. One Chromium USB property error and fifteen CRL verification
messages are recorded separately in the receipt.

## Limits and next work

This exercises an actual AngelScript UObject consumer, rather than a migrated
Pause screen. It separately checks native Blueprint reflection metadata;
actual Widget Blueprint VM execution is not exercised. The observer itself
does not release authored sessions, control pause, remove widgets or restore
focus. The script Pause consumer and prepared native Settings child still need
their own policy and rollback tests.

The fixture uses zero-duration stack transitions. Nonzero transitions,
same-player controller replacement, cross-world travel and generation-counter
exhaustion have no direct runtime cases here. Physical controllers, packaged
execution, loose-file packaged lookup and two-view split-screen rendering remain
unverified. See the [migration plan](../menu-scripting-migration-plan.md) for the
next screen-level proof and the [shared input proof](../OwnedInputProof/README.md)
for the independently verified input/focus adapter.
