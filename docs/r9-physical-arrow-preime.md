# Physical arrow pre-IME routing candidate

Base: `22b880ac1c36e3fcfa326ee61e668f5c19d3bbb3`.

Observed real physical Down tap: ACTION_DOWN reached pre-IME, ordinary dispatch,
the input sender, JNI (24/24 bytes), and the X server. ACTION_UP reached pre-IME
but no later stage. All recorded Activity sequence numbers were contiguous,
with lost=0. Current hardwareKbdScancodesWorkaround=true returns false from
pre-IME; XQueryKeymap still reported key 116 held after the user's release.
The exact framework/IME consumer is not identified by these observations.

When that existing workaround is enabled, this candidate sends physical
keyboard UP/DOWN/LEFT/RIGHT ACTION_DOWN and ACTION_UP through the existing
MainActivity.handleKey from pre-IME. It returns the handler's existing consumed
result, preventing double dispatch when handled. Hardware identification reuses
InputEventSender.isHardwareKeyboardEvent verbatim (visibility only expanded).
No new held-key tracking, fake release, delay, remapping, repeat policy, settings
change, or new protocol is added. The old compatibility path is retained for
non-arrows, virtual/software/unknown/gamepad sources. With the workaround off,
the existing pre-IME path is unchanged. Existing handler filtering/rejection
and modifier/repeat behavior are retained.

Production scope: LorieView.java plus helper visibility in InputEventSender.java.
No native input/render/Denial/Bridge/C9 or frozen files are changed. Existing
bounded arrow diagnostics are retained for a future authorized activation.

The offline regression executes the actual pre-IME method and hardware predicate
with synthetic event/device objects. The retained baseline method fails when
ordinary dispatch omits UP; the candidate delivers DOWN/UP exactly once. Tests
also cover normal dispatch, repeats, unchanged event identity/modifiers,
handler rejection, excluded device/source/event cases, other keycodes, and
workaround-off behavior. This simulation is not Android framework or real
keyboard acceptance; full APK build and a later real tap remain separate gates.

The fixture r9-arrow-preime-baseline.java.txt is extracted verbatim from
LorieView.dispatchKeyEventPreIme at 22b880a. The tests do not launch Android UI
or inject device keys. No installation/session switch is authorized by building.
