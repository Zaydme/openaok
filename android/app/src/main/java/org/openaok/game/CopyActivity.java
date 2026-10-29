package org.openaok.game;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.CRC32;

/**
 * The launcher. The engine reads the game's data from files beside its build
 * directory, and the APK holds none of it: the player makes a folder of it on
 * a computer from their own CD (android/package.py) and copies it to the
 * phone. On first start this asks for that folder and copies it into the app's
 * own storage, checking every file against the folder's manifest.txt (its
 * CRC-32, size and path), then starts the game. Data of another version than
 * this build plays from (BuildConfig.DATA_VERSION, the folder's version.txt)
 * is asked for again.
 */
public class CopyActivity extends Activity {
    private static final int kChooseFolder = 1;
    // The name android/package.py gives the folder, looked for inside the one
    // chosen if that is not it.
    private static final String kFolderName = "OpenAoK-data";

    private LinearLayout layout;
    private TextView status;
    private ProgressBar bar;
    private Button choose;
    private Button play;

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        final int installed = installedVersion();
        if (installed == BuildConfig.DATA_VERSION) {
            play();
            return;
        }
        build();
        if (installed > 0) {
            show("This version of OpenAoK plays from newer game data than the copy on this phone. "
                + "Make the OpenAoK-data folder again with this release's source code, copy it to "
                + "the phone and choose it.", true);
        } else {
            show("OpenAoK needs the game's data, made from your own Age of Empires II CD on a "
                + "computer. With the CD copied into original_game_1999/ in OpenAoK's source code, "
                + "run python3 android/package.py there, copy the OpenAoK-data folder it makes to "
                + "this phone, then choose that folder. The README has the steps.", false);
        }
    }

    private File game() {
        return new File(getFilesDir(), "game");
    }

    private File marker() {
        return new File(game(), ".installed");
    }

    // The version of the data copied in, or 0 when there is none.
    private int installedVersion() {
        try {
            return Integer.parseInt(new String(Files.readAllBytes(marker().toPath()), StandardCharsets.UTF_8).trim());
        } catch (IOException | NumberFormatException e) {
            return 0;
        }
    }

    private void build() {
        layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setBackgroundColor(Color.BLACK);
        layout.setPadding(96, 0, 96, 0);
        status = new TextView(this);
        status.setTextColor(Color.rgb(230, 200, 120));
        status.setTextSize(18);
        status.setGravity(Gravity.CENTER);
        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        bar.setVisibility(View.GONE);
        choose = new Button(this);
        choose.setText("Choose folder");
        choose.setOnClickListener(v -> {
            Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            startActivityForResult(pick, kChooseFolder);
        });
        play = new Button(this);
        play.setText("Play anyway");
        play.setOnClickListener(v -> play());
        layout.addView(status);
        layout.addView(bar, new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        LinearLayout buttons = new LinearLayout(this);
        buttons.setGravity(Gravity.CENTER);
        buttons.addView(choose);
        buttons.addView(play);
        layout.addView(buttons);
        setContentView(layout);
        layout.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    // The words, and the buttons beneath them; `playable` offers to play with
    // the data already copied in.
    private void show(String text, boolean playable) {
        status.setText(text);
        bar.setVisibility(View.GONE);
        choose.setVisibility(View.VISIBLE);
        play.setVisibility(playable ? View.VISIBLE : View.GONE);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != kChooseFolder || result != RESULT_OK || data == null || data.getData() == null) return;
        final Uri tree = data.getData();
        choose.setVisibility(View.GONE);
        play.setVisibility(View.GONE);
        bar.setVisibility(View.VISIBLE);
        bar.setProgress(0);
        status.setText("Reading the folder...");
        new Thread(() -> {
            try {
                install(tree);
                runOnUiThread(this::play);
            } catch (IOException e) {
                final boolean playable = installedVersion() > 0;
                runOnUiThread(() -> show(e.getMessage(), playable));
            }
        }).start();
    }

    // A document in the chosen tree: its id, and whether it is a folder.
    private static final class Entry {
        final String id;
        final boolean folder;

        Entry(String id, boolean folder) {
            this.id = id;
            this.folder = folder;
        }
    }

    private Map<String, Entry> children(Uri tree, String folder) throws IOException {
        Map<String, Entry> out = new HashMap<>();
        Uri uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, folder);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor c = getContentResolver().query(uri, columns, null, null, null)) {
            if (c == null) throw new IOException("Could not read the folder.");
            while (c.moveToNext()) {
                out.put(c.getString(1), new Entry(c.getString(0),
                    DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))));
            }
        }
        return out;
    }

    // Every file under `folder`, by its path from it.
    private void walk(Uri tree, String folder, String prefix, Map<String, String> files) throws IOException {
        for (Map.Entry<String, Entry> child : children(tree, folder).entrySet()) {
            String path = prefix + child.getKey();
            if (child.getValue().folder) {
                walk(tree, child.getValue().id, path + "/", files);
            } else {
                files.put(path, child.getValue().id);
            }
        }
    }

    private InputStream open(Uri tree, String id) throws IOException {
        InputStream in = getContentResolver().openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, id));
        if (in == null) throw new IOException("Could not open a file in the folder.");
        return in;
    }

    private List<String> lines(Uri tree, String id) throws IOException {
        List<String> out = new ArrayList<>();
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(open(tree, id), StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) out.add(line);
        }
        return out;
    }

    private void install(Uri tree) throws IOException {
        // The folder chosen, or the one package.py made inside it.
        String root = DocumentsContract.getTreeDocumentId(tree);
        Map<String, Entry> top = children(tree, root);
        if (!top.containsKey("manifest.txt") && top.containsKey(kFolderName) && top.get(kFolderName).folder) {
            root = top.get(kFolderName).id;
        }
        Map<String, String> files = new HashMap<>();
        walk(tree, root, "", files);
        if (!files.containsKey("manifest.txt") || !files.containsKey("version.txt")) {
            throw new IOException("That folder is not the game's data: choose the " + kFolderName
                + " folder made by android/package.py.");
        }
        int version;
        try {
            version = Integer.parseInt(lines(tree, files.get("version.txt")).get(0).trim());
        } catch (RuntimeException e) {
            version = 0;
        }
        if (version != BuildConfig.DATA_VERSION) {
            throw new IOException("That folder holds game data of version " + version + "; this OpenAoK "
                + "plays from version " + BuildConfig.DATA_VERSION + ". Make it again with this "
                + "release's source code.");
        }

        // CRC-32, size, path.
        List<String[]> wanted = new ArrayList<>();
        long total = 0;
        for (String line : lines(tree, files.get("manifest.txt"))) {
            String[] parts = line.split(" ", 3);
            if (parts.length != 3) continue;
            wanted.add(parts);
            total += Long.parseLong(parts[1]);
        }

        File game = game();
        marker().delete();
        new File(game, "build").mkdirs();
        byte[] buffer = new byte[1 << 20];
        long done = 0;
        int shown = -1;
        for (String[] item : wanted) {
            String id = files.get(item[2]);
            if (id == null) throw new IOException("The folder is missing " + item[2] + ": copy it to the phone again.");
            File out = new File(game, item[2]);
            File parent = out.getParentFile();
            if (parent != null) parent.mkdirs();
            CRC32 crc = new CRC32();
            long size = 0;
            try (InputStream in = open(tree, id); OutputStream os = new FileOutputStream(out)) {
                int n;
                while ((n = in.read(buffer)) > 0) {
                    os.write(buffer, 0, n);
                    crc.update(buffer, 0, n);
                    size += n;
                    done += n;
                    final int permille = total > 0 ? (int) (done * 1000 / total) : 1000;
                    if (permille != shown) {
                        shown = permille;
                        final long mb = done >> 20, all = total >> 20;
                        runOnUiThread(() -> {
                            bar.setProgress(permille);
                            status.setText("Copying the game data...  " + mb + " / " + all + " MB");
                        });
                    }
                }
            }
            if (size != Long.parseLong(item[1]) || crc.getValue() != Long.parseLong(item[0], 16)) {
                throw new IOException(item[2] + " in the folder is damaged: copy the folder to the phone again.");
            }
        }
        try (FileWriter w = new FileWriter(marker())) {
            w.write(Integer.toString(BuildConfig.DATA_VERSION));
        }
    }

    private void play() {
        startActivity(new Intent(this, GameActivity.class));
        finish();
    }
}
