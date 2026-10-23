package org.midas.android;

import android.content.Context;
import android.content.res.AssetManager;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Installs the runtime text tables that ship in the APK beside the native
 * private root. Files are copied before native entry, and a checksum manifest
 * keeps user-edited copies across app updates.
 */
final class GameData {
    private static final String TAG = "MidasAssets";
    private static final String BUNDLED_MANIFEST = "support-files.sha256";
    private static final String INSTALLED_MANIFEST = ".midas-support.sha256";

    private static final SupportFile[] FILES = {
            new SupportFile("gamecontrollerdb.txt", "gamecontrollerdb.txt"),
            new SupportFile("gamecontrollerdefault.txt", "gamecontrollerdefault.txt"),
            new SupportFile("midas_ini_help.txt", "midas_ini_help.txt"),
            new SupportFile("shaders_base.txt", "Data/shaders_base.txt"),
            new SupportFile("Data_README.txt", "Data/README"),
            new SupportFile("Mods_README.txt", "Mods/README"),
            new SupportFile("LICENSE.txt", "LICENSE.txt"),
            new SupportFile("THIRD_PARTY_NOTICES.txt", "THIRD_PARTY_NOTICES.txt")
    };

    private static final class SupportFile {
        final String asset;
        final String destination;

        SupportFile(String asset, String destination) {
            this.asset = asset;
            this.destination = destination;
        }
    }

    private GameData() {
    }

    static void ensureInstalled(Context context) {
        AssetManager assets = context.getAssets();
        File root = RomStore.gameRoot(context);

        Map<String, String> bundled = readManifest(assets, BUNDLED_MANIFEST);
        if (bundled.isEmpty()) {
            bundled = hashBundledFiles(assets);
        }
        Map<String, String> installed = readManifestFile(new File(root, INSTALLED_MANIFEST));
        Map<String, String> next = new LinkedHashMap<>();

        for (SupportFile file : FILES) {
            String bundledHash = bundled.get(file.asset);
            File destination = new File(root, file.destination);
            // Treat an empty leftover as missing so a crash cannot pin an
            // unreadable zero-byte table in place.
            String actualHash = destination.isFile() && destination.length() > 0
                    ? sha256(destination) : null;
            String previousHash = installed.get(file.asset);

            if (actualHash == null) {
                try {
                    copyAsset(assets, file.asset, destination);
                    String installedHash = bundledHash != null ? bundledHash : sha256(destination);
                    next.put(file.asset, installedHash);
                } catch (IOException e) {
                    Log.w(TAG, "Could not install " + file.asset + ": " + e.getMessage());
                    if (previousHash != null) {
                        next.put(file.asset, previousHash);
                    }
                }
            } else if (bundledHash == null) {
                // No manifest entry: leave an existing file alone.
                next.put(file.asset, previousHash == null ? actualHash : previousHash);
            } else if (actualHash.equals(bundledHash)) {
                next.put(file.asset, bundledHash);
            } else if (previousHash != null && actualHash.equals(previousHash)) {
                try {
                    copyAsset(assets, file.asset, destination);
                    next.put(file.asset, bundledHash);
                } catch (IOException e) {
                    Log.w(TAG, "Could not update " + file.asset + ": " + e.getMessage());
                    next.put(file.asset, previousHash);
                }
            } else {
                // User-edited (or a legacy install with no manifest): keep the
                // copy. Recording the bundled hash as the baseline ensures a
                // later update still sees it as edited.
                next.put(file.asset, previousHash == null ? bundledHash : previousHash);
                Log.i(TAG, "Preserving user-edited " + file.destination);
            }
        }

        writeManifestIfChanged(root, next);
    }

    private static Map<String, String> readManifest(AssetManager assets, String name) {
        Map<String, String> out = new LinkedHashMap<>();
        try (InputStream in = assets.open(name)) {
            parseManifest(in, out);
        } catch (IOException e) {
            Log.w(TAG, "Missing support-file manifest: " + e.getMessage());
        }
        return out;
    }

    private static Map<String, String> readManifestFile(File file) {
        Map<String, String> out = new LinkedHashMap<>();
        if (!file.isFile()) {
            return out;
        }
        try (InputStream in = Files.newInputStream(file.toPath())) {
            parseManifest(in, out);
        } catch (IOException e) {
            Log.w(TAG, "Could not read " + INSTALLED_MANIFEST + ": " + e.getMessage());
        }
        return out;
    }

