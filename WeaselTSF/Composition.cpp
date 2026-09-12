#include "stdafx.h"
#include "WeaselTSF.h"
#include "EditSession.h"
#include "ResponseParser.h"
#include "CandidateList.h"

#include <oleacc.h>

namespace {

constexpr DWORD kR21MotionGateMs = 75;

bool R21SameRect(const RECT& a, const RECT& b) {
  return a.left == b.left && a.top == b.top && a.right == b.right &&
         a.bottom == b.bottom;
}

bool R21SuccessfulRect(bool hasRect, HRESULT hr) {
  return hasRect && SUCCEEDED(hr);
}

bool R21ReadMsaaCaret(HWND expectedView,
                      RECT& caretRect,
                      HWND& focus,
                      HWND& root) {
  focus = nullptr;
  root = nullptr;
  caretRect = {};

  GUITHREADINFO info = {};
  info.cbSize = sizeof(info);
  if (!::GetGUIThreadInfo(0, &info) || !info.hwndFocus)
    return false;

  DWORD focusProcessId = 0;
  ::GetWindowThreadProcessId(info.hwndFocus, &focusProcessId);
  if (!focusProcessId || focusProcessId != ::GetCurrentProcessId())
    return false;

  const HWND focusRoot = ::GetAncestor(info.hwndFocus, GA_ROOT);
  const HWND foreground = ::GetForegroundWindow();
  const HWND foregroundRoot =
      foreground ? ::GetAncestor(foreground, GA_ROOT) : nullptr;
  if (!focusRoot || focusRoot != foregroundRoot)
    return false;

  if (expectedView) {
    const HWND viewRoot = ::GetAncestor(expectedView, GA_ROOT);
    if (viewRoot && viewRoot != focusRoot)
      return false;
  }

  using AccessibleObjectFromWindowFn =
      HRESULT(WINAPI*)(HWND, DWORD, REFIID, void**);
  static const auto accessibleObjectFromWindow = [] {
    HMODULE module = ::GetModuleHandleW(L"oleacc.dll");
    if (!module)
      module = ::LoadLibraryW(L"oleacc.dll");
    return module ? reinterpret_cast<AccessibleObjectFromWindowFn>(
                        ::GetProcAddress(module, "AccessibleObjectFromWindow"))
                  : nullptr;
  }();
  if (!accessibleObjectFromWindow)
    return false;

  IAccessible* rawAccessible = nullptr;
  const HRESULT objectHr = accessibleObjectFromWindow(
      info.hwndFocus, static_cast<DWORD>(OBJID_CARET), __uuidof(IAccessible),
      reinterpret_cast<void**>(&rawAccessible));
  if (FAILED(objectHr) || !rawAccessible)
    return false;

  com_ptr<IAccessible> accessible;
  accessible.Attach(rawAccessible);

  LONG left = 0;
  LONG top = 0;
  LONG width = 0;
  LONG height = 0;
  VARIANT child = {};
  child.vt = VT_I4;
  child.lVal = CHILDID_SELF;
  const HRESULT locationHr =
      accessible->accLocation(&left, &top, &width, &height, child);
  if (FAILED(locationHr) || width <= 0 || height <= 0)
    return false;

  RECT rootRect = {};
  if (!::GetWindowRect(focusRoot, &rootRect))
    return false;

  RECT current = {left, top, left + width, top + height};
  RECT intersection = {};
  if (!::IntersectRect(&intersection, &current, &rootRect))
    return false;

  caretRect = current;
  focus = info.hwndFocus;
  root = focusRoot;
  return true;
}

RECT R21TranslateRect(const RECT& source, LONG dx, LONG dy) {
  RECT result = source;
  result.left += dx;
  result.right += dx;
  result.top += dy;
  result.bottom += dy;
  return result;
}

}  // namespace

/* Start Composition */
class CStartCompositionEditSession : public CEditSession {
 public:
  CStartCompositionEditSession(com_ptr<WeaselTSF> pTextService,
                               com_ptr<ITfContext> pContext,
                               BOOL fCUASWorkaroundEnabled)
      : CEditSession(pTextService, pContext) {
    _fCUASWorkaroundEnabled = fCUASWorkaroundEnabled;
  }

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  BOOL _fCUASWorkaroundEnabled;
};

STDMETHODIMP CStartCompositionEditSession::DoEditSession(TfEditCookie ec) {
  HRESULT hr = E_FAIL;
  com_ptr<ITfInsertAtSelection> pInsertAtSelection;
  com_ptr<ITfRange> pRangeComposition;
  if (_pContext->QueryInterface(IID_ITfInsertAtSelection,
                                (LPVOID*)&pInsertAtSelection) != S_OK)
    return hr;
  if (pInsertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, NULL, 0,
                                                &pRangeComposition) != S_OK)
    return hr;

  com_ptr<ITfContextComposition> pContextComposition;
  com_ptr<ITfComposition> pComposition;
  if (_pContext->QueryInterface(IID_ITfContextComposition,
                                (LPVOID*)&pContextComposition) != S_OK)
    return hr;
  if ((pContextComposition->StartComposition(
           ec, pRangeComposition, _pTextService, &pComposition) == S_OK) &&
      (pComposition != NULL)) {
    _pTextService->_SetComposition(pComposition);

    /* set selection */
    TF_SELECTION tfSelection;
    pRangeComposition->Collapse(ec, TF_ANCHOR_END);
    tfSelection.range = pRangeComposition;
    tfSelection.style.ase = TF_AE_NONE;
    tfSelection.style.fInterimChar = FALSE;
    _pContext->SetSelection(ec, 1, &tfSelection);

    // The old composition's range is still visible while its asynchronous
    // end session is pending. Position only after the new composition has
    // actually been created, not from the response handler's stale range.
    _pTextService->_UpdateCompositionWindow(_pContext);
  }

  return hr;
}

