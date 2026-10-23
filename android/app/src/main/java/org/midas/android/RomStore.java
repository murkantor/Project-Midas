package org.midas.android;

import android.Manifest;
import android.content.ContentResolver;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.OpenableColumns;
import android.util.Log;


import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * App-private storage, search, and import rules for the ROM.
 *
 * <p>All game files live under {@code getFilesDir()}, which is the path SDL
 * reports through {@code SDL_AndroidGetInternalStoragePath()} and therefore
 * the root the native port uses for midas.ini and Data/.</p>
 */
public final class RomStore {
    private static final String TAG = "MidasConfig";
    private static final String PREFS = "midas_android";
    private static final String KEY_SOURCE_URI = "rom_source_uri";
    private static final String KEY_SOURCE_PATH = "rom_source_path";
    private static final String KEY_IMPORTED_AT = "rom_imported_at";
    private static final String KEY_IMPORTED_ORDER = "rom_imported_order";
    private static final String KEY_IMPORTED_SHA1 = "rom_imported_sha1";
    private static final String KEY_PICKER_SHOWN = "rom_picker_shown";
    private static final String KEY_PERM_REQUESTED = "rom_permission_requested";

    private static final String ROM_NAME = "baserom.u.z64";
    private static final String TEMP_NAME = ".baserom.u.z64.import";

    private RomStore() {
    }

    public static final class ImportResult {
        public final boolean ok;
        public final String message;
        public final RomNormalizer.ByteOrder order;

        private ImportResult(boolean ok, String message, RomNormalizer.ByteOrder order) {
            this.ok = ok;
            this.message = message;
            this.order = order;
        }

        public static ImportResult success(RomNormalizer.ByteOrder order, String message) {
            return new ImportResult(true, message, order);
        }

        public static ImportResult failure(String message) {
            return new ImportResult(false, message, RomNormalizer.ByteOrder.UNKNOWN);
        }
    }

    public static File gameRoot(Context context) {
        return context.getFilesDir();
    }

    public static File dataDir(Context context) {
        return new File(gameRoot(context), "Data");
    }

    public static File romFile(Context context) {
        return new File(dataDir(context), ROM_NAME);
    }

    public static File iniFile(Context context) {
        return new File(gameRoot(context), "midas.ini");
    }

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    public static String getSavedUri(Context context) {
        return prefs(context).getString(KEY_SOURCE_URI, null);
    }

    public static String getSourcePath(Context context) {
        return prefs(context).getString(KEY_SOURCE_PATH, null);
    }

    public static long getImportedAt(Context context) {
        return prefs(context).getLong(KEY_IMPORTED_AT, 0L);
    }

    public static String getImportedOrder(Context context) {
        return prefs(context).getString(KEY_IMPORTED_ORDER, null);
    }

    public static boolean wasPickerShown(Context context) {
        return prefs(context).getBoolean(KEY_PICKER_SHOWN, false);
    }

    public static void markPickerShown(Context context) {
        prefs(context).edit().putBoolean(KEY_PICKER_SHOWN, true).apply();
    }

    public static boolean wasPermissionRequested(Context context) {
        return prefs(context).getBoolean(KEY_PERM_REQUESTED, false);
    }

    public static void markPermissionRequested(Context context) {
        prefs(context).edit().putBoolean(KEY_PERM_REQUESTED, true).apply();
    }

    public static boolean hasStoragePermission(Context context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        } else {
            return context.checkSelfPermission(
                    Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
        }
    }

    /**
     * Fast boot check: the installed image has the expected size and is
     * already in .z64 order. The full SHA-1 is checked when importing; the
     * native loader also rejects a non-stock image before using it.
     */
    public static boolean hasUsableRom(Context context) {
        File rom = romFile(context);
        if (!rom.isFile() || rom.length() != RomNormalizer.ROM_SIZE) {
            return false;
        }
        try (InputStream in = new FileInputStream(rom)) {
            byte[] header = new byte[4];
            int read = readFully(in, header);
            return read == 4 && RomNormalizer.detectOrder(header) == RomNormalizer.ByteOrder.Z64;
        } catch (IOException e) {
            return false;
        }
    }

