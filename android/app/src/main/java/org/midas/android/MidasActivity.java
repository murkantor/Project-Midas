package org.midas.android;

import android.content.Intent;
import android.os.Bundle;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import org.libsdl.app.SDLActivity;

import java.io.File;

/**
 * SDL entry point for the game. The ROM has already been validated and
 * installed in app-private storage by {@link RomActivity}.
 */
public class MidasActivity extends SDLActivity {
    private TouchControlsView controls;
    @Override
    protected String[] getLibraries() {
        return new String[]{"SDL2", "main"};
    }

    @Override
    protected String[] getArguments() {
        File rom = RomStore.romFile(this);
        return new String[]{"--rom-file", rom.getAbsolutePath()};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        GameData.ensureInstalled(this);
        RomStore.recoverInterruptedImport(this);
        RomStore.ensureAndroidConfig(this);
        boolean hasRom = RomStore.hasUsableRom(this);
        super.onCreate(savedInstanceState);

        if (!hasRom) {
            // The launcher normally prevents this. If the game activity is
            // restored after its data was cleared, go back to the importer
            // instead of starting native code without a cart. finish() here
            // prevents SDL's native thread from being resumed.
            startActivity(new Intent(this, RomActivity.class));
            finish();
            return;
        }

        if (!SDLActivity.mBrokenLibraries) {
            controls = new TouchControlsView(this);
            addContentView(controls, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }
    }

    @Override
    protected void onPause() {
        if (controls != null) {
            controls.resetTouchInput();
        }
        AndroidInput.reset();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        AndroidInput.reset();
        super.onDestroy();
    }
}