void WeaselTSF::_StartComposition(com_ptr<ITfContext> pContext,
                                  BOOL fCUASWorkaroundEnabled) {
  _R19ResetPlacementProbeDiagnostics();
  com_ptr<CStartCompositionEditSession> pStartCompositionEditSession;
  pStartCompositionEditSession.Attach(
      new CStartCompositionEditSession(this, pContext, fCUASWorkaroundEnabled));
  _cand->StartUI();
  if (pStartCompositionEditSession != nullptr) {
    HRESULT hr;
    pContext->RequestEditSession(_tfClientId, pStartCompositionEditSession,
                                 TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
  }
}

/* End Composition */
class CEndCompositionEditSession : public CEditSession {
 public:
  CEndCompositionEditSession(com_ptr<WeaselTSF> pTextService,
                             com_ptr<ITfContext> pContext,
                             com_ptr<ITfComposition> pComposition,
                             BOOL clear = TRUE)
      : CEditSession(pTextService, pContext), _clear(clear) {
    _pComposition = pComposition;
  }

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  com_ptr<ITfComposition> _pComposition;
  BOOL _clear;
};

STDMETHODIMP CEndCompositionEditSession::DoEditSession(TfEditCookie ec) {
  /* Clear the dummy text we set before, if any. */
  if (_pComposition == nullptr)
    return S_OK;
  // Avoid null pointer dereference
  if (!_pTextService || !_pContext)
    return S_OK;

  com_ptr<ITfRange> pCompositionRange;
  // This session owns the composition being ended. The service may already
  // have no current composition, or may be composing new text by now.
  if (_pComposition->GetRange(&pCompositionRange) == S_OK &&
      pCompositionRange != nullptr) {
    _pTextService->_ClearCompositionDisplayAttributes(ec, _pContext,
                                                      pCompositionRange);
    if (_clear)
      pCompositionRange->SetText(ec, 0, L"", 0);
  }

  // Drop ownership before EndComposition(). Some applications notify
  // OnCompositionTerminated synchronously while the old composition ends.
  // Keeping it as the current composition makes that normal notification
  // look like an external abort and can clear a new Rime composition during
  // auto-commit.
  if (_pTextService && _pTextService->_IsCurrentComposition(_pComposition))
    _pTextService->_FinalizeComposition();
  _pComposition->EndComposition(ec);
  return S_OK;
}

void WeaselTSF::_EndComposition(com_ptr<ITfContext> pContext,
                                BOOL clear,
                                BOOL endUI) {
  CEndCompositionEditSession* pEditSession;
  HRESULT hr = E_FAIL;
  com_ptr<ITfComposition> pComposition = _pComposition;

  if (endUI)
    _cand->EndUI();
  if (!pContext || !pComposition)
    return;
  if ((pEditSession = new CEndCompositionEditSession(
           this, pContext, pComposition, clear)) != NULL) {
    const HRESULT requestHr = pContext->RequestEditSession(
        _tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
    // Once TSF accepts cleanup, stop blocking fresh input on the old object.
    // A synchronous session already finalized it; a deferred session owns
    // its own object and cleans only the captured range, even if a newer
    // composition has since started. A rejected request retains ownership.
    if (SUCCEEDED(requestHr) && SUCCEEDED(hr) &&
        _IsCurrentComposition(pComposition))
      _FinalizeComposition();
    pEditSession->Release();
  }
}

/* Get Text Extent */
class CGetTextExtentEditSession : public CEditSession {
 public:
  CGetTextExtentEditSession(com_ptr<WeaselTSF> pTextService,
                            com_ptr<ITfContext> pContext,
                            com_ptr<ITfContextView> pContextView,
                            com_ptr<ITfComposition> pComposition,
                            bool enhancedPosition)
      : CEditSession(pTextService, pContext) {
    _pContextView = pContextView;
    _pComposition = pComposition;
    _enhancedPosition = enhancedPosition;
  }

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  com_ptr<ITfContextView> _pContextView;
  com_ptr<ITfComposition> _pComposition;
  bool _enhancedPosition;
};

STDMETHODIMP CGetTextExtentEditSession::DoEditSession(TfEditCookie ec) {
  com_ptr<ITfInsertAtSelection> pInsertAtSelection;
  com_ptr<ITfRange> pRangeComposition;
  ITfRange* pRange;
  RECT rc;
  BOOL fClipped;
  TF_SELECTION selection;
  ULONG nSelection;

  if (FAILED(_pContext->QueryInterface(IID_ITfInsertAtSelection,
                                       (LPVOID*)&pInsertAtSelection)))
    return E_FAIL;
  if (FAILED(_pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection,
                                     &nSelection)))
    return E_FAIL;

  if (_pComposition != nullptr && _pComposition->GetRange(&pRange) == S_OK) {
    pRange->Collapse(ec, TF_ANCHOR_START);
  } else {
    // composition end
    // note: selection.range is always an empty range
    pRange = selection.range;
  }

  if ((_pContextView->GetTextExt(ec, pRange, &rc, &fClipped)) == S_OK &&
      (rc.left != 0 || rc.top != 0)) {
    // get the foreground window pos and check if rc from GetTextExt is out of
    // window
    if (_enhancedPosition) {
      HWND hwnd;
      RECT rcForegroundWindow;
      hwnd = GetForegroundWindow();
      ::GetWindowRect(hwnd, &rcForegroundWindow);

      if (rc.left < rcForegroundWindow.left ||
          rc.left > rcForegroundWindow.right ||
          rc.top < rcForegroundWindow.top ||
          rc.top > rcForegroundWindow.bottom) {
        POINT pt;
        bool hasCaret = ::GetCaretPos(&pt);
        int offsetx = rcForegroundWindow.left - rc.left + (hasCaret ? pt.x : 0);
        int offsety = rcForegroundWindow.top - rc.top + (hasCaret ? pt.y : 0);
        rc.left += offsetx;
        rc.right += offsetx;
        rc.top += offsety;
        rc.bottom += offsety;
      }
    }
    _pTextService->_SetCompositionPosition(rc);
  }
  return S_OK;
}

/* R19 diagnostic-only GetTextExt probe. It never updates candidate position. */
class CR19PlacementProbeEditSession : public CEditSession {
 public:
  CR19PlacementProbeEditSession(com_ptr<WeaselTSF> pTextService,
                                com_ptr<ITfContext> pContext,
                                com_ptr<ITfContextView> pContextView,
                                com_ptr<ITfComposition> pComposition,
                                DWORD generation)
      : CEditSession(pTextService, pContext),
        _pContextView(pContextView),
        _pComposition(pComposition),
        _generation(generation) {}

  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  com_ptr<ITfContextView> _pContextView;
  com_ptr<ITfComposition> _pComposition;
  DWORD _generation;
};

STDMETHODIMP CR19PlacementProbeEditSession::DoEditSession(TfEditCookie ec) {
  TF_SELECTION selection = {};
  ULONG fetched = 0;
  HRESULT hr = _pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection,
                                       &fetched);
  if (FAILED(hr) || fetched != 1 || !selection.range) {
    const HRESULT selectionHr = FAILED(hr) ? hr : E_FAIL;
    _pTextService->_R20CompleteRangeSourceProbe(
        _generation, selectionHr, nullptr, FALSE, E_UNEXPECTED, nullptr, FALSE,
        E_UNEXPECTED, nullptr, FALSE);
    _pTextService->_R19CompletePlacementProbe(_generation, selectionHr, nullptr,
                                              FALSE);
    return S_OK;
  }

  com_ptr<ITfRange> selectionRange;
  selectionRange.Attach(selection.range);

  RECT selectionRc = {};
  BOOL selectionClipped = FALSE;
  const HRESULT selectionHr = _pContextView->GetTextExt(
      ec, selectionRange, &selectionRc, &selectionClipped);

  com_ptr<ITfRange> compositionStartRange;
  RECT compositionStartRc = {};
  BOOL compositionStartClipped = FALSE;
  HRESULT compositionStartHr = E_UNEXPECTED;
  const bool haveCompositionStartRange =
      _pComposition != nullptr &&
      _pComposition->GetRange(&compositionStartRange) == S_OK;
  if (haveCompositionStartRange) {
    compositionStartRange->Collapse(ec, TF_ANCHOR_START);
    compositionStartHr = _pContextView->GetTextExt(ec, compositionStartRange,
                                                   &compositionStartRc,
                                                   &compositionStartClipped);
  }

  com_ptr<ITfRange> compositionEndRange;
  RECT compositionEndRc = {};
  BOOL compositionEndClipped = FALSE;
  HRESULT compositionEndHr = E_UNEXPECTED;
  const bool haveCompositionEndRange =
      _pComposition != nullptr &&
      _pComposition->GetRange(&compositionEndRange) == S_OK;
  if (haveCompositionEndRange) {
    compositionEndRange->Collapse(ec, TF_ANCHOR_END);
    compositionEndHr = _pContextView->GetTextExt(
        ec, compositionEndRange, &compositionEndRc, &compositionEndClipped);
  }

  _pTextService->_R20CompleteRangeSourceProbe(
      _generation, selectionHr, SUCCEEDED(selectionHr) ? &selectionRc : nullptr,
      selectionClipped, compositionStartHr,
      SUCCEEDED(compositionStartHr) ? &compositionStartRc : nullptr,
      compositionStartClipped, compositionEndHr,
      SUCCEEDED(compositionEndHr) ? &compositionEndRc : nullptr,
      compositionEndClipped);

  // Preserve R19 source-selection semantics: while composing, use composition
  // START; only fall back to selection when GetRange fails.
  const HRESULT r19Hr =
      haveCompositionStartRange ? compositionStartHr : selectionHr;
  const RECT* r19Rc =
      haveCompositionStartRange
          ? (SUCCEEDED(compositionStartHr) ? &compositionStartRc : nullptr)
          : (SUCCEEDED(selectionHr) ? &selectionRc : nullptr);
  const BOOL r19Clipped =
      haveCompositionStartRange ? compositionStartClipped : selectionClipped;
  _pTextService->_R19CompletePlacementProbe(_generation, r19Hr, r19Rc,
                                            r19Clipped);
  return S_OK;
}