    /** Removes half-written imports and restores the previous ROM after a crash. */
    public static void recoverInterruptedImport(Context context) {
        File dir = dataDir(context);
        File target = romFile(context);
        File backup = backupFor(target);
        File temp = new File(dir, TEMP_NAME);

        if (!target.exists() && backup.isFile()) {
            if (!backup.renameTo(target)) {
                return;
            }
        }
        //noinspection ResultOfMethodCallIgnored
        temp.delete();
        if (target.exists()) {
            //noinspection ResultOfMethodCallIgnored
            backup.delete();
        }
    }

    /**
     * Adds the Android platform defaults without touching user values.
     */
    public static void ensureAndroidConfig(Context context) {
        File ini = iniFile(context);
        recoverBackup(ini);
        String existing = "";
        try {
            if (ini.isFile()) {
                existing = new String(readAll(ini), StandardCharsets.ISO_8859_1);
            }
            String updated = IniConfig.ensureAndroidDefaults(existing);
            if (!updated.equals(existing)) {
                writeIni(context, updated);
            }
        } catch (IOException e) {
            Log.w(TAG, "Could not apply Android config defaults: " + e.getMessage());
        }
    }

    /**
     * Imports a GoldenEye 007 ROM directly from a File on the device.
     */
    public static ImportResult importFromFile(Context context, File file) {
        if (file == null || !file.exists()) {
            return ImportResult.failure("File not found.");
        }
        if (file.length() != RomNormalizer.ROM_SIZE) {
            return ImportResult.failure(sizeMessage(file.length()));
        }

        recoverInterruptedImport(context);

        final byte[] raw;
        try {
            raw = Files.readAllBytes(file.toPath());
        } catch (IOException | SecurityException e) {
            String detail = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
            return ImportResult.failure("Could not read ROM: " + detail);
        }

        final RomNormalizer.Result normalized;
        try {
            normalized = RomNormalizer.validateAndNormalize(raw);
        } catch (RomNormalizer.RomValidationException e) {
            return ImportResult.failure(e.getMessage());
        }

        File dir = dataDir(context);
        if (!dir.isDirectory() && !dir.mkdirs()) {
            return ImportResult.failure("Could not create the app's Data directory.");
        }

        File temp = new File(dir, TEMP_NAME);
        File target = romFile(context);
        try {
            writeSynced(temp, normalized.getBytes());
            install(temp, target);
        } catch (IOException e) {
            //noinspection ResultOfMethodCallIgnored
            temp.delete();
            String detail = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
            return ImportResult.failure("Could not save the ROM: " + detail);
        }

        String warning = null;
        try {
            updateIni(context, file.getAbsolutePath());
        } catch (IOException e) {
            warning = " The ROM is installed, but midas.ini could not be updated: " + e.getMessage();
        }

        prefs(context).edit()
                .putString(KEY_SOURCE_URI, Uri.fromFile(file).toString())
                .putString(KEY_SOURCE_PATH, file.getAbsolutePath())
                .putLong(KEY_IMPORTED_AT, System.currentTimeMillis())
                .putString(KEY_IMPORTED_ORDER, normalized.getOrder().name())
                .putString(KEY_IMPORTED_SHA1, normalized.getSha1())
                .commit();

        String converted = normalized.getOrder() == RomNormalizer.ByteOrder.Z64
                ? ""
                : " The " + normalized.getOrder().name().toLowerCase(Locale.ROOT)
                        + " image was converted to .z64.";
        return ImportResult.success(normalized.getOrder(),
                "Imported GoldenEye 007 (USA)." + converted + (warning == null ? "" : warning));
    }

