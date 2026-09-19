#include <windows.h>
#include <atlcomcli.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

template <typename T>
using com_ptr = CComPtr<T>;
using TfEditCookie = DWORD;
constexpr DWORD TF_ES_ASYNCDONTCARE = 0;
constexpr DWORD TF_ES_READWRITE = 6;
constexpr int GUID_PROP_ATTRIBUTE = 1;
int liveObjects = 0;

// Only the host operations called by the extracted production routines are
// modeled here. ATL smart pointers exercise their real AddRef/Release paths.
struct RefCounted : IUnknown {
  ULONG refs = 0;
  RefCounted() { ++liveObjects; }
  virtual ~RefCounted() { --liveObjects; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** result) override {
    if (!result)
      return E_POINTER;
    *result = static_cast<IUnknown*>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto remaining = --refs;
    if (!remaining)
      delete this;
    return remaining;
  }
};
struct ITfRange : RefCounted {
  std::wstring text = L"old preedit";
  int attributesCleared = 0;
  int textWrites = 0;
  HRESULT SetText(TfEditCookie, DWORD, const wchar_t* value, LONG length) {
    text.assign(value, length);
    ++textWrites;
    return S_OK;
  }
};
struct ITfProperty : RefCounted {
  HRESULT Clear(TfEditCookie, ITfRange* range) {
    ++range->attributesCleared;
    return S_OK;
  }
};
struct ITfComposition : RefCounted {
  com_ptr<ITfRange> range = new ITfRange;
  HRESULT rangeResult = S_OK;
  int ends = 0;
  std::function<void()> onEnd;
  HRESULT GetRange(ITfRange** result) {
    *result = nullptr;
    if (FAILED(rangeResult))
      return rangeResult;
    *result = range;
    if (*result)
      (*result)->AddRef();
    return S_OK;
  }
  HRESULT EndComposition(TfEditCookie) {
    ++ends;
    if (onEnd)
      onEnd();
    return S_OK;
  }
};
struct Candidate : RefCounted {
  int endCalls = 0;
  void EndUI() { ++endCalls; }
};
class CEditSession;
struct ITfContext : RefCounted {
  bool deferred = true;
  bool rejectRequest = false;
  bool failProperty = false;
  bool nullProperty = false;
  std::vector<com_ptr<CEditSession>> pending;
  HRESULT GetProperty(int, ITfProperty** result) {
    *result = nullptr;
    if (failProperty)
      return E_FAIL;
    if (!nullProperty) {
      *result = new ITfProperty;
      (*result)->AddRef();
    }
    return S_OK;
  }
  HRESULT RequestEditSession(DWORD, CEditSession*, DWORD, HRESULT*);
  void Drain();
};
class WeaselTSF : public RefCounted {
 public:
  com_ptr<ITfComposition> _pComposition;
  com_ptr<Candidate> _cand = new Candidate;
  DWORD _tfClientId = 1;
  DWORD _r19ProbeGeneration = 1;
  bool _r19ProbePending = false;
  void _EndComposition(com_ptr<ITfContext>, BOOL, BOOL = TRUE);
  void _ClearCompositionDisplayAttributes(TfEditCookie, ITfContext*, ITfRange*);
  void _FinalizeComposition();
  BOOL _IsComposing();
  BOOL _IsCurrentComposition(ITfComposition*);
};
class CEditSession : public RefCounted {
 public:
  CEditSession(com_ptr<WeaselTSF> service, com_ptr<ITfContext> context)
      : _pTextService(service), _pContext(context) {
    refs = 1;
  }
  virtual HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie) = 0;

 protected:
  com_ptr<WeaselTSF> _pTextService;
  com_ptr<ITfContext> _pContext;
};
HRESULT ITfContext::RequestEditSession(DWORD,
                                       CEditSession* session,
                                       DWORD,
                                       HRESULT* result) {
  if (rejectRequest) {
    *result = E_FAIL;
    return E_FAIL;
  }
  if (deferred) {
    pending.emplace_back(session);
    *result = S_OK;
  } else {
    *result = session->DoEditSession(1);
  }
  return S_OK;
}
void ITfContext::Drain() {
  while (!pending.empty()) {
    auto session = pending.front();
    pending.erase(pending.begin());
    session->DoEditSession(1);
  }
}

#include "obj/composition-under-test.inc"

void Check(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}