void WeaselTSF::_R19ResetPlacementProbeDiagnostics() {
  ++_r19ProbeGeneration;
  if (!_r19ProbeGeneration)
    ++_r19ProbeGeneration;
  _r19ProbePending = false;
  _r19ProbeRequests = 0;
  _r19ProbeSubmitted = 0;
  _r19ProbeCoalesced = 0;
  _r19ProbeCompleted = 0;
  _r19ProbeFailures = 0;
  _r19ProbeRectChanges = 0;
  _r19LayoutCallbacks = 0;
  _r19LayoutChanges = 0;
  _r19NormalPositionRequests = 0;
  _r19ProbeLastSubmitHr = S_OK;
  _r19ProbeLastSessionHr = S_OK;
  _r19ProbeLastTextExtHr = S_OK;
  _r19ProbeHasRect = false;
  _r19ProbeLastRect = {};
  _r19ProbeLastClipped = FALSE;
  _r19ProbeViewHwnd = nullptr;

  _r20SamplesCompleted = 0;
  _r20SelectionFailures = 0;
  _r20CompositionStartFailures = 0;
  _r20CompositionEndFailures = 0;
  _r20SelectionRectChanges = 0;
  _r20CompositionStartRectChanges = 0;
  _r20CompositionEndRectChanges = 0;
  _r20SelectionLastTextExtHr = S_OK;
  _r20CompositionStartLastTextExtHr = S_OK;
  _r20CompositionEndLastTextExtHr = S_OK;
  _r20SelectionHasRect = false;
  _r20CompositionStartHasRect = false;
  _r20CompositionEndHasRect = false;
  _r20SelectionLastRect = {};
  _r20CompositionStartLastRect = {};
  _r20CompositionEndLastRect = {};
  _r20SelectionLastClipped = FALSE;
  _r20CompositionStartLastClipped = FALSE;
  _r20CompositionEndLastClipped = FALSE;

  _R21ResetPlacementFollow();
}

