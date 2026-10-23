package org.midas.android;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.text.format.DateFormat;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.Date;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Launcher and ROM setup screen.
 * Handles storage permission requests, auto-locates GoldenEye 007 (USA).z64
 * on the device, configures midas.ini persistently, and launches the game.
 */
public class RomActivity extends Activity {
    private static final int REQUEST_OPEN_ROM = 1001;
    private static final int REQUEST_MANAGE_STORAGE = 1002;
    private static final int REQUEST_STORAGE_PERMS = 1003;

    private static final int COLOR_GOLD = 0xFFD4AF37;
    private static final int COLOR_GOLD_DARK = 0xFF997A1E;
    private static final int COLOR_BG_DARK = 0xFF101114;
    private static final int COLOR_CARD = 0xFF181A20;
    private static final int COLOR_TEXT_PRIMARY = 0xFFEDEDED;
    private static final int COLOR_TEXT_MUTED = 0xFFA0A5B0;
    private static final int COLOR_ERROR = 0xFFE53935;

    private final ExecutorService backgroundExecutor = Executors.newSingleThreadExecutor();

    private TextView statusView;
    private TextView detailView;
    private TextView errorView;
    private Button primaryButton;
    private Button secondaryButton;
    private Button tertiaryButton;
    private ProgressBar progress;

    private boolean importing;
    private boolean scanning;
    private boolean scannedOnce;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        GameData.ensureInstalled(this);
        RomStore.recoverInterruptedImport(this);
        RomStore.ensureAndroidConfig(this);

        buildUi();
        refresh();

