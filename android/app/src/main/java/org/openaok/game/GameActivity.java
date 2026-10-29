package org.openaok.game;

import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.window.OnBackInvokedDispatcher;

import org.libsdl.app.SDLActivity;

/**
 * The game: SDL's activity, running the engine in libmain.so (built by
 * android/app/jni/CMakeLists.txt) on top of libSDL2.so. It also tells the
 * engine whether a mouse is plugged in, whenever a device comes or goes: with
 * one the map scrolls at the screen's edges, as on a desktop (src/hal/Touch).
 *
 * The back gesture is the game's Escape (src/hal/Window), never the end of the
 * game: since Android 13 the system takes it before any key event, so it is
 * caught here and handed to the engine as the back key. When the game ends,
 * the process ends with it, so the next start loads everything afresh rather
 * than finding the last game's art in memory, made for a renderer since gone.
 */
public class GameActivity extends SDLActivity implements InputManager.InputDeviceListener {
    private static native void nativeMouseAttached(boolean attached);

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT, () -> {
                    onNativeKeyDown(KeyEvent.KEYCODE_BACK);
                    onNativeKeyUp(KeyEvent.KEYCODE_BACK);
                });
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        System.exit(0);
    }

    @Override
    protected void onResume() {
        super.onResume();
        inputs().registerInputDeviceListener(this, null);
        reportMouse();
    }

    @Override
    protected void onPause() {
        inputs().unregisterInputDeviceListener(this);
        super.onPause();
    }

    @Override public void onInputDeviceAdded(int id) { reportMouse(); }
    @Override public void onInputDeviceRemoved(int id) { reportMouse(); }
    @Override public void onInputDeviceChanged(int id) { reportMouse(); }

    private InputManager inputs() {
        return (InputManager) getSystemService(Context.INPUT_SERVICE);
    }

    private void reportMouse() {
        boolean mouse = false;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device != null && !device.isVirtual() && device.supportsSource(InputDevice.SOURCE_MOUSE)) {
                mouse = true;
            }
        }
        try {
            nativeMouseAttached(mouse);
        } catch (UnsatisfiedLinkError e) {
            // The engine's library did not load; SDLActivity says so itself.
        }
    }
}