    /**
     * Validates a document URI completely, then installs it into app storage.
     */
    public static ImportResult importFromUri(Context context, Uri uri) {
        if (uri == null) {
            return ImportResult.failure("No ROM was selected.");
        }

        recoverInterruptedImport(context);

        final byte[] raw;
        try {
            long declared = querySize(context.getContentResolver(), uri);
            if (declared > 0 && declared != RomNormalizer.ROM_SIZE) {
                return ImportResult.failure(sizeMessage(declared));
            }
            raw = readExactly(context.getContentResolver(), uri);
        } catch (IOException | SecurityException e) {
            String detail = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
            return ImportResult.failure("Could not read the selected ROM: " + detail);
        }

        final RomNormalizer.Result normalized;
        try {
            normalized = RomNormalizer.validateAndNormalize(raw);
        } catch (RomNormalizer.RomValidationException e) {
            return ImportResult.failure(e.getMessage());
        }

        File dir = dataDir(context);
        if (!dir.isDirectory() && !dir.mkdirs()) {
            return ImportResult.failure("Could not create the app's Data directory.");
        }

        File temp = new File(dir, TEMP_NAME);
        File target = romFile(context);
        try {
            writeSynced(temp, normalized.getBytes());
            install(temp, target);
        } catch (IOException e) {
            //noinspection ResultOfMethodCallIgnored
            temp.delete();
            String detail = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
            return ImportResult.failure("Could not save the ROM: " + detail);
        }

        String warning = null;
        try {
            updateIni(context, uri.toString());
        } catch (IOException e) {
            warning = " The ROM is installed, but midas.ini could not be updated: " + e.getMessage();
        }

        prefs(context).edit()
                .putString(KEY_SOURCE_URI, uri.toString())
                .putString(KEY_SOURCE_PATH, uri.toString())
                .putLong(KEY_IMPORTED_AT, System.currentTimeMillis())
                .putString(KEY_IMPORTED_ORDER, normalized.getOrder().name())
                .putString(KEY_IMPORTED_SHA1, normalized.getSha1())
                .commit();

        String converted = normalized.getOrder() == RomNormalizer.ByteOrder.Z64
                ? ""
                : " The " + normalized.getOrder().name().toLowerCase(Locale.ROOT)
                        + " image was converted to .z64.";
        return ImportResult.success(normalized.getOrder(),
                "Imported GoldenEye 007 (USA)." + converted + (warning == null ? "" : warning));
    }

    /**
     * Searches common device directories for a GoldenEye 007 (USA) ROM.
     */
    public static File findRomOnDevice(Context context) {
        List<File> searchDirs = new ArrayList<>();

        // 1. Download directories
        try {
            File dl = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
            if (dl != null && dl.isDirectory()) searchDirs.add(dl);
        } catch (Exception ignored) {
        }

        // 2. Internal shared storage roots and standard folders
        try {
            File ext = Environment.getExternalStorageDirectory();
            if (ext != null && ext.isDirectory()) {
                searchDirs.add(new File(ext, "Download"));
                searchDirs.add(new File(ext, "Downloads"));
                searchDirs.add(new File(ext, "ROMs"));
                searchDirs.add(new File(ext, "ROMs/N64"));
                searchDirs.add(new File(ext, "roms"));
                searchDirs.add(new File(ext, "roms/n64"));
                searchDirs.add(new File(ext, "RetroArch/downloads"));
                searchDirs.add(ext);
            }
        } catch (Exception ignored) {
        }

        // 3. /sdcard direct shortcuts
        File sdcard = new File("/sdcard");
        if (sdcard.isDirectory()) {
            searchDirs.add(new File(sdcard, "Download"));
            searchDirs.add(new File(sdcard, "ROMs/N64"));
            searchDirs.add(new File(sdcard, "ROMs"));
            searchDirs.add(new File(sdcard, "roms/n64"));
            searchDirs.add(new File(sdcard, "roms"));
            searchDirs.add(sdcard);
        }

        // 4. External SD card and secondary storage mounts
        try {
            File[] extDirs = context.getExternalFilesDirs(null);
            if (extDirs != null) {
                for (File d : extDirs) {
                    if (d == null) continue;
                    File parent = d;
                    while (parent != null && parent.getParentFile() != null
                            && !"/storage".equals(parent.getParentFile().getAbsolutePath())
                            && !"/mnt".equals(parent.getParentFile().getAbsolutePath())) {
                        parent = parent.getParentFile();
                    }
                    if (parent != null && parent.isDirectory()) {
                        searchDirs.add(new File(parent, "ROMs"));
                        searchDirs.add(new File(parent, "ROMs/N64"));
                        searchDirs.add(new File(parent, "roms"));
                        searchDirs.add(new File(parent, "roms/n64"));
                        searchDirs.add(new File(parent, "Download"));
                        searchDirs.add(parent);
                    }
                }
            }
        } catch (Exception ignored) {
        }

        // Priority exact names to check first
        String[] knownNames = {
            "GoldenEye 007 (USA).z64",
            "GoldenEye 007 (U) [!].z64",
            "Goldeneye 007 (USA).z64",
            "goldeneye 007 (usa).z64",
            "GoldenEye 007.z64",
            "Goldeneye.z64",
            "GoldenEye 007 (USA).v64",
            "GoldenEye 007 (USA).n64",
            "baserom.u.z64",
            "ge007.z64"
        };

        Set<String> visited = new HashSet<>();

        // Fast pass: exact matches
        for (File dir : searchDirs) {
            if (dir == null || !dir.isDirectory()) continue;
            String path = dir.getAbsolutePath();
            if (!visited.add(path)) continue;

            for (String name : knownNames) {
                File candidate = new File(dir, name);
                if (isPotentialRom(candidate)) {
                    Log.i(TAG, "Fast match found: " + candidate.getAbsolutePath());
                    return candidate;
                }
            }
        }

        // Shallow recursive pass (up to 2 levels deep) in ROMs and Download folders
        for (File dir : searchDirs) {
            if (dir == null || !dir.isDirectory()) continue;
            File found = scanDirectoryForRom(dir, 2);
            if (found != null) {
                Log.i(TAG, "Recursive match found: " + found.getAbsolutePath());
                return found;
            }
        }

        return null;
    }