void WeaselTSF::_R19PlacementProbeTick(com_ptr<ITfContext> pContext) {
  ++_r19ProbeRequests;
  if (_r19ProbePending) {
    ++_r19ProbeCoalesced;
    return;
  }
  if (!pContext || !_IsComposing()) {
    ++_r19ProbeFailures;
    _r19ProbeLastSubmitHr = E_UNEXPECTED;
    return;
  }

  com_ptr<ITfContextView> pContextView;
  const HRESULT viewHr = pContext->GetActiveView(&pContextView);
  if (FAILED(viewHr) || !pContextView) {
    ++_r19ProbeFailures;
    _r19ProbeLastSubmitHr = FAILED(viewHr) ? viewHr : E_FAIL;
    return;
  }
  HWND viewHwnd = nullptr;
  pContextView->GetWnd(&viewHwnd);
  _r19ProbeViewHwnd = viewHwnd;

  com_ptr<CR19PlacementProbeEditSession> pEditSession;
  pEditSession.Attach(new CR19PlacementProbeEditSession(
      this, pContext, pContextView, _pComposition, _r19ProbeGeneration));

  _r19ProbePending = true;
  ++_r19ProbeSubmitted;
  HRESULT sessionHr = E_FAIL;
  const HRESULT requestHr = pContext->RequestEditSession(
      _tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READ, &sessionHr);
  _r19ProbeLastSubmitHr = requestHr;
  _r19ProbeLastSessionHr = sessionHr;
  if (FAILED(requestHr) || (FAILED(sessionHr) && _r19ProbePending)) {
    _r19ProbePending = false;
    ++_r19ProbeFailures;
  }
}

void WeaselTSF::_R19CompletePlacementProbe(DWORD generation,
                                           HRESULT textExtHr,
                                           const RECT* rect,
                                           BOOL clipped) {
  if (generation != _r19ProbeGeneration)
    return;
  _r19ProbePending = false;
  ++_r19ProbeCompleted;
  _r19ProbeLastTextExtHr = textExtHr;
  _r19ProbeLastClipped = clipped;
  if (FAILED(textExtHr) || !rect) {
    ++_r19ProbeFailures;
    _r19ProbeHasRect = false;
    return;
  }
  if (_r19ProbeHasRect && (rect->left != _r19ProbeLastRect.left ||
                           rect->top != _r19ProbeLastRect.top ||
                           rect->right != _r19ProbeLastRect.right ||
                           rect->bottom != _r19ProbeLastRect.bottom))
    ++_r19ProbeRectChanges;
  _r19ProbeLastRect = *rect;
  _r19ProbeHasRect = true;
}

void WeaselTSF::_R20CompleteRangeSourceProbe(DWORD generation,
                                             HRESULT selectionHr,
                                             const RECT* selectionRect,
                                             BOOL selectionClipped,
                                             HRESULT compositionStartHr,
                                             const RECT* compositionStartRect,
                                             BOOL compositionStartClipped,
                                             HRESULT compositionEndHr,
                                             const RECT* compositionEndRect,
                                             BOOL compositionEndClipped) {
  if (generation != _r19ProbeGeneration)
    return;

  ++_r20SamplesCompleted;

  const auto updateSource = [](HRESULT hr, const RECT* rect, BOOL clipped,
                               HRESULT& lastHr, bool& hasRect, RECT& lastRect,
                               BOOL& lastClipped, DWORD& failures,
                               DWORD& rectChanges) {
    lastHr = hr;
    lastClipped = clipped;
    if (FAILED(hr) || !rect) {
      ++failures;
      hasRect = false;
      return;
    }
    if (hasRect &&
        (rect->left != lastRect.left || rect->top != lastRect.top ||
         rect->right != lastRect.right || rect->bottom != lastRect.bottom))
      ++rectChanges;
    lastRect = *rect;
    hasRect = true;
  };

  updateSource(selectionHr, selectionRect, selectionClipped,
               _r20SelectionLastTextExtHr, _r20SelectionHasRect,
               _r20SelectionLastRect, _r20SelectionLastClipped,
               _r20SelectionFailures, _r20SelectionRectChanges);
  updateSource(compositionStartHr, compositionStartRect,
               compositionStartClipped, _r20CompositionStartLastTextExtHr,
               _r20CompositionStartHasRect, _r20CompositionStartLastRect,
               _r20CompositionStartLastClipped, _r20CompositionStartFailures,
               _r20CompositionStartRectChanges);
  updateSource(compositionEndHr, compositionEndRect, compositionEndClipped,
               _r20CompositionEndLastTextExtHr, _r20CompositionEndHasRect,
               _r20CompositionEndLastRect, _r20CompositionEndLastClipped,
               _r20CompositionEndFailures, _r20CompositionEndRectChanges);
}

void WeaselTSF::_R21ResetPlacementFollow() {
  _r21LastR20Sample = 0;
  _r21HaveAuthoritativeStart = false;
  _r21AuthoritativeStart = {};
  _r21HaveTsfPair = false;
  _r21PairStart = {};
  _r21PairEnd = {};
  _r21HaveMsaaBaseline = false;
  _r21BaselineMsaa = {};
  _r21BaselineFocus = nullptr;
  _r21BaselineRoot = nullptr;
  _r21BaselineCorrectionX = 0;
  _r21BaselineCorrectionY = 0;
  _r21CorrectionX = 0;
  _r21CorrectionY = 0;
  _r21CorrectionActive = false;
  _r21MotionProvisional = false;
  _r21MotionProvisionalTick = 0;
  _r21MotionActive = false;
  _r21LastTextActivityTick = ::GetTickCount();
  _r21HaveNormalPosition = false;
  _r21LastNormalPosition = {};
  _r21HaveOutputPosition = false;
  _r21LastOutputPosition = {};
  _r21AuthoritativeRequeries = 0;
  _r21Rebases = 0;
  _r21MotionGates = 0;
  _r21MotionActivations = 0;
  _r21CorrectionUpdates = 0;
  _r21MsaaUnavailable = 0;
  _r21GuardResets = 0;
}