        if (savedInstanceState == null) {
            boolean stayInLauncher = getIntent().getBooleanExtra("stay_in_launcher", false);
            if (RomStore.hasUsableRom(this) && !stayInLauncher) {
                // ROM is already verified and installed - start game directly
                startGame();
            } else if (!RomStore.hasUsableRom(this)) {
                // First run: prompt for permissions and auto-locate ROM
                promptStoragePermissionAndScan();
            }
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!importing && !scanning) {
            if (!RomStore.hasUsableRom(this) && RomStore.hasStoragePermission(this) && !scannedOnce) {
                scanAndAutoImport();
            } else {
                refresh();
            }
        }
    }

    @Override
    protected void onDestroy() {
        backgroundExecutor.shutdown();
        super.onDestroy();
    }

    private void promptStoragePermissionAndScan() {
        if (RomStore.hasStoragePermission(this)) {
            scanAndAutoImport();
            return;
        }

        // Show a dialog explaining why permission is requested
        new AlertDialog.Builder(this)
                .setTitle("Storage Permission Required")
                .setMessage("Project Midas needs storage access to search for 'GoldenEye 007 (USA).z64' on your device.\n\nWould you like to grant access now?")
                .setPositiveButton("Grant Access", (dialog, which) -> requestStoragePermission())
                .setNegativeButton("Choose Manually", (dialog, which) -> openPicker())
                .setCancelable(true)
                .show();
    }

    private void requestStoragePermission() {
        RomStore.markPermissionRequested(this);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivityForResult(intent, REQUEST_MANAGE_STORAGE);
            } catch (Exception e) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                startActivityForResult(intent, REQUEST_MANAGE_STORAGE);
            }
        } else {
            requestPermissions(new String[]{
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
            }, REQUEST_STORAGE_PERMS);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_STORAGE_PERMS) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                scanAndAutoImport();
            } else {
                refresh();
                setError("Storage permission was denied. You can select your ROM manually.");
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_MANAGE_STORAGE) {
            if (RomStore.hasStoragePermission(this)) {
                scanAndAutoImport();
            } else {
                refresh();
                setError("Storage permission was not granted. You can select your ROM manually.");
            }
            return;
        }

        if (requestCode != REQUEST_OPEN_ROM) {
            return;
        }
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            setError(getString(R.string.import_cancelled));
            refresh();
            return;
        }

        final Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException | IllegalArgumentException e) {
            setError(getString(R.string.persist_failed));
            return;
        }
        beginImportUri(uri);
    }

    private void scanAndAutoImport() {
        if (scanning || importing) return;
        scanning = true;
        scannedOnce = true;
        setError(null);
        statusView.setText(R.string.scanning_device);
        detailView.setText("Searching storage for GoldenEye 007 (USA).z64\u2026");
        progress.setVisibility(View.VISIBLE);
        setButtonsEnabled(false);

        backgroundExecutor.execute(() -> {
            File found = RomStore.findRomOnDevice(getApplicationContext());
            runOnUiThread(() -> {
                scanning = false;
                if (found != null && found.isFile()) {
                    beginImportFile(found);
                } else {
                    progress.setVisibility(View.GONE);
                    setButtonsEnabled(true);
                    setError(getString(R.string.rom_not_found_auto));
                    refresh();
                }
            });
        });
    }

    private void beginImportFile(File file) {
        importing = true;
        setError(null);
        statusView.setText(R.string.importing_rom);
        detailView.setText("Found: " + file.getAbsolutePath() + "\nValidating SHA-1 and installing\u2026");
        progress.setVisibility(View.VISIBLE);
        setButtonsEnabled(false);

        backgroundExecutor.execute(() -> {
            RomStore.ImportResult result;
            try {
                result = RomStore.importFromFile(getApplicationContext(), file);
            } catch (RuntimeException e) {
                result = RomStore.ImportResult.failure(e.getMessage() == null ? e.toString() : e.getMessage());
            }
            final RomStore.ImportResult finalResult = result;
            runOnUiThread(() -> {
                importing = false;
                progress.setVisibility(View.GONE);
                setButtonsEnabled(true);
                if (finalResult.ok) {
                    refresh();
                    Toast.makeText(this, "Found and configured GoldenEye 007 (USA)!", Toast.LENGTH_LONG).show();
                    startGame();
                } else {
                    refresh();
                    setError(getString(R.string.import_failed, finalResult.message));
                }
            });
        });
    }

    private void beginImportUri(final Uri uri) {
        final String previousUri = RomStore.getSavedUri(this);
        importing = true;
        setError(null);
        statusView.setText(R.string.importing_rom);
        detailView.setText("Validating and installing ROM from document provider\u2026");
        progress.setVisibility(View.VISIBLE);
        setButtonsEnabled(false);

        backgroundExecutor.execute(() -> {
            RomStore.ImportResult result;
            try {
                result = RomStore.importFromUri(getApplicationContext(), uri);
            } catch (RuntimeException e) {
                result = RomStore.ImportResult.failure(
                        e.getMessage() == null ? e.toString() : e.getMessage());
            }
            final RomStore.ImportResult finalResult = result;
            runOnUiThread(() -> finishImportUri(uri, previousUri, finalResult));
        });
    }

    private void finishImportUri(Uri uri, String previousUri, RomStore.ImportResult result) {
        if (isFinishing() || isDestroyed()) return;
        importing = false;
        progress.setVisibility(View.GONE);
        setButtonsEnabled(true);

        if (result.ok) {
            refresh();
            Toast.makeText(this, result.message, Toast.LENGTH_LONG).show();
            startGame();
        } else {
            refresh();
            setError(getString(R.string.import_failed, result.message));
        }
    }

    private void setButtonsEnabled(boolean enabled) {
        primaryButton.setEnabled(enabled);
        secondaryButton.setEnabled(enabled);
        if (tertiaryButton != null) {
            tertiaryButton.setEnabled(enabled);
        }
    }

    private void buildUi() {
        int pad = dp(24);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(COLOR_BG_DARK);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad, pad, pad);
        root.setGravity(Gravity.START);
        scroll.addView(root, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView heading = new TextView(this);
        heading.setText("PROJECT MIDAS");
        heading.setTextSize(28);
        heading.setTextColor(COLOR_GOLD);
        heading.getPaint().setFakeBoldText(true);
        add(root, heading, 4);

        TextView subheading = new TextView(this);
        subheading.setText("GoldenEye 007 - Classified Mission");
        subheading.setTextSize(16);
        subheading.setTextColor(COLOR_TEXT_PRIMARY);
        add(root, subheading, 2);

        TextView subtitle = new TextView(this);
        subtitle.setText("Android Port with Full Controller & Options Support");
        subtitle.setTextSize(13);
        subtitle.setTextColor(COLOR_TEXT_MUTED);
        add(root, subtitle, 2);

        // Status Card
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(16), dp(16), dp(16), dp(16));
        GradientDrawable cardBg = new GradientDrawable();
        cardBg.setColor(COLOR_CARD);
        cardBg.setCornerRadius(dp(8));
        cardBg.setStroke(dp(1), 0x33D4AF37);
        card.setBackground(cardBg);

        statusView = styledText(R.style.RomStatus);
        statusView.setTextColor(COLOR_GOLD);
        card.addView(statusView);

        detailView = styledText(R.style.RomBody);
        detailView.setTextColor(COLOR_TEXT_MUTED);
        LinearLayout.LayoutParams detailParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        detailParams.topMargin = dp(8);
        card.addView(detailView, detailParams);

        errorView = styledText(R.style.RomError);
        errorView.setTextColor(COLOR_ERROR);
        errorView.setVisibility(View.GONE);
        LinearLayout.LayoutParams errorParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        errorParams.topMargin = dp(8);
        card.addView(errorView, errorParams);

        add(root, card, 20);

        // Progress bar
        progress = new ProgressBar(this);
        progress.setIndeterminate(true);
        progress.setVisibility(View.GONE);
        LinearLayout.LayoutParams progressParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        progressParams.topMargin = dp(16);
        progressParams.gravity = Gravity.CENTER_HORIZONTAL;
        root.addView(progress, progressParams);

        // Primary Button (Gold)
        primaryButton = new Button(this);
        primaryButton.setAllCaps(false);
        primaryButton.setMinHeight(dp(52));
        primaryButton.setTextSize(16);
        primaryButton.setTextColor(0xFF101114);
        primaryButton.getPaint().setFakeBoldText(true);
        GradientDrawable pBg = new GradientDrawable();
        pBg.setColor(COLOR_GOLD);
        pBg.setCornerRadius(dp(6));
        primaryButton.setBackground(pBg);
        primaryButton.setOnClickListener(v -> onPrimary());
        add(root, primaryButton, 20);

        // Secondary Button (Dark with gold border)
        secondaryButton = new Button(this);
        secondaryButton.setAllCaps(false);
        secondaryButton.setMinHeight(dp(48));
        secondaryButton.setTextSize(14);
        secondaryButton.setTextColor(COLOR_GOLD);
        GradientDrawable sBg = new GradientDrawable();
        sBg.setColor(COLOR_CARD);
        sBg.setCornerRadius(dp(6));
        sBg.setStroke(dp(1), COLOR_GOLD_DARK);
        secondaryButton.setBackground(sBg);
        secondaryButton.setOnClickListener(v -> onSecondary());
        add(root, secondaryButton, 10);

        // Tertiary Button (Manual pick fallback)
        tertiaryButton = new Button(this);
        tertiaryButton.setAllCaps(false);
        tertiaryButton.setMinHeight(dp(44));
        tertiaryButton.setTextSize(13);
        tertiaryButton.setTextColor(COLOR_TEXT_MUTED);
        GradientDrawable tBg = new GradientDrawable();
        tBg.setColor(0x00000000);
        tertiaryButton.setBackground(tBg);
        tertiaryButton.setOnClickListener(v -> openPicker());
        add(root, tertiaryButton, 6);

        TextView note = styledText(R.style.RomBody);
        note.setText(R.string.privacy_note);
        note.setTextSize(12);
        note.setTextColor(COLOR_TEXT_MUTED);
        add(root, note, 24);

        setContentView(scroll);
    }

    private TextView styledText(int style) {
        TextView view = new TextView(this);
        view.setTextAppearance(style);
        return view;
    }

    private void add(LinearLayout parent, View child, int topMarginDp) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(topMarginDp);
        parent.addView(child, params);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void refresh() {
        boolean ready = RomStore.hasUsableRom(this);
        boolean hasPerm = RomStore.hasStoragePermission(this);

        if (ready) {
            statusView.setText(R.string.rom_status_ready);
            long importedAt = RomStore.getImportedAt(this);
            String when = importedAt > 0
                    ? DateFormat.getMediumDateFormat(this).format(new Date(importedAt))
                            + " " + DateFormat.getTimeFormat(this).format(new Date(importedAt))
                    : "Installed";
            String order = RomStore.getImportedOrder(this);
            String source = order == null ? "z64" : displayOrder(order);
            String path = RomStore.getSourcePath(this);
            if (path == null) path = "Data/baserom.u.z64";

            detailView.setText(getString(R.string.rom_detail_ready, path, source, when));
            primaryButton.setText(R.string.button_play);
            primaryButton.setVisibility(View.VISIBLE);
            primaryButton.setEnabled(true);

            secondaryButton.setText(R.string.button_scan_rom);
            secondaryButton.setVisibility(View.VISIBLE);
            secondaryButton.setEnabled(true);

            tertiaryButton.setText(R.string.button_choose_rom);
            tertiaryButton.setVisibility(View.VISIBLE);
            tertiaryButton.setEnabled(true);
        } else {
            statusView.setText(R.string.rom_status_missing);
            if (!hasPerm) {
                detailView.setText(R.string.storage_permission_needed);
                primaryButton.setText(R.string.button_grant_permission);
                primaryButton.setEnabled(true);

                secondaryButton.setText(R.string.button_choose_rom);
                secondaryButton.setVisibility(View.VISIBLE);
                secondaryButton.setEnabled(true);

                tertiaryButton.setVisibility(View.GONE);
            } else {
                detailView.setText(R.string.rom_detail_missing);
                primaryButton.setText(R.string.button_scan_rom);
                primaryButton.setEnabled(true);

                secondaryButton.setText(R.string.button_choose_rom);
                secondaryButton.setVisibility(View.VISIBLE);
                secondaryButton.setEnabled(true);

                tertiaryButton.setVisibility(View.GONE);
            }
        }
        progress.setVisibility(importing || scanning ? View.VISIBLE : View.GONE);
    }

    private String displayOrder(String order) {
        if ("Z64".equals(order)) {
            return "z64 (big-endian)";
        }
        return order.toLowerCase(Locale.ROOT) + " (converted to .z64)";
    }

    private void onPrimary() {
        if (RomStore.hasUsableRom(this)) {
            startGame();
        } else if (!RomStore.hasStoragePermission(this)) {
            requestStoragePermission();
        } else {
            scanAndAutoImport();
        }
    }

    private void onSecondary() {
        if (RomStore.hasUsableRom(this)) {
            if (!RomStore.hasStoragePermission(this)) {
                requestStoragePermission();
            } else {
                scanAndAutoImport();
            }
        } else {
            openPicker();
        }
    }

    private void startGame() {
        if (!RomStore.hasUsableRom(this)) {
            refresh();
            setError(getString(R.string.rom_detail_missing));
            return;
        }
        startActivity(new Intent(this, MidasActivity.class));
    }

    private void openPicker() {
        if (importing || scanning) {
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(intent, REQUEST_OPEN_ROM);
        } catch (ActivityNotFoundException e) {
            setError(getString(R.string.picker_unavailable));
        }
    }

    private void setError(String message) {
        if (message == null || message.isEmpty()) {
            errorView.setText("");
            errorView.setVisibility(View.GONE);
        } else {
            errorView.setText(message);
            errorView.setVisibility(View.VISIBLE);
        }
    }
}