    private static File scanDirectoryForRom(File dir, int depth) {
        if (dir == null || !dir.isDirectory() || depth < 0) {
            return null;
        }
        File[] files = dir.listFiles();
        if (files == null) {
            return null;
        }

        // Check files in current dir
        for (File f : files) {
            if (f.isFile()) {
                String name = f.getName().toLowerCase(Locale.ROOT);
                if ((name.endsWith(".z64") || name.endsWith(".v64") || name.endsWith(".n64"))
                        && (name.contains("goldeneye") || name.contains("007") || name.contains("midas") || name.contains("baserom"))) {
                    if (isPotentialRom(f)) {
                        return f;
                    }
                }
            }
        }

        // Recurse subdirectories
        if (depth > 0) {
            for (File f : files) {
                if (f.isDirectory() && !f.isHidden() && !f.getName().startsWith(".")) {
                    String sub = f.getName().toLowerCase(Locale.ROOT);
                    if (sub.contains("rom") || sub.contains("n64") || sub.contains("game") || sub.contains("download")) {
                        File found = scanDirectoryForRom(f, depth - 1);
                        if (found != null) {
                            return found;
                        }
                    }
                }
            }
        }

        return null;
    }

    private static boolean isPotentialRom(File file) {
        if (!file.isFile() || file.length() != RomNormalizer.ROM_SIZE) {
            return false;
        }
        try (InputStream in = new FileInputStream(file)) {
            byte[] header = new byte[4];
            int read = readFully(in, header);
            return read == 4 && RomNormalizer.detectOrder(header) != RomNormalizer.ByteOrder.UNKNOWN;
        } catch (IOException e) {
            return false;
        }
    }

    private static String sizeMessage(long size) {
        return "ROM is " + size + " bytes. GoldenEye 007 (U) must be exactly 12 MiB ("
                + RomNormalizer.ROM_SIZE + " bytes); truncated or padded images are not accepted.";
    }