void WeaselTSF::_R21PlacementFollowTick(com_ptr<ITfContext> pContext) {
  if (!_IsComposing())
    return;

  const DWORD now = ::GetTickCount();
  const bool newSample = _r20SamplesCompleted != _r21LastR20Sample;
  if (newSample)
    _r21LastR20Sample = _r20SamplesCompleted;

  const bool startUsable = R21SuccessfulRect(_r20CompositionStartHasRect,
                                             _r20CompositionStartLastTextExtHr);
  const bool endUsable = R21SuccessfulRect(_r20CompositionEndHasRect,
                                           _r20CompositionEndLastTextExtHr);

  if (newSample && startUsable) {
    if (!_r21HaveAuthoritativeStart) {
      _r21AuthoritativeStart = _r20CompositionStartLastRect;
      _r21HaveAuthoritativeStart = true;
    } else if (!R21SameRect(_r21AuthoritativeStart,
                            _r20CompositionStartLastRect)) {
      _r21AuthoritativeStart = _r20CompositionStartLastRect;
      _r21CorrectionX = 0;
      _r21CorrectionY = 0;
      _r21CorrectionActive = false;
      _r21MotionProvisional = false;
      _r21MotionActive = false;
      _r21HaveMsaaBaseline = false;
      ++_r21AuthoritativeRequeries;
      if (pContext)
        _UpdateCompositionWindow(pContext);
    }
  }

  // A transient END/START layout failure is common while the host catches up
  // with typing. Freeze any existing correction rather than interpreting the
  // accessibility caret as viewport motion.
  if (!startUsable || !endUsable)
    return;

  const bool hadPair = _r21HaveTsfPair;
  const bool pairStartChanged =
      hadPair && !R21SameRect(_r21PairStart, _r20CompositionStartLastRect);
  const bool pairEndChanged =
      hadPair && !R21SameRect(_r21PairEnd, _r20CompositionEndLastRect);
  const bool pairChanged = !hadPair || pairStartChanged || pairEndChanged;

  RECT msaa = {};
  HWND focus = nullptr;
  HWND root = nullptr;
  const bool haveMsaa = R21ReadMsaaCaret(_r19ProbeViewHwnd, msaa, focus, root);
  if (!haveMsaa) {
    ++_r21MsaaUnavailable;
    _r21MotionProvisional = false;
    _r21MotionActive = false;
    return;
  }

  if (pairChanged) {
    _r21PairStart = _r20CompositionStartLastRect;
    _r21PairEnd = _r20CompositionEndLastRect;
    _r21HaveTsfPair = true;

    // A START change is authoritative and resets the correction above. An
    // END-only change is typing/caret motion inside the same composition; keep
    // the already-proven viewport correction and only rebase its MSAA witness.
    _r21BaselineMsaa = msaa;
    _r21BaselineFocus = focus;
    _r21BaselineRoot = root;
    _r21BaselineCorrectionX = _r21CorrectionX;
    _r21BaselineCorrectionY = _r21CorrectionY;
    _r21HaveMsaaBaseline = true;
    _r21MotionProvisional = false;
    _r21MotionActive = false;
    ++_r21Rebases;
    return;
  }

  if (!_r21HaveMsaaBaseline) {
    _r21BaselineMsaa = msaa;
    _r21BaselineFocus = focus;
    _r21BaselineRoot = root;
    _r21BaselineCorrectionX = _r21CorrectionX;
    _r21BaselineCorrectionY = _r21CorrectionY;
    _r21HaveMsaaBaseline = true;
    ++_r21Rebases;
    return;
  }

  if (focus != _r21BaselineFocus || root != _r21BaselineRoot) {
    _r21BaselineMsaa = msaa;
    _r21BaselineFocus = focus;
    _r21BaselineRoot = root;
    _r21BaselineCorrectionX = 0;
    _r21BaselineCorrectionY = 0;
    _r21CorrectionX = 0;
    _r21CorrectionY = 0;
    _r21CorrectionActive = false;
    _r21MotionProvisional = false;
    _r21MotionActive = false;
    ++_r21Rebases;
    ++_r21GuardResets;
    if (pContext)
      _UpdateCompositionWindow(pContext);
    return;
  }

  const LONG motionX = msaa.left - _r21BaselineMsaa.left;
  const LONG motionY = msaa.top - _r21BaselineMsaa.top;
  if (motionX == 0 && motionY == 0) {
    _r21MotionProvisional = false;
    _r21MotionActive = false;
    if (_r21CorrectionX == _r21BaselineCorrectionX &&
        _r21CorrectionY == _r21BaselineCorrectionY)
      return;

    _r21CorrectionX = _r21BaselineCorrectionX;
    _r21CorrectionY = _r21BaselineCorrectionY;
    _r21CorrectionActive = _r21CorrectionX != 0 || _r21CorrectionY != 0;
    ++_r21CorrectionUpdates;
    const RECT source = _r21HaveNormalPosition ? _r21LastNormalPosition
                                               : _r20CompositionStartLastRect;
    _SetCompositionPosition(source);
    return;
  }

  // Input can move the MSAA caret tens of milliseconds before the host's TSF
  // END extent changes. During that window keep the current correction frozen.
  if ((now - _r21LastTextActivityTick) < kR21MotionGateMs) {
    _r21MotionProvisional = false;
    _r21MotionActive = false;
    return;
  }

  if (!_r21MotionActive) {
    if (!_r21MotionProvisional) {
      _r21MotionProvisional = true;
      _r21MotionProvisionalTick = now;
      ++_r21MotionGates;
      return;
    }
    if ((now - _r21MotionProvisionalTick) < kR21MotionGateMs)
      return;
    _r21MotionProvisional = false;
    _r21MotionActive = true;
    ++_r21MotionActivations;
  }

  const LONG nextCorrectionX = _r21BaselineCorrectionX + motionX;
  const LONG nextCorrectionY = _r21BaselineCorrectionY + motionY;
  if (nextCorrectionX == _r21CorrectionX && nextCorrectionY == _r21CorrectionY)
    return;

  _r21CorrectionX = nextCorrectionX;
  _r21CorrectionY = nextCorrectionY;
  _r21CorrectionActive = _r21CorrectionX != 0 || _r21CorrectionY != 0;
  ++_r21CorrectionUpdates;

  const RECT source = _r21HaveNormalPosition ? _r21LastNormalPosition
                                             : _r20CompositionStartLastRect;
  _SetCompositionPosition(source);
}

