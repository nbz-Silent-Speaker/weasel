// The UI lifecycle and termination/abort routines below are extracted from
// production. Only the Windows host, window operations, and IPC are modeled.
namespace candidate_recovery {

struct ITfUIElementMgr : RefCounted {
  BOOL allowWindow = TRUE;
  bool failBegin = false;
  int begins = 0;
  int ends = 0;
  HRESULT BeginUIElement(void*, BOOL* show, DWORD* id) {
    ++begins;
    *show = allowWindow;
    *id = begins;
    return failBegin ? E_FAIL : S_OK;
  }
  HRESULT EndUIElement(DWORD) {
    ++ends;
    return S_OK;
  }
};
struct ITfThreadMgr : RefCounted {
  com_ptr<ITfUIElementMgr> manager = new ITfUIElementMgr;
  bool unavailable = false;
  HRESULT QueryInterface(ITfUIElementMgr** result) {
    *result = nullptr;
    if (unavailable)
      return E_FAIL;
    *result = manager;
    (*result)->AddRef();
    return S_OK;
  }
};
struct Window {
  bool exists = false;
  bool visible = false;
  int creates = 0;
  int currentStyle = 0;
  std::function<void(size_t*, size_t*, bool*, bool*)> callback;
  auto& uiCallback() { return callback; }
  void SetUICallBack(decltype(callback) value) { callback = value; }
  int& style() { return currentStyle; }
};
class WeaselTSF;
class CCandidateList : public RefCounted {
 public:
  explicit CCandidateList(WeaselTSF* service) : _tsf(service) {}
  WeaselTSF* _tsf;
  Window window;
  Window* _ui = &window;
  int _style = 0;
  bool _uiStarted = false;
  BOOL _pbShow = TRUE;
  DWORD uiid = 0;
  void StartUI();
  void EndUI();
  void Destroy();
  void DestroyAll();
  void Show(BOOL show) { window.visible = show && window.exists; }
  void _MakeUIWindow() {
    window.exists = true;
    ++window.creates;
  }
  void _DisposeUIWindow() {
    window.exists = false;
    window.visible = false;
  }
  void _DisposeUIWindowAll() { _DisposeUIWindow(); }
};
class WeaselTSF : public RefCounted {
 public:
  com_ptr<ITfThreadMgr> manager = new ITfThreadMgr;
  com_ptr<CCandidateList> _cand = new CCandidateList(this);
  com_ptr<ITfComposition> _pComposition;
  com_ptr<ITfContext> _pEditSessionContext;
  struct {
    bool composing = true;
  } _status;
  struct {
    int clears = 0;
    void ClearComposition() { ++clears; }
  } m_client;
  BOOL _committed = FALSE;
  DWORD _r19ProbeGeneration = 1;
  bool _r19ProbePending = false;
  com_ptr<ITfThreadMgr> _GetThreadMgr() { return manager; }
  void HandleUICallback(size_t*, size_t*, bool*, bool*) {}
  STDMETHODIMP OnCompositionTerminated(TfEditCookie, ITfComposition*);
  void _AbortComposition(bool clear = true);
  void _FinalizeComposition();
  BOOL _IsComposing();
  BOOL _IsCurrentComposition(ITfComposition*);
  void _EndComposition(com_ptr<ITfContext>, bool) {
    throw std::runtime_error(
        "UI recovery fixture unexpectedly ended an active composition");
  }
};
void UpdateAcrylicUiProbe(const void*, DWORD, HRESULT, BOOL) {}

#include "obj/candidate-under-test.inc"

void Run() {
  {
    com_ptr<WeaselTSF> service = new WeaselTSF;
    for (int repeat = 0; repeat < 3; ++repeat) {
      service->_cand->StartUI();
      Check(service->_cand->window.exists, "first new input did not create UI");
      com_ptr<ITfComposition> composition = new ITfComposition;
      service->_pComposition = composition;
      service->OnCompositionTerminated(1, composition);
      Check(!service->_IsComposing() && service->_cand->_uiStarted,
            "termination fixture did not leave the UI active");
      service->_AbortComposition();
      service->_AbortComposition();
      Check(!service->_cand->_uiStarted && !service->_cand->window.exists,
            "focus abort left a destroyed UI marked started");
    }
    service->_cand->StartUI();
    Check(service->_cand->window.creates == 4 &&
              service->manager->manager->begins == 4 &&
              service->manager->manager->ends == 3,
          "repeated interruptions did not recreate UI on the first start");
    service->_cand->EndUI();
  }
  std::cout << "PASS: host termination then repeated focus abort recovers on "
               "first start\n";
  for (bool all : {false, true}) {
    com_ptr<WeaselTSF> service = new WeaselTSF;
    auto candidate = service->_cand;
    candidate->StartUI();
    if (all) {
      candidate->DestroyAll();
      candidate->DestroyAll();
    } else {
      candidate->Destroy();
      candidate->Destroy();
    }
    Check(!candidate->_uiStarted && service->manager->manager->ends == 1,
          "destroy did not end the UI exactly once");
    candidate->StartUI();
    Check(candidate->window.exists && candidate->window.creates == 2,
          "destroy blocked next candidate creation");
    candidate->EndUI();
  }
  std::cout << "PASS: both destroy paths are idempotent and allow recreation\n";
  {
    com_ptr<WeaselTSF> service = new WeaselTSF;
    service->_cand->StartUI();
    service->manager->unavailable = true;
    service->_cand->Destroy();
    Check(!service->_cand->_uiStarted && !service->_cand->window.exists,
          "unavailable UI manager left stale startup state");
    service->manager->unavailable = false;
    service->_cand->StartUI();
    Check(service->_cand->window.exists,
          "UI manager recovery did not recreate window");
    service->_cand->EndUI();
  }
  std::cout << "PASS: unavailable UI manager does not block later recreation\n";
  {
    com_ptr<WeaselTSF> service = new WeaselTSF;
    service->manager->manager->allowWindow = FALSE;
    service->_cand->StartUI();
    service->_cand->Destroy();
    service->_cand->StartUI();
    Check(service->_cand->_uiStarted && !service->_cand->window.exists &&
              service->_cand->window.creates == 0,
          "recovery overrode the host's candidate presentation policy");
    service->_cand->EndUI();
    service->manager->manager->allowWindow = TRUE;
    service->manager->manager->failBegin = true;
    service->_cand->StartUI();
    Check(!service->_cand->_uiStarted, "failed begin marked UI started");
    service->manager->manager->failBegin = false;
    service->_cand->StartUI();
    Check(service->_cand->window.exists, "failed begin could not retry");
    service->_cand->EndUI();
  }
  std::cout << "PASS: host show policy and failed-begin retry remain intact\n";
}
}  // namespace candidate_recovery
