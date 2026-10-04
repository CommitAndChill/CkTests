# CkOnline interactive editor gate

These two single-player gyms test the internal service host with a deliberately
fake backend: completion, cancellation, timeout, stale callbacks, and lifetime
across map travel. They send no network traffic. To test actual Host/Join between
two instances, use the [Online panel guide](ONLINE_PANEL_EDITOR_TESTING.md).

**Joining:** these fake-backend gyms use one player; there is no second-player join step.

Status: **automated gate green; manual editor verification pending**. The 2026-10-02 local build and test run passed in `Saved/Logs/OnlineHostGate-05.log`; the final fresh test-only run after updating gym controls recorded 15 green tests (14 native and the AS smoke), zero failures, in `Saved/Logs/OnlineHostGate-06.log`. The steps below are expected results, not a record of a completed manual run.

Use the CkPlugins editor with `Plugins/CkTests/Content/TestGyms/TestGyms_CkTests_Level.umap` open. Start a single-player PIE session. Press **Tab** to open the gym switchboard and select **Online Requests** or **Online Lifetime** from the `CkOnline` category. Press **H** if the control panel is hidden. Both gyms show `Verdict`; Online Lifetime also shows `Reentrant verdict` and `Generic verdict`. Do not use separate PIE sessions for the travel check: a new PIE session creates a new GameInstance.

## Online Requests: deferred operations and stale completions

1. Select **Online Requests**. Press **X** to stop any prior service, then **Z** to reset the test record, then **J** to start. Expect `State = Running`, a nonzero generation, and `Queue / pending = 0 / 0` after a frame.
2. Press **I**. Expect `Completed = 1`, `Observed completions / success = 1 / 1`, and the adapter execute count to increase. This is the deliberately inline fake callback; the service must still publish exactly one deferred completion.
3. Press **K**, wait until `Pending = 1`, then **R**. Expect `Completed = 2`, `Pending = 0`, and observed successes `= 2`. Press **O** to replay the same callback. Expect `Ignored callbacks` to increase by one while completed and observed completion counts stay fixed.
4. Press **T**, wait at least 0.3 seconds for `TimedOut = 1`, then press **L**. Expect a second increase in `Ignored callbacks` and no new completion.
5. Press **K**, wait until `Pending = 1`, then **N**. Expect `Cancelled = 1`, `Completed = 4`, adapter cancel count to increase, and the panel `Verdict` to read **PASS**. The success, cancel, and timeout counts together must account for four outcomes. Check the Output Log for script errors and ensures.

The buttons use the real `UCk_Utils_OnlineService_UE` request API. The fake adapter and observed callback counters live in a GameInstance test subsystem, so map travel does not erase them.

## Online Lifetime: travel and reentrant shutdown

1. Select **Online Lifetime**. Press **X**, **Z**, then **J**. Press **P** to pin the current generation. Note the displayed generation and tick count.
2. Press **Tab**, travel to **Online Requests**, then press **Tab** and return to **Online Lifetime** in the same PIE session. Expect `Visits since pin >= 2`, the same generation, a higher tick count, `State = Running`, and the first `Verdict` to read **PASS**. This exercises real map teardown and recreation of the GameMode and PlayerController while the service stays in the GameInstance.
3. Press **K** and wait until `Pending = 1`. Press **B** to arm stop on the next completion, then **I**. Expect the completion listener to stop the service: `State = Stopped`, `signal stop >= 1`, `Queued = Pending = 0`, `Cancelled >= 1`, and adapter shutdown count increased. Press **R** to deliver the held callback after stop. Wait a few frames; expect no additional observed completion. The `Reentrant verdict` should read **PASS**. It cannot pass in the release frame. Check the Output Log for ensures and script errors.
4. Press **J** to restart. Expect `State = Running` and a new generation. Press **R** again to deliver the old callback; expect no completion in the new generation. This verifies the callback's old ticket cannot complete new work.
5. Still in this new generation, press **K** and wait for `Pending = 1`. Press **G** to submit an inline success whose **generic request-completion delegate** stops the service. Expect `State = Stopped`, `generic stop >= 1`, `Queued = Pending = 0`, `Cancelled >= 1`, and adapter shutdown count at least two. Press **R** to deliver the held callback after that stop. Wait a few frames; expect the observed completion count unchanged and `Generic verdict = PASS`. This separate path covers shutdown from the generic completion after the domain signal has fired. Check the Output Log for ensures and script errors.

The lifetime gym's verdicts apply to the current service generation: shutdown invalidates the running-state condition of the travel verdict, and restarting invalidates the previous shutdown verdict. Confirm travel first, then the signal shutdown, then restart and confirm generic shutdown. A verdict returning to PENDING after moving to the next generation is expected.

## Blueprint surface check

In a disposable Blueprint graph in this editor session, use **Get Game Instance Subsystem** for `CkOnlineTest_Subsystem`, call **Request Start**, then **Get Service**. The test helper injects the fake adapter; core adapter startup is a native integration seam. Call **Request New Operation Id** and **Request Mark Operation Immediate** with that GUID. Use Ck Online **Bind To OnCompleted** before issuing the request, with a custom event taking the service handle and completion payload, binding policy **IgnorePayloadInFlight**, and post-fire behavior **DoNothing**. Call **Request Execute** with an `FCk_Request_OnlineService_Execute` containing the same GUID and a positive timeout. Read **Get Diagnostics** from the returned handle. Expect one success event and `Completed = 1`. Remove the temporary graph without saving its asset. This remains **pending** until observed; the repository does not include a new Blueprint test asset.

## Automated smoke

The 2026-10-02 UnrealToolbox gate ran the AS smoke green alongside 14 native tests; see `Saved/Logs/OnlineHostGate-05.log` for that run's evidence. In Session Frontend's Automation tab, refresh and filter for `Project.Functional Tests` and the `UCk_AutoTest_OnlineService_UtilsExecuteCompletes` label, or use the project-approved UnrealToolbox test runner with the corresponding filter to rerun it. This test starts the fake GameInstance service, submits an execute request through the reflected CkOnline utility, waits for the AS signal, reads handle diagnostics, unbinds, and stops the service. The interactive gyms and Blueprint surface check remain pending until observed in the editor.
