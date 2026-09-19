// Both production placement callbacks run against a deterministic host. The
// witness represents composition END, deliberately offset from START in X.
inline DWORD placementTestNow = 1000;
inline DWORD PlacementTestTick() {
  return placementTestNow;
}

namespace placement_follow {

struct Witness {
  RECT rect = {};
  HWND focus = reinterpret_cast<HWND>(1);
  HWND root = reinterpret_cast<HWND>(2);
  bool available = true;
};
inline Witness witness;
bool R21ReadMsaaCaret(HWND, RECT& rect, HWND& focus, HWND& root) {
  rect = witness.rect;
  focus = witness.focus;
  root = witness.root;
  return witness.available;
}
struct PositionSink {
  RECT output = {};
  void UpdateInputPosition(const RECT& rect) { output = rect; }
};
struct WeaselTSF {
#include "obj/placement-state.inc"
  HWND _r19ProbeViewHwnd = nullptr;
  BOOL _fCUASWorkaroundTested = TRUE;
  BOOL _fCUASWorkaroundEnabled = FALSE;
  PositionSink m_client;
  PositionSink candidate;
  PositionSink* _cand = &candidate;
  bool composing = true;
  bool deferred = false;
  bool pending = false;
  RECT hostNormal = {};
  BOOL _IsComposing() { return composing; }
  void _R21ResetPlacementFollow();
  void _R21PlacementFollowTick(com_ptr<ITfContext> context);
  void _SetCompositionPosition(const RECT& rect);
  void _UpdateCompositionWindow(com_ptr<ITfContext>) {
    if (deferred)
      pending = true;
    else
      _SetCompositionPosition(hostNormal);
  }
  void Drain() {
    if (pending) {
      pending = false;
      _SetCompositionPosition(hostNormal);
    }
  }
};

// Redirect only the clock call in the extracted routines. Real Windows/ATL
// types and production geometry, guards, and state transitions are unchanged.
#define GetTickCount PlacementTestTick
#include "obj/placement-under-test.inc"
#undef GetTickCount

struct Fixture {
  WeaselTSF service;
  com_ptr<ITfContext> context = new ITfContext;
  Fixture() {
    placementTestNow = 1000;
    witness = {};
    witness.rect = {2649, 1217, 2650, 1248};
    service._R21ResetPlacementFollow();
    placementTestNow += 100;
    service.hostNormal = {2556, 1217, 2556, 1248};
    service._r20CompositionStartLastRect = service.hostNormal;
    service._r20CompositionEndLastRect = {2651, 1217, 2651, 1248};
    service._r20CompositionStartHasRect = true;
    service._r20CompositionEndHasRect = true;
    service._SetCompositionPosition(service.hostNormal);
    ++service._r20SamplesCompleted;
    Tick();
    Expect(2556, 1217);
  }
  void Tick() { service._R21PlacementFollowTick(context); }
  void Settle() {
    Tick();
    placementTestNow += 100;
    Tick();
    service.Drain();
    Tick();
  }
  void Expect(LONG x, LONG y) {
    const auto& output = service._r21LastOutputPosition;
    Check(output.left == x && output.top == y,
          "candidate anchor did not follow expected physical START");
    Check(R21SameRect(output, service.m_client.output) &&
              R21SameRect(output, service.candidate.output),
          "client and candidate received different anchors");
    Check(output.bottom - output.top ==
              service.hostNormal.bottom - service.hostNormal.top,
          "translation changed the current TSF preedit height");
  }
  void Scroll(LONG dx, LONG dy) {
    witness.rect = R21TranslateRect(witness.rect, dx, dy);
    Settle();
  }
  void SourceMove(LONG dx, LONG dy) {
    service.hostNormal = R21TranslateRect(service.hostNormal, dx, dy);
    service._r20CompositionStartLastRect = service.hostNormal;
    service._r20CompositionEndLastRect =
        R21TranslateRect(service._r20CompositionEndLastRect, dx, dy);
    ++service._r20SamplesCompleted;
  }
  // 0: normal first; 1: pair first, synchronous query; 2: deferred normal.
  void Move(LONG dx, LONG dy, int order) {
    witness.rect = R21TranslateRect(witness.rect, dx, dy);
    SourceMove(dx, dy);
    service.deferred = order == 2;
    if (order == 0)
      service._SetCompositionPosition(service.hostNormal);
    Settle();
  }
};

void Run() {
  for (int order = 0; order < 3; ++order) {
    Fixture f;
    f.Scroll(0, -270);
    f.Expect(2556, 947);
    // The captured Code session ended with START 858, MSAA 588 and the
    // correction wrongly zeroed. Root and text-source updates were
    // asynchronous.
    f.Move(-76, -359, order);
    f.Expect(2480, 588);
    Check(f.service._r21CorrectionY == -270 && f.service._r21GuardResets == 0,
          "window movement discarded the captured -270 viewport correction");
    f.Move(76, 359, order);
    f.Expect(2556, 947);
    f.Scroll(0, 270);
    f.Expect(2556, 1217);
  }
  std::cout << "PASS: captured scroll/move/reverse in all callback orders\n";

  for (int order = 0; order < 3; ++order) {
    Fixture f;
    f.Scroll(0, -270);
    struct Frame {
      RECT start;
      RECT end;
      RECT caret;
    };
    // Read-only capture 2026-09-12, 22211..22700 ms, then stable 34180 ms.
    // Keep independently rounded edges and asynchronous updates verbatim.
    const Frame frames[] = {
        {{2556, 1217, 2556, 1248},
         {2651, 1217, 2651, 1248},
         {2649, 927, 2650, 957}},
        {{2556, 1197, 2556, 1227},
         {2651, 1197, 2651, 1227},
         {2646, 906, 2647, 936}},
        {{2553, 1176, 2553, 1206},
         {2648, 1176, 2648, 1206},
         {2642, 890, 2643, 921}},
        {{2549, 1155, 2549, 1185},
         {2643, 1155, 2643, 1185},
         {2640, 870, 2641, 900}},
        {{2544, 1131, 2544, 1161},
         {2639, 1131, 2639, 1161},
         {2637, 861, 2638, 891}},
        {{2544, 1131, 2544, 1161},
         {2639, 1131, 2639, 1161},
         {2636, 855, 2637, 885}},
        {{2543, 1125, 2543, 1155},
         {2637, 1125, 2637, 1155},
         {2636, 851, 2637, 882}},
        {{2543, 1121, 2543, 1152},
         {2637, 1121, 2637, 1152},
         {2634, 848, 2635, 879}},
        {{2541, 1116, 2541, 1146},
         {2636, 1116, 2636, 1146},
         {2634, 845, 2635, 876}},
        {{2541, 1113, 2541, 1143},
         {2636, 1113, 2636, 1143},
         {2634, 839, 2635, 870}},
        {{2538, 1107, 2538, 1137},
         {2633, 1107, 2633, 1137},
         {2631, 836, 2632, 867}},
        {{2538, 1103, 2538, 1134},
         {2633, 1103, 2633, 1134},
         {2631, 833, 2632, 864}},
        {{2480, 858, 2480, 888},
         {2574, 858, 2574, 888},
         {2573, 588, 2574, 618}},
    };
    for (const auto& frame : frames) {
      witness.rect = frame.caret;
      f.service.hostNormal = frame.start;
      f.service._r20CompositionStartLastRect = frame.start;
      f.service._r20CompositionEndLastRect = frame.end;
      ++f.service._r20SamplesCompleted;
      f.service.deferred = order == 2;
      if (order == 0)
        f.service._SetCompositionPosition(frame.start);
      f.Settle();
      f.Expect(frame.caret.left - 93, frame.caret.top);
    }
    Check(f.service._r21CorrectionY == -270 && f.service._r21GuardResets == 0,
          "one-pixel TSF rounding discarded the captured viewport correction");
  }
  std::cout << "PASS: recorded rounded rectangles in all callback orders\n";

  for (LONG scroll : {-270L, 270L, 0L}) {
    for (int order = 0; order < 3; ++order) {
      Fixture f;
      f.Scroll(0, scroll);
      for (int i = 0; i < 20; ++i) {
        f.Move(5, -7, order);
        f.Expect(2556 + 5 * (i + 1), 1217 + scroll - 7 * (i + 1));
      }
      f.Move(-100, 140, order);
      f.Expect(2556, 1217 + scroll);
      Check(
          f.service._r21CorrectionY == scroll && f.service._r21CorrectionX == 0,
          "repeated movement accumulated a displacement");
    }
  }
  std::cout
      << "PASS: both scroll directions and plain repeated window movement\n";

  for (int order = 0; order < 3; ++order) {
    Fixture f;
    f.Scroll(-40, -270);
    f.Expect(2516, 947);
    // The TSF source finally incorporates scroll; the actual caret stays put.
    f.SourceMove(-40, -270);
    f.service.deferred = order == 2;
    if (order == 0)
      f.service._SetCompositionPosition(f.service.hostNormal);
    f.Settle();
    f.Expect(2516, 947);
    Check(!f.service._r21CorrectionActive,
          "fresh TSF anchor was translated a second time");
    f.Move(12, 18, order);
    f.Expect(2528, 965);
  }
  std::cout << "PASS: TSF viewport catch-up removes redundant correction\n";

  for (bool witnessFirst : {false, true}) {
    Fixture f;
    f.Scroll(0, -270);
    // Stagger witness and TSF by one observation; never require simultaneous
    // root/source deltas, which the captured trace shows are not synchronous.
    if (witnessFirst) {
      f.Scroll(-10, -20);
      f.SourceMove(-10, -20);
    } else {
      f.SourceMove(-10, -20);
      f.Settle();
      f.Scroll(-10, -20);
    }
    f.Settle();
    f.Expect(2546, 927);
    Check(f.service._r21CorrectionY == -270,
          "asynchronous geometry left a residual gap");
  }
  std::cout << "PASS: asynchronous source and accessibility updates converge\n";

  {
    Fixture f;
    f.Scroll(0, -270);
    f.service._r21LastTextActivityTick = placementTestNow;
    witness.rect = R21TranslateRect(witness.rect, 19, 0);
    f.Tick();
    f.Expect(2556, 947);
    // END catches up with typing; it must rebase the caret witness without
    // moving START or throwing away the established viewport correction.
    f.service._r20CompositionEndLastRect =
        R21TranslateRect(f.service._r20CompositionEndLastRect, 19, 0);
    ++f.service._r20SamplesCompleted;
    f.Settle();
    f.Expect(2556, 947);
    f.Move(8, 16, 2);
    f.Expect(2564, 963);
  }
  std::cout
      << "PASS: typing gate and END-only change retain the START anchor\n";

  for (int guard = 0; guard < 5; ++guard) {
    Fixture f;
    f.Scroll(0, -270);
    f.SourceMove(0, -20);
    if (guard == 0)
      witness.available = false;
    if (guard == 1)
      witness.focus = reinterpret_cast<HWND>(3);
    if (guard == 2)
      witness.root = reinterpret_cast<HWND>(4);
    if (guard == 3)
      f.service._r21LastTextActivityTick = placementTestNow;
    if (guard == 4)
      f.service.hostNormal.right += 3;
    f.service._SetCompositionPosition(f.service.hostNormal);
    f.Expect(2556, 1197);
    Check(!f.service._r21CorrectionActive && !f.service._r21HaveMsaaBaseline &&
              f.service._r21GuardResets == 1,
          "normal-path guard retained an untrusted witness");
  }
  std::cout
      << "PASS: normal callback rejects missing/foreign/typing/shape witness\n";

  {
    Fixture f;
    f.Scroll(0, -270);
    f.SourceMove(0, -20);
    f.service._r20CompositionEndLastRect.right += 3;
    f.Settle();
    f.Expect(2556, 1197);
    Check(!f.service._r21CorrectionActive,
          "non-rigid TSF pair retained a stale correction");
  }
  {
    Fixture f;
    f.Scroll(0, -270);
    f.service._r20CompositionEndLastTextExtHr = E_FAIL;
    witness.rect = R21TranslateRect(witness.rect, 20, 20);
    f.Settle();
    f.Expect(2556, 947);
    f.service.composing = false;
    f.SourceMove(0, 50);
    f.Settle();
    f.Expect(2556, 947);
    f.service._R21ResetPlacementFollow();
    Check(!f.service._r21HaveMsaaBaseline && !f.service._r21CorrectionActive &&
              !f.service._r21HaveNormalPosition,
          "composition reset retained placement state");
  }
  std::cout << "PASS: pair geometry/failure and composition-lifetime guards\n";
}

}  // namespace placement_follow