    private static void parseManifest(InputStream in, Map<String, String> out) throws IOException {
        byte[] data = readAll(in);
        String[] lines = new String(data, StandardCharsets.UTF_8).split("\r\n|\n|\r");
        for (String line : lines) {
            String trimmed = line.trim();
            if (trimmed.isEmpty() || trimmed.startsWith("#")) {
                continue;
            }
            int space = trimmed.indexOf(' ');
            if (space <= 0) {
                continue;
            }
            String hash = trimmed.substring(0, space).trim();
            String name = trimmed.substring(space + 1).trim();
            if (!hash.isEmpty() && !name.isEmpty()) {
                out.put(name, hash.toLowerCase(java.util.Locale.ROOT));
            }
        }
    }

    private static Map<String, String> hashBundledFiles(AssetManager assets) {
        Map<String, String> out = new LinkedHashMap<>();
        for (SupportFile file : FILES) {
            try (InputStream in = assets.open(file.asset)) {
                out.put(file.asset, sha256(in));
            } catch (IOException e) {
                Log.w(TAG, "Missing bundled " + file.asset + ": " + e.getMessage());
            }
        }
        return out;
    }

    private static void copyAsset(AssetManager assets, String assetName, File destination)
            throws IOException {
        File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("could not create " + parent);
        }
        File temp = new File(destination.getPath() + ".import");
        try {
            try (InputStream in = assets.open(assetName);
                 FileOutputStream out = new FileOutputStream(temp)) {
                byte[] buffer = new byte[16 * 1024];
                int read;
                while ((read = in.read(buffer)) != -1) {
                    out.write(buffer, 0, read);
                }
                out.flush();
                out.getFD().sync();
            }
            try {
                Files.move(temp.toPath(), destination.toPath(),
                        StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            } catch (IOException | UnsupportedOperationException e) {
                Files.move(temp.toPath(), destination.toPath(),
                        StandardCopyOption.REPLACE_EXISTING);
            }
        } finally {
            //noinspection ResultOfMethodCallIgnored
            temp.delete();
        }
    }

    private static void writeManifestIfChanged(File root, Map<String, String> hashes) {
        StringBuilder out = new StringBuilder();
        for (SupportFile file : FILES) {
            String hash = hashes.get(file.asset);
            if (hash != null) {
                out.append(hash).append("  ").append(file.asset).append('\n');
            }
        }
        File manifest = new File(root, INSTALLED_MANIFEST);
        byte[] bytes = out.toString().getBytes(StandardCharsets.UTF_8);
        try {
            if (manifest.isFile() && sha256(manifest).equals(sha256(bytes))) {
                return;
            }
            File temp = new File(root, "." + INSTALLED_MANIFEST + ".import");
            try (FileOutputStream stream = new FileOutputStream(temp)) {
                stream.write(bytes);
                stream.flush();
                stream.getFD().sync();
            }
            try {
                Files.move(temp.toPath(), manifest.toPath(),
                        StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            } catch (IOException | UnsupportedOperationException e) {
                Files.move(temp.toPath(), manifest.toPath(),
                        StandardCopyOption.REPLACE_EXISTING);
            }
        } catch (IOException e) {
            Log.w(TAG, "Could not write " + INSTALLED_MANIFEST + ": " + e.getMessage());
        }
    }

    private static byte[] readAll(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[16 * 1024];
        int read;
        while ((read = in.read(buffer)) != -1) {
            out.write(buffer, 0, read);
        }
        return out.toByteArray();
    }

    private static String sha256(File file) {
        try (InputStream in = Files.newInputStream(file.toPath())) {
            return sha256(in);
        } catch (IOException e) {
            return "";
        }
    }

    private static String sha256(byte[] bytes) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            return toHex(digest.digest(bytes));
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-256 is required by the JVM", e);
        }
    }

    private static String sha256(InputStream in) throws IOException {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) != -1) {
                digest.update(buffer, 0, read);
            }
            return toHex(digest.digest());
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-256 is required by the JVM", e);
        }
    }

    private static String toHex(byte[] bytes) {
        char[] out = new char[bytes.length * 2];
        final char[] digits = "0123456789abcdef".toCharArray();
        for (int i = 0; i < bytes.length; i++) {
            int value = bytes[i] & 0xFF;
            out[i * 2] = digits[value >>> 4];
            out[i * 2 + 1] = digits[value & 0x0F];
        }
        return new String(out);
    }
}
