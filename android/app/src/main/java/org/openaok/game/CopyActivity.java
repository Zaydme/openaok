package org.openaok.game;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * The launcher. The engine reads its data from files beside its build
 * directory, so on first run, and after an update, this copies the game's data
 * out of the APK's assets (game/, listed with sizes in game/manifest.txt by the
 * Gradle build) into the app's own storage, showing progress, then starts the
 * game.
 */
public class CopyActivity extends Activity {
    private TextView status;
    private ProgressBar bar;

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        final File game = new File(getFilesDir(), "game");
        final File marker = new File(game, ".copied-" + version());
        if (marker.exists()) {
            play();
            return;
        }

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setBackgroundColor(Color.BLACK);
        layout.setPadding(96, 0, 96, 0);
        status = new TextView(this);
        status.setTextColor(Color.rgb(230, 200, 120));
        status.setTextSize(20);
        status.setGravity(Gravity.CENTER);
        status.setText("Copying game data...");
        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        layout.addView(status);
        layout.addView(bar, new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        setContentView(layout);
        layout.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        new Thread(() -> {
            try {
                copyAll(game);
                marker.createNewFile();
                runOnUiThread(this::play);
            } catch (IOException e) {
                runOnUiThread(() -> status.setText("Could not copy the game data: " + e.getMessage()));
            }
        }).start();
    }

    private long version() {
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            return info.getLongVersionCode() * 1000003L + info.lastUpdateTime;
        } catch (Exception e) {
            return 0;
        }
    }

    private void copyAll(File game) throws IOException {
        List<String> paths = new ArrayList<>();
        long total = 0;
        try (BufferedReader manifest = new BufferedReader(
                new InputStreamReader(getAssets().open("game/manifest.txt")))) {
            String line;
            while ((line = manifest.readLine()) != null) {
                int tab = line.indexOf('\t');
                if (tab <= 0) continue;
                total += Long.parseLong(line.substring(0, tab));
                paths.add(line.substring(tab + 1));
            }
        }
        // The directory the engine runs in: "../assets" from it is game/assets.
        new File(game, "build").mkdirs();
        byte[] buffer = new byte[1 << 20];
        long done = 0;
        int shown = -1;
        for (String path : paths) {
            File out = new File(game, path);
            File parent = out.getParentFile();
            if (parent != null) parent.mkdirs();
            try (InputStream in = getAssets().open("game/" + path);
                 OutputStream os = new FileOutputStream(out)) {
                int n;
                while ((n = in.read(buffer)) > 0) {
                    os.write(buffer, 0, n);
                    done += n;
                    final int permille = total > 0 ? (int) (done * 1000 / total) : 1000;
                    if (permille != shown) {
                        shown = permille;
                        final long mb = done >> 20, all = total >> 20;
                        runOnUiThread(() -> {
                            bar.setProgress(permille);
                            status.setText("Copying game data...  " + mb + " / " + all + " MB");
                        });
                    }
                }
            }
        }
    }

    private void play() {
        startActivity(new Intent(this, GameActivity.class));
        finish();
    }
}
