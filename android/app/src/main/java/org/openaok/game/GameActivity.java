package org.openaok.game;

import android.content.Context;
import android.hardware.input.InputManager;
import android.view.InputDevice;

import org.libsdl.app.SDLActivity;

/**
 * The game: SDL's activity, running the engine in libmain.so (built by
 * android/app/jni/CMakeLists.txt) on top of libSDL2.so. It also tells the
 * engine whether a mouse is plugged in, whenever a device comes or goes: with
 * one the map scrolls at the screen's edges, as on a desktop (src/hal/Touch).
 */
public class GameActivity extends SDLActivity implements InputManager.InputDeviceListener {
    private static native void nativeMouseAttached(boolean attached);

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
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
