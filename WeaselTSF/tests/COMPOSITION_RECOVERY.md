# Candidate recovery after an interrupted composition

## UI lifecycle follow-up after CI83 failed user validation

The CI83 user video still shows inline letters without a candidate window
after deleting and retyping. Screenshot capture is only one trigger; recovery
must work after any interruption that destroys the candidate window.

The previous tests used a counter-only Candidate stub. They missed this valid
sequence: the host terminates the composition while Rime is composing;
OnCompositionTerminated drops the composition pointer; a later focus abort
skips _EndComposition and calls Destroy. Destroy formerly left _uiStarted true,
so the next StartUI skipped recreating the destroyed window.

Destroy and DestroyAll now end the UI lifecycle as well as destroying its
window. EndUI also resets local state when the host manager is unavailable.
The existing captured-range cleanup and BeginUIElement show policy remain.

CandidateLifecycleTests.h executes the extracted production StartUI, EndUI,
Destroy, DestroyAll, termination, and abort routines against a controlled host.
The composition-ending branch is explicitly rejected by this fixture; the
existing composition tests continue to cover it. Window operations and IPC
remain modeled, so this does not claim full Windows focus integration.
Tests cover repeated abort/restart, idempotent destruction, missing manager,
host-owned presentation, and failed-begin retry. The user confirms older
versions also had this problem; validation focuses on current recovery behavior
and does not run comparisons against historical implementations.

The video's exact internal callback order has not been captured. This repairs
a demonstrated source-level lifecycle defect consistent with the symptom;
user-managed installation and generic interruption/retype validation below
are still required before the reported problem can be marked resolved.

## Composition cleanup shipped in CI83

Baseline: cd02da5 (CI 82). Focus loss ends the candidate UI immediately, while
TSF may defer ending the composition. Retaining that composition as current
can prevent the next input from starting its candidate UI.

The earlier unshipped 9cb38db repeats the synchronous ownership reset reverted
in eab8f34 after host crashes. It cannot be shipped as-is: the delayed end
session called `_ClearCompositionDisplayAttributes`, which dereferenced the
service's current composition even after it had become null, or a different
composition. A null guard alone would still risk clearing the new range.

The revised change retrieves and retains the range from the end session's own
composition. Both attribute and text cleanup use this old range. Ownership is
released only once TSF accepts the end request; synchronous execution retains
its existing identity check before EndComposition. Late cleanup cannot clear
the new composition's attributes or reset its ownership. Rejected requests do
not discard current ownership. Existing text commit and candidate UI flags are
preserved. No Acrylic rendering, palette, or theme logic changes.

`PrepareCompositionTests.ps1` extracts the actual end-session, finalization,
identity, and attribute-cleanup routines into a generated test include. The
C++ tests compile those unchanged routines against a controlled host with real
ATL reference counting. They exercise synchronous/deferred cleanup, null current
state, a new active composition during delayed cleanup, reentrant end callbacks,
commit without UI teardown, unavailable ranges/properties, null inputs,
rejected scheduling, and object lifetime. CI runs x64 and Win32. These test
doubles do not replace host application testing or emulate a complete TSF store.

After user-managed installation and restart, verify in Codex and Word:

1. Enter nihao without selecting a candidate, then invoke and cancel screenshot
   capture. Return to the same input, delete any remaining preedit, and enter
   nihao once. The candidate window should return on that first new input.
2. Repeat the focus interruption and candidate hide/show several times. Confirm
   no application exit and no stale underline on newly committed text.
3. Check normal selection/commit, continued typing after automatic commit, and
   both inline and non-inline preedit configurations if available.
4. Confirm the accepted Acrylic appearance remains intact.

Do not call the user scenario resolved until the installed build passes those
checks. The historical 9cb38db is preserved locally, not merged or cherry-picked.