    private static long querySize(ContentResolver resolver, Uri uri) {
        try (Cursor cursor = resolver.query(uri, new String[]{OpenableColumns.SIZE},
                null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.SIZE);
                if (index >= 0 && !cursor.isNull(index)) {
                    return cursor.getLong(index);
                }
            }
        } catch (RuntimeException ignored) {
        }
        return -1;
    }

    private static byte[] readExactly(ContentResolver resolver, Uri uri) throws IOException {
        try (InputStream in = resolver.openInputStream(uri)) {
            if (in == null) {
                throw new IOException("the provider returned no stream");
            }
            byte[] data = new byte[RomNormalizer.ROM_SIZE];
            int offset = 0;
            while (offset < data.length) {
                int read = in.read(data, offset, data.length - offset);
                if (read < 0) {
                    throw new IOException(sizeMessage(offset));
                }
                if (read == 0) {
                    int one = in.read();
                    if (one < 0) {
                        throw new IOException(sizeMessage(offset));
                    }
                    data[offset++] = (byte) one;
                    continue;
                }
                offset += read;
            }
            if (in.read() != -1) {
                throw new IOException("the selected file is larger than 12 MiB ("
                        + RomNormalizer.ROM_SIZE + " bytes).");
            }
            return data;
        }
    }

    private static void writeSynced(File file, byte[] data) throws IOException {
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(data);
            out.flush();
            out.getFD().sync();
        }
    }

    private static void install(File temp, File target) throws IOException {
        try {
            Files.move(temp.toPath(), target.toPath(),
                    StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            return;
        } catch (IOException | UnsupportedOperationException ignored) {
        }

        File backup = backupFor(target);
        Files.deleteIfExists(backup.toPath());
        boolean hadTarget = target.exists();
        if (hadTarget) {
            Files.move(target.toPath(), backup.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
        try {
            Files.move(temp.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
            if (hadTarget) {
                Files.deleteIfExists(backup.toPath());
            }
        } catch (IOException e) {
            if (hadTarget && backup.exists()) {
                try {
                    Files.move(backup.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
                } catch (IOException ignored) {
                }
            }
            throw e;
        }
    }

    /**
     * Reads midas.ini as ISO-8859-1, adds platform defaults, and sets the ROM keys.
     */
    private static void updateIni(Context context, String sourcePath) throws IOException {
        File ini = iniFile(context);
        recoverBackup(ini);
        String existing = "";
        if (ini.isFile()) {
            existing = new String(readAll(ini), StandardCharsets.ISO_8859_1);
        }
        String updated = IniConfig.ensureAndroidDefaults(
                IniConfig.setDataRom(existing, IniConfig.ROM_KEY_VALUE));
        if (sourcePath != null && !sourcePath.isEmpty()) {
            updated = IniConfig.setSourceRom(updated, sourcePath);
        }
        if (updated.equals(existing)) {
            return;
        }
        writeIni(context, updated);
    }

    private static void writeIni(Context context, String content) throws IOException {
        File ini = iniFile(context);
        File temp = new File(ini.getParentFile(), ".midas.ini.import");
        try {
            writeSynced(temp, content.getBytes(StandardCharsets.ISO_8859_1));
            install(temp, ini);
        } catch (IOException e) {
            //noinspection ResultOfMethodCallIgnored
            temp.delete();
            throw e;
        }
    }

    private static File backupFor(File target) {
        return new File(target.getParentFile(), "." + target.getName() + ".bak");
    }

    private static void recoverBackup(File target) {
        File backup = backupFor(target);
        if (!target.exists() && backup.isFile()) {
            //noinspection ResultOfMethodCallIgnored
            backup.renameTo(target);
        } else if (target.exists() && backup.isFile()) {
            //noinspection ResultOfMethodCallIgnored
            backup.delete();
        }
    }

    private static byte[] readAll(File file) throws IOException {
        try (InputStream in = new FileInputStream(file)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream((int) Math.max(256, file.length()));
            byte[] buffer = new byte[8192];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            return out.toByteArray();
        }
    }

    private static int readFully(InputStream in, byte[] out) throws IOException {
        int offset = 0;
        while (offset < out.length) {
            int read = in.read(out, offset, out.length - offset);
            if (read < 0) {
                break;
            }
            offset += read;
        }
        return offset;
    }
}
