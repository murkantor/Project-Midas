package org.midas.android;

import java.util.ArrayList;
import java.util.List;

/**
 * Minimal, comment-preserving editor for the port's midas.ini.
 *
 * <p>The native loader reads {@code [data] rom=} from midas.ini. Importing a
 * ROM must add or replace that single key while leaving every other section,
 * key, comment and the file's line ending style untouched.</p>
 */
public final class IniConfig {
    public static final String ROM_KEY_VALUE = "Data/baserom.u.z64";

    private IniConfig() {
    }

    /**
     * Android defaults applied only when the key is absent. The rest of the
     * native midas.ini option table is left untouched (and remains available);
     * an existing user value always wins.
     */
    public static String ensureAndroidDefaults(String original) {
        String result = original == null ? "" : original;
        result = ensureKey(result, "[display]", "fullscreen", "fullscreen");
        result = ensureKey(result, "[display]", "maxfps", "60");
        result = ensureKey(result, "[input]", "mouse", "0");
        result = ensureKey(result, "[input]", "p1_hybrid", "0");
        result = ensureKey(result, "[input]", "pads", "1");
        return result;
    }

    public static String setDataRom(String original, String romValue) {
        return setSectionKey(original, "[data]", "rom", romValue);
    }

    public static String setSourceRom(String original, String sourceRom) {
        return setSectionKey(original, "[data]", "source_rom", sourceRom);
    }

    public static String setSectionKey(String original, String section, String key, String value) {
        if (original == null) {
            original = "";
        }
        if (value == null || value.indexOf('\n') >= 0 || value.indexOf('\r') >= 0) {
            throw new IllegalArgumentException("Invalid midas.ini value: " + value);
        }

        String eol = original.contains("\r\n") ? "\r\n" : "\n";
        String[] rawLines = original.split("\r\n|\n|\r", -1);
        boolean trailingNewline = rawLines.length > 0 && rawLines[rawLines.length - 1].isEmpty();
        int lineCount = trailingNewline ? rawLines.length - 1 : rawLines.length;

        List<String> lines = new ArrayList<>(lineCount + 3);
        for (int i = 0; i < lineCount; i++) {
            lines.add(rawLines[i]);
        }

        int header = -1;
        int sectionEnd = -1;
        int targetLine = -1;
        boolean inSection = false;

        for (int i = 0; i < lineCount; i++) {
            String trimmed = lines.get(i).trim();
            if (trimmed.startsWith("[")) {
                if (trimmed.equals(section)) {
                    if (header < 0) {
                        header = i;
                        sectionEnd = i + 1;
                    }
                    inSection = true;
                } else {
                    inSection = false;
                }
                continue;
            }
            if (inSection) {
                sectionEnd = i + 1;
                int eq = trimmed.indexOf('=');
                if (eq >= 0 && trimmed.substring(0, eq).trim().equals(key)) {
                    targetLine = i;
                }
            }
        }

        String lineText = key + " = " + value;
        if (targetLine >= 0) {
            lines.set(targetLine, lineText);
        } else if (header >= 0) {
            while (sectionEnd > header + 1 && lines.get(sectionEnd - 1).trim().isEmpty()) {
                sectionEnd--;
            }
            lines.add(sectionEnd, lineText);
        } else {
            if (!lines.isEmpty() && !lines.get(lines.size() - 1).trim().isEmpty()) {
                lines.add("");
            }
            lines.add(section);
            lines.add(lineText);
        }

        return render(original, eol, trailingNewline, lines);
    }

    private static String ensureKey(String original, String section, String key, String value) {
        String eol = original.contains("\r\n") ? "\r\n" : "\n";
        String[] rawLines = original.split("\r\n|\n|\r", -1);
        boolean trailingNewline = rawLines.length > 0 && rawLines[rawLines.length - 1].isEmpty();
        int lineCount = trailingNewline ? rawLines.length - 1 : rawLines.length;

        List<String> lines = new ArrayList<>(lineCount + 4);
        for (int i = 0; i < lineCount; i++) {
            lines.add(rawLines[i]);
        }

        int header = -1;
        int insertAt = -1;
        boolean inTarget = false;
        boolean keyFound = false;

        for (int i = 0; i < lineCount; i++) {
            String trimmed = lines.get(i).trim();
            if (trimmed.startsWith("[")) {
                inTarget = trimmed.equals(section);
                if (inTarget) {
                    header = i;
                    insertAt = i + 1;
                }
                continue;
            }
            if (inTarget) {
                insertAt = i + 1;
                int eq = trimmed.indexOf('=');
                if (eq >= 0 && trimmed.substring(0, eq).trim().equals(key)) {
                    keyFound = true;
                }
            }
        }

        if (keyFound) {
            return original;
        }

        String keyLine = key + " = " + value;
        if (header >= 0) {
            // Insert above trailing blank lines and the next section.
            while (insertAt > header + 1 && lines.get(insertAt - 1).trim().isEmpty()) {
                insertAt--;
            }
            lines.add(insertAt, keyLine);
        } else {
            if (!lines.isEmpty() && !lines.get(lines.size() - 1).trim().isEmpty()) {
                lines.add("");
            }
            lines.add(section);
            lines.add(keyLine);
        }
        return render(original, eol, trailingNewline, lines);
    }

    private static String render(String original, String eol, boolean trailingNewline, List<String> lines) {
        StringBuilder out = new StringBuilder(original.length() + 64);
        for (int i = 0; i < lines.size(); i++) {
            if (i > 0) {
                out.append(eol);
            }
            out.append(lines.get(i));
        }
        if (trailingNewline && !lines.isEmpty()) {
            out.append(eol);
        }
        return out.toString();
    }
}
