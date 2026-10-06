# Bounded arrow-key trace candidate

Base: `ce414d9cfc4539ebd0daf20649e88149696c2abe`.
Diagnostic candidate only; no input fix or performance attribution.

Only Android DPAD Up/Down/Left/Right (19–22), and their XKB codes
111/116/113/114, are recorded. No characters, text, clipboard, or other key
contents are logged. Each endpoint opens a single 10-second window on its
first arrow event, with a 2048-event cap. Java counts each stage as an event;
JNI may emit two lines per event (enter/write result). Windows do not rearm.

Stages (`R9ArrowTrace`):

- `preime`, `dispatch`, `sender`: existing Java entry points; monotonic ns,
  Android key/scan/action/flags, device/source, repeat, downTime/eventTime.
- `jni_enter`: before the unchanged single EVENT_KEY write.
- `jni_send`: after that write; original return count/errno, expected count,
  XKB code, down state, fd, start/end monotonic ns. No retry or recovery.
- `jni_disconnected`: arrow JNI invocation with no connection; no write.
- `x11_receive`: server received EVENT_KEY, immediately before the existing
  QueueKeyboardEvents call. No changes to that call or the wire format.

All native timestamps use CLOCK_MONOTONIC; Java uses System.nanoTime.
Match key/state and temporal order. There is no new on-wire event ID, so a
missing/ambiguous match must remain unknown, not be paired by assumption.
Absence after an endpoint's 10-second window is not evidence of a lost key.
`jni_enter` without `jni_send` distinguishes an unfinished write from failure
to reach JNI. A short/error write is recorded, never repaired by this patch.

For a separately authorized future activation, both the Android Activity and
X server must use this candidate. The existing `TERMUX_X11_DEBUG=1` startup
mechanism forwards the Activity's own logcat through its existing Binder fd
to the server log; filter that file for `R9ArrowTrace`. No READ_LOGS grant,
root, WRITE_SECURE_SETTINGS, new IPC, or new log-export service is needed.
That existing debug transport may also capture ordinary application logs;
the new trace itself is arrow-only. It is for a short key test, not a timing
benchmark. Capture must be prepared before the first arrow key is pressed.

Installing the APK or restarting either side is outside build authorization.
No mapping, focus, IME policy, scancode preference, key-state cleanup, input
protocol, renderer, Denial, Bridge, C9 logic, or frozen baseline is changed.
The extra hooks add bounded logging overhead; runtime behavior is not yet
validated merely because compilation and offline checks pass.