#include "CandidateLifecycleTests.h"
#include "PlacementFollowTests.h"
struct Fixture {
  com_ptr<WeaselTSF> service = new WeaselTSF;
  com_ptr<ITfContext> context = new ITfContext;
  com_ptr<ITfComposition> old = new ITfComposition;
  Fixture() { service->_pComposition = old; }
};
int main() {
  try {
    for (bool deferred : {false, true}) {
      Fixture f;
      f.context->deferred = deferred;
      f.old->onEnd = [&] {
        Check(!f.service->_IsCurrentComposition(f.old),
              "normal end callback must not abort the active composition");
      };
      f.service->_EndComposition(f.context, TRUE);
      Check(!f.service->_IsComposing(), "old ownership blocks fresh input");
      f.context->Drain();
      Check(f.old->range->text.empty() &&
                f.old->range->attributesCleared == 1 && f.old->ends == 1 &&
                f.service->_cand->endCalls == 1,
            "old text/attributes were not cleaned exactly once");
    }
    Check(liveObjects == 0, "synchronous/deferred cleanup leaked references");
    std::cout
        << "PASS: synchronous and deferred end with no current composition\n";
    {
      Fixture f;
      f.service->_EndComposition(f.context, TRUE);
      com_ptr<ITfComposition> fresh = new ITfComposition;
      fresh->range->text = L"new preedit";
      f.service->_pComposition = fresh;
      const auto generation = f.service->_r19ProbeGeneration;
      f.old->onEnd = [&] {
        Check(!f.service->_IsCurrentComposition(f.old), "stale end is active");
      };
      f.context->Drain();
      Check(f.service->_IsCurrentComposition(fresh) &&
                f.service->_r19ProbeGeneration == generation &&
                fresh->range->text == L"new preedit" &&
                fresh->range->attributesCleared == 0 && fresh->ends == 0,
            "old cleanup damaged new composition");
      Check(f.old->range->attributesCleared == 1 && f.old->range->text.empty(),
            "cleanup did not use the captured old range");
    }
    Check(liveObjects == 0, "replacement composition leaked references");
    std::cout
        << "PASS: delayed old cleanup preserves new text and attributes\n";
    {
      Fixture f;
      f.old->range->text = L"committed text";
      f.service->_EndComposition(f.context, FALSE, FALSE);
      f.context->Drain();
      Check(f.old->range->text == L"committed text" &&
                f.old->range->textWrites == 0 &&
                f.old->range->attributesCleared == 1 &&
                f.service->_cand->endCalls == 0,
            "auto-commit lost committed text or ended replacement UI");
    }
    std::cout << "PASS: auto-commit preserves text and candidate UI\n";
    for (int unavailable = 0; unavailable < 4; ++unavailable) {
      Fixture f;
      if (unavailable == 0)
        f.old->rangeResult = E_FAIL;
      if (unavailable == 1)
        f.old->range = nullptr;
      if (unavailable == 2)
        f.context->failProperty = true;
      if (unavailable == 3)
        f.context->nullProperty = true;
      f.service->_EndComposition(f.context, TRUE);
      f.context->Drain();
      Check(f.old->ends == 1, "unavailable range/property blocked end");
    }
    std::cout << "PASS: missing/failed ranges and attributes do not crash\n";
    {
      Fixture f;
      f.service->_ClearCompositionDisplayAttributes(1, f.context, nullptr);
      f.service->_ClearCompositionDisplayAttributes(1, nullptr, f.old->range);
      f.service->_EndComposition(nullptr, TRUE);
      Check(f.service->_IsCurrentComposition(f.old),
            "null context lost ownership");
      f.service->_pComposition = nullptr;
      f.service->_EndComposition(f.context, TRUE);
      Check(f.context->pending.empty(), "null composition queued an end");
    }
    Check(liveObjects == 0, "composition regression tests leaked references");
    std::cout << "PASS: null guards and retained-reference lifetime\n";
    {
      Fixture f;
      f.context->rejectRequest = true;
      f.service->_EndComposition(f.context, TRUE);
      Check(f.service->_IsCurrentComposition(f.old) &&
                f.context->pending.empty() && f.old->ends == 0,
            "rejected edit session discarded active composition");
    }
    Check(liveObjects == 0, "rejected edit session leaked references");
    std::cout << "PASS: rejected cleanup retains the active composition\n";
    candidate_recovery::Run();
    Check(liveObjects == 0, "candidate lifecycle tests leaked references");
    placement_follow::Run();
    Check(liveObjects == 0, "placement tests leaked references");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