void WeaselTSF::_R19PublishPlacementProbeDiagnostics(HWND hwnd) const {
  if (!hwnd)
    return;
  const auto publish = [hwnd](const wchar_t* name, ULONG_PTR value) {
    ::SetPropW(hwnd, name, reinterpret_cast<HANDLE>(value));
  };
  const auto publishSigned = [hwnd](const wchar_t* name, LONG value) {
    ::SetPropW(hwnd, name,
               reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(value)));
  };
  publish(L"WeaselR19ProbeVersion", 1);
  publish(L"WeaselR19ProbePulse", ::GetTickCount());
  publish(L"WeaselR19ProbeGeneration", _r19ProbeGeneration);
  publish(L"WeaselR19ProbePending", _r19ProbePending ? 1 : 0);
  publish(L"WeaselR19ProbeRequests", _r19ProbeRequests);
  publish(L"WeaselR19ProbeSubmitted", _r19ProbeSubmitted);
  publish(L"WeaselR19ProbeCoalesced", _r19ProbeCoalesced);
  publish(L"WeaselR19ProbeCompleted", _r19ProbeCompleted);
  publish(L"WeaselR19ProbeFailures", _r19ProbeFailures);
  publish(L"WeaselR19ProbeRectChanges", _r19ProbeRectChanges);
  publish(L"WeaselR19LayoutCallbacks", _r19LayoutCallbacks);
  publish(L"WeaselR19LayoutChanges", _r19LayoutChanges);
  publish(L"WeaselR19NormalPositionRequests", _r19NormalPositionRequests);
  publish(L"WeaselR19ProbeLastSubmitHr",
          static_cast<DWORD>(_r19ProbeLastSubmitHr));
  publish(L"WeaselR19ProbeLastSessionHr",
          static_cast<DWORD>(_r19ProbeLastSessionHr));
  publish(L"WeaselR19ProbeLastTextExtHr",
          static_cast<DWORD>(_r19ProbeLastTextExtHr));
  publish(L"WeaselR19ProbeHasRect", _r19ProbeHasRect ? 1 : 0);
  publishSigned(L"WeaselR19ProbeLeft", _r19ProbeLastRect.left);
  publishSigned(L"WeaselR19ProbeTop", _r19ProbeLastRect.top);
  publishSigned(L"WeaselR19ProbeRight", _r19ProbeLastRect.right);
  publishSigned(L"WeaselR19ProbeBottom", _r19ProbeLastRect.bottom);
  publish(L"WeaselR19ProbeClipped", _r19ProbeLastClipped ? 1 : 0);
  publish(L"WeaselR19ProbeViewHwnd",
          reinterpret_cast<ULONG_PTR>(_r19ProbeViewHwnd));

  publish(L"WeaselR20ProbeVersion", 1);
  publish(L"WeaselR20SamplesCompleted", _r20SamplesCompleted);
  publish(L"WeaselR20SelectionFailures", _r20SelectionFailures);
  publish(L"WeaselR20CompositionStartFailures", _r20CompositionStartFailures);
  publish(L"WeaselR20CompositionEndFailures", _r20CompositionEndFailures);
  publish(L"WeaselR20SelectionRectChanges", _r20SelectionRectChanges);
  publish(L"WeaselR20CompositionStartRectChanges",
          _r20CompositionStartRectChanges);
  publish(L"WeaselR20CompositionEndRectChanges", _r20CompositionEndRectChanges);

  publish(L"WeaselR20SelectionLastTextExtHr",
          static_cast<DWORD>(_r20SelectionLastTextExtHr));
  publish(L"WeaselR20CompositionStartLastTextExtHr",
          static_cast<DWORD>(_r20CompositionStartLastTextExtHr));
  publish(L"WeaselR20CompositionEndLastTextExtHr",
          static_cast<DWORD>(_r20CompositionEndLastTextExtHr));

  publish(L"WeaselR20SelectionHasRect", _r20SelectionHasRect ? 1 : 0);
  publishSigned(L"WeaselR20SelectionLeft", _r20SelectionLastRect.left);
  publishSigned(L"WeaselR20SelectionTop", _r20SelectionLastRect.top);
  publishSigned(L"WeaselR20SelectionRight", _r20SelectionLastRect.right);
  publishSigned(L"WeaselR20SelectionBottom", _r20SelectionLastRect.bottom);
  publish(L"WeaselR20SelectionClipped", _r20SelectionLastClipped ? 1 : 0);

  publish(L"WeaselR20CompositionStartHasRect",
          _r20CompositionStartHasRect ? 1 : 0);
  publishSigned(L"WeaselR20CompositionStartLeft",
                _r20CompositionStartLastRect.left);
  publishSigned(L"WeaselR20CompositionStartTop",
                _r20CompositionStartLastRect.top);
  publishSigned(L"WeaselR20CompositionStartRight",
                _r20CompositionStartLastRect.right);
  publishSigned(L"WeaselR20CompositionStartBottom",
                _r20CompositionStartLastRect.bottom);
  publish(L"WeaselR20CompositionStartClipped",
          _r20CompositionStartLastClipped ? 1 : 0);

  publish(L"WeaselR20CompositionEndHasRect", _r20CompositionEndHasRect ? 1 : 0);
  publishSigned(L"WeaselR20CompositionEndLeft",
                _r20CompositionEndLastRect.left);
  publishSigned(L"WeaselR20CompositionEndTop", _r20CompositionEndLastRect.top);
  publishSigned(L"WeaselR20CompositionEndRight",
                _r20CompositionEndLastRect.right);
  publishSigned(L"WeaselR20CompositionEndBottom",
                _r20CompositionEndLastRect.bottom);
  publish(L"WeaselR20CompositionEndClipped",
          _r20CompositionEndLastClipped ? 1 : 0);

  publish(L"WeaselR21FollowVersion", 1);
  publish(L"WeaselR21AuthoritativeRequeries", _r21AuthoritativeRequeries);
  publish(L"WeaselR21Rebases", _r21Rebases);
  publish(L"WeaselR21MotionGates", _r21MotionGates);
  publish(L"WeaselR21MotionActivations", _r21MotionActivations);
  publish(L"WeaselR21CorrectionUpdates", _r21CorrectionUpdates);
  publish(L"WeaselR21MsaaUnavailable", _r21MsaaUnavailable);
  publish(L"WeaselR21GuardResets", _r21GuardResets);
  publish(L"WeaselR21CorrectionActive", _r21CorrectionActive ? 1 : 0);
  publish(L"WeaselR21MotionProvisional", _r21MotionProvisional ? 1 : 0);
  publish(L"WeaselR21MotionActive", _r21MotionActive ? 1 : 0);
  publishSigned(L"WeaselR21CorrectionX", _r21CorrectionX);
  publishSigned(L"WeaselR21CorrectionY", _r21CorrectionY);
  publish(L"WeaselR21HaveOutput", _r21HaveOutputPosition ? 1 : 0);
  publishSigned(L"WeaselR21OutputLeft", _r21LastOutputPosition.left);
  publishSigned(L"WeaselR21OutputTop", _r21LastOutputPosition.top);
  publishSigned(L"WeaselR21OutputRight", _r21LastOutputPosition.right);
  publishSigned(L"WeaselR21OutputBottom", _r21LastOutputPosition.bottom);
}

