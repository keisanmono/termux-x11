package com.termux.x11;

import android.util.Log;
import android.view.KeyEvent;

/** Diagnostic branch only: no characters, remapping, injection or retries. */
public final class ArrowKeyTrace {
    private static long startNs;
    private static int lines;

    private ArrowKeyTrace() {}

    static boolean isArrow(int key) {
        return key == KeyEvent.KEYCODE_DPAD_UP || key == KeyEvent.KEYCODE_DPAD_DOWN
                || key == KeyEvent.KEYCODE_DPAD_LEFT || key == KeyEvent.KEYCODE_DPAD_RIGHT;
    }

    // One window per process, starting on its first arrow event. Never rearms.
    static synchronized boolean allow(int key, long nowNs) {
        if (!isArrow(key)) return false;
        if (startNs == 0) startNs = nowNs;
        if (nowNs < startNs || nowNs - startNs >= 10_000_000_000L || lines >= 2048)
            return false;
        lines++;
        return true;
    }

    public static void event(String stage, KeyEvent e) {
        long nowNs = System.nanoTime();
        if (!allow(e.getKeyCode(), nowNs)) return;
        Log.i("R9ArrowTrace", "stage=" + stage + " mono_ns=" + nowNs
                + " key=" + e.getKeyCode() + " scan=" + e.getScanCode()
                + " action=" + e.getAction() + " flags=" + e.getFlags()
                + " device=" + e.getDeviceId() + " source=" + e.getSource()
                + " repeat=" + e.getRepeatCount() + " down_ms=" + e.getDownTime()
                + " event_ms=" + e.getEventTime());
    }
}
