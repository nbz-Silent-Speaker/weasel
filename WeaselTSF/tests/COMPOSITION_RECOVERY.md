# Candidate recovery after an interrupted composition

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