/* Composition Window Handling */
BOOL WeaselTSF::_UpdateCompositionWindow(com_ptr<ITfContext> pContext) {
  ++_r19NormalPositionRequests;
  com_ptr<ITfContextView> pContextView;
  if (pContext->GetActiveView(&pContextView) != S_OK)
    return FALSE;
  com_ptr<CGetTextExtentEditSession> pEditSession;
  pEditSession.Attach(
      new CGetTextExtentEditSession(this, pContext, pContextView, _pComposition,
                                    _cand->style().enhanced_position));
  if (pEditSession == NULL) {
    return FALSE;
  }
  HRESULT hr;
  pContext->RequestEditSession(_tfClientId, pEditSession,
                               TF_ES_ASYNCDONTCARE | TF_ES_READ, &hr);
  return SUCCEEDED(hr);
}

void WeaselTSF::_SetCompositionPosition(const RECT& rc) {
  /* Test if rect is valid.
   * If it is invalid during CUAS test, we need to apply CUAS workaround */
  if (!_fCUASWorkaroundTested) {
    _fCUASWorkaroundTested = TRUE;
    if (rc.top == rc.bottom) {
      _fCUASWorkaroundEnabled = TRUE;
      return;
    }
  }
  RECT _rc;
  _rc.left = _rc.right = rc.left;
  _rc.top = _rc.bottom = rc.bottom;

  const bool normalChanged =
      _r21HaveNormalPosition && !R21SameRect(_r21LastNormalPosition, rc);
  if (_r21CorrectionActive && normalChanged) {
    // The normal TSF path itself has obtained a new anchor. Prefer it
    // immediately and discard the accessibility correction rather than
    // translating an already-fresh position twice.
    _r21CorrectionX = 0;
    _r21CorrectionY = 0;
    _r21CorrectionActive = false;
    _r21MotionProvisional = false;
    _r21MotionActive = false;
    _r21HaveMsaaBaseline = false;
    ++_r21GuardResets;
  }

  _r21LastNormalPosition = rc;
  _r21HaveNormalPosition = true;
  const RECT output =
      _r21CorrectionActive
          ? R21TranslateRect(rc, _r21CorrectionX, _r21CorrectionY)
          : rc;
  _r21LastOutputPosition = output;
  _r21HaveOutputPosition = true;

  m_client.UpdateInputPosition(output);
  _cand->UpdateInputPosition(output);
}

/* Inline Preedit */
class CInlinePreeditEditSession : public CEditSession {
 public:
  CInlinePreeditEditSession(com_ptr<WeaselTSF> pTextService,
                            com_ptr<ITfContext> pContext,
                            com_ptr<ITfComposition> pComposition,
                            const std::shared_ptr<weasel::Context> context)
      : CEditSession(pTextService, pContext),
        _pComposition(pComposition),
        _context(context) {}

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  com_ptr<ITfComposition> _pComposition;
  const std::shared_ptr<weasel::Context> _context;
};

STDMETHODIMP CInlinePreeditEditSession::DoEditSession(TfEditCookie ec) {
  std::wstring preedit = _context->preedit.str;

  com_ptr<ITfRange> pRangeComposition;
  if (_pComposition == nullptr)
    return E_FAIL;
  if ((_pComposition->GetRange(&pRangeComposition)) != S_OK)
    return E_FAIL;

  if ((pRangeComposition->SetText(ec, 0, preedit.c_str(),
                                  static_cast<LONG>(preedit.length()))) != S_OK)
    return E_FAIL;

  /* TODO: Check the availability and correctness of these values */
  int sel_cursor = -1;
  for (size_t i = 0; i < _context->preedit.attributes.size(); i++) {
    if (_context->preedit.attributes.at(i).type == weasel::HIGHLIGHTED) {
      sel_cursor = _context->preedit.attributes.at(i).range.cursor;
      break;
    }
  }

  _pTextService->_SetCompositionDisplayAttributes(ec, _pContext,
                                                  pRangeComposition);

  /* Set caret */
  LONG cch;
  TF_SELECTION tfSelection;
  if (sel_cursor < 0) {
    pRangeComposition->Collapse(ec, TF_ANCHOR_END);
  } else {
    pRangeComposition->Collapse(ec, TF_ANCHOR_START);
    pRangeComposition->ShiftStart(ec, sel_cursor, &cch, NULL);
  }
  tfSelection.range = pRangeComposition;
  tfSelection.style.ase = TF_AE_NONE;
  tfSelection.style.fInterimChar = FALSE;
  _pContext->SetSelection(ec, 1, &tfSelection);

  return S_OK;
}

BOOL WeaselTSF::_ShowInlinePreedit(
    com_ptr<ITfContext> pContext,
    const std::shared_ptr<weasel::Context> context) {
  com_ptr<CInlinePreeditEditSession> pEditSession;
  pEditSession.Attach(
      new CInlinePreeditEditSession(this, pContext, _pComposition, context));
  if (pEditSession != NULL) {
    HRESULT hr;
    pContext->RequestEditSession(_tfClientId, pEditSession,
                                 TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
  }
  return TRUE;
}

/* Update Composition */
class CInsertTextEditSession : public CEditSession {
 public:
  CInsertTextEditSession(com_ptr<WeaselTSF> pTextService,
                         com_ptr<ITfContext> pContext,
                         com_ptr<ITfComposition> pComposition,
                         const std::wstring& text)
      : CEditSession(pTextService, pContext),
        _text(text),
        _pComposition(pComposition) {}

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  std::wstring _text;
  com_ptr<ITfComposition> _pComposition;
};

STDMETHODIMP CInsertTextEditSession::DoEditSession(TfEditCookie ec) {
  com_ptr<ITfRange> pRange;
  TF_SELECTION tfSelection;
  HRESULT hRet = S_OK;

  if (_pComposition == nullptr)
    return E_FAIL;
  if (FAILED(_pComposition->GetRange(&pRange)))
    return E_FAIL;

  if (FAILED(pRange->SetText(ec, 0, _text.c_str(),
                             static_cast<LONG>(_text.length()))))
    return E_FAIL;

  /* update the selection to an insertion point just past the inserted text. */
  pRange->Collapse(ec, TF_ANCHOR_END);

  tfSelection.range = pRange;
  tfSelection.style.ase = TF_AE_NONE;
  tfSelection.style.fInterimChar = FALSE;

  _pContext->SetSelection(ec, 1, &tfSelection);

  return hRet;
}

BOOL WeaselTSF::_InsertText(com_ptr<ITfContext> pContext,
                            const std::wstring& text) {
  CInsertTextEditSession* pEditSession;
  HRESULT hr;

  if ((pEditSession = new CInsertTextEditSession(this, pContext, _pComposition,
                                                 text)) != NULL) {
    pContext->RequestEditSession(_tfClientId, pEditSession,
                                 TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
    pEditSession->Release();
  }

  return TRUE;
}

void WeaselTSF::_UpdateComposition(com_ptr<ITfContext> pContext) {
  HRESULT hr;

  // Prevent input-driven MSAA movement from being classified as viewport
  // motion while the host's TSF END extent is still catching up.
  _r21LastTextActivityTick = ::GetTickCount();

  _pEditSessionContext = pContext;

  _pEditSessionContext->RequestEditSession(
      _tfClientId, this, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
  _async_edit = !!(hr == TF_S_ASYNC);
}

/* Composition State */
STDMETHODIMP WeaselTSF::OnCompositionTerminated(TfEditCookie ecWrite,
                                                ITfComposition* pComposition) {
  // NOTE:
  // This will be called when an edit session ended up with an empty composition
  // string, Even if it is closed normally. Silly M$.

  // EndComposition() may generate this callback for the composition we just
  // closed. Only an active, matching composition is an external termination.
  if (!_IsCurrentComposition(pComposition))
    return S_OK;

  // A host may terminate the empty TSF composition used for a non-inline
  // preedit. Keep Rime's composing state; the next key will create a fresh
  // TSF composition. Only an inactive Rime session should be aborted here.
  if (_status.composing) {
    _FinalizeComposition();
    return S_OK;
  }

  _AbortComposition();
  return S_OK;
}

void WeaselTSF::_AbortComposition(bool clear) {
  m_client.ClearComposition();
  if (_IsComposing()) {
    _EndComposition(_pEditSessionContext, clear);
  }
  _committed = TRUE;
  _cand->Destroy();
}

void WeaselTSF::_FinalizeComposition() {
  _pComposition = nullptr;
  ++_r19ProbeGeneration;
  if (!_r19ProbeGeneration)
    ++_r19ProbeGeneration;
  _r19ProbePending = false;
}

void WeaselTSF::_SetComposition(com_ptr<ITfComposition> pComposition) {
  _pComposition = pComposition;
}

BOOL WeaselTSF::_IsComposing() {
  return _pComposition != NULL;
}

BOOL WeaselTSF::_IsCurrentComposition(ITfComposition* pComposition) {
  return _pComposition != nullptr && _pComposition == pComposition;
}
