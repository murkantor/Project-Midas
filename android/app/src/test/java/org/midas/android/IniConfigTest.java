package org.midas.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class IniConfigTest {
    @Test
    public void replacesOnlyTheDataRomKey() {
        String original = "; keep this comment\n"
                + "[graphics]\n"
                + "resolution = 640x480\n"
                + "\n"
                + "[data]\n"
                + "rom = old.z64\n"
                + "texture_cache = 1\n"
                + "\n"
                + "[input]\n"
                + "key_a = A\n";
        String updated = IniConfig.setDataRom(original, IniConfig.ROM_KEY_VALUE);

        assertEquals("; keep this comment\n"
                + "[graphics]\n"
                + "resolution = 640x480\n"
                + "\n"
                + "[data]\n"
                + "rom = Data/baserom.u.z64\n"
                + "texture_cache = 1\n"
                + "\n"
                + "[input]\n"
                + "key_a = A\n", updated);
    }

    @Test
    public void addsDataSectionWithoutDroppingExistingSettings() {
        String original = "vsync = 1\n\n[audio]\nvolume = 8\n";
        String updated = IniConfig.setDataRom(original, IniConfig.ROM_KEY_VALUE);

        assertEquals("vsync = 1\n\n[audio]\nvolume = 8\n\n[data]\nrom = Data/baserom.u.z64\n",
                updated);
    }

    @Test
    public void addsRomToAnExistingEmptyDataSection() {
        String original = "[graphics]\nvsync = 1\n\n[data]\n\n[input]\nkey_a = A\n";
        String updated = IniConfig.setDataRom(original, IniConfig.ROM_KEY_VALUE);

        assertEquals("[graphics]\nvsync = 1\n\n[data]\nrom = Data/baserom.u.z64\n\n[input]\nkey_a = A\n",
                updated);
    }

    @Test
    public void preservesCrLf() {
        String original = "[data]\r\nrom = old.z64\r\naudio = 1\r\n";
        String updated = IniConfig.setDataRom(original, IniConfig.ROM_KEY_VALUE);

        assertEquals("[data]\r\nrom = Data/baserom.u.z64\r\naudio = 1\r\n", updated);
        assertFalse(updated.replace("\r\n", "").contains("\n"));
    }

    @Test
    public void isIdempotent() {
        String once = IniConfig.setDataRom("[data]\nrom = old.z64\n", IniConfig.ROM_KEY_VALUE);
        String twice = IniConfig.setDataRom(once, IniConfig.ROM_KEY_VALUE);
        assertEquals(once, twice);
    }

    @Test
    public void emptyFileBecomesAValidIni() {
        String updated = IniConfig.setDataRom("", IniConfig.ROM_KEY_VALUE);
        assertEquals("[data]\nrom = Data/baserom.u.z64\n", updated);
    }

    @Test
    public void androidDefaultsAreAddedToADataOnlyIni() {
        String original = "[data]\nrom = Data/baserom.u.z64\n";
        String updated = IniConfig.ensureAndroidDefaults(original);

        assertTrue(updated.contains("[data]\nrom = Data/baserom.u.z64\n"));
        assertTrue(updated.contains("[display]\nfullscreen = fullscreen\n"));
        assertTrue(updated.contains("[input]\nmouse = 0\n"));
        assertTrue(updated.contains("p1_hybrid = 0\n"));
        assertTrue(updated.contains("pads = 1\n"));
        assertTrue(updated.contains("maxfps = 60\n"));
    }

    @Test
    public void androidDefaultsNeverOverwriteUserValues() {
        String original = "[display]\nmaxfps = 30\nfullscreen = windowed\n"
                + "[input]\nmouse = 1\np1_hybrid = 1\npads = 4\n";
        String updated = IniConfig.ensureAndroidDefaults(original);

        assertEquals(original, updated);
    }

    @Test
    public void androidDefaultsAreIdempotentAndPreserveOtherKeys() {
        String once = IniConfig.ensureAndroidDefaults("[data]\nrom = Data/baserom.u.z64\n");
        String twice = IniConfig.ensureAndroidDefaults(once);
        assertEquals(once, twice);
        assertTrue(once.contains("rom = Data/baserom.u.z64"));
    }

    @Test
    public void androidDefaultsPreserveCrLf() {
        String updated = IniConfig.ensureAndroidDefaults("[data]\r\nrom = old.z64\r\n");
        assertFalse(updated.replace("\r\n", "").contains("\n"));
        assertTrue(updated.contains("[display]\r\nfullscreen = fullscreen\r\n"));
        assertTrue(updated.contains("[input]\r\nmouse = 0\r\n"));
    }

    @Test
    public void sectionNamesRemainCaseSensitiveLikeTheNativeParser() {
        String original = "[Data]\nrom = keep.z64\n";
        String updated = IniConfig.setDataRom(original, IniConfig.ROM_KEY_VALUE);

        assertTrue(updated.startsWith("[Data]\nrom = keep.z64\n"));
        assertTrue(updated.contains("\n[data]\nrom = Data/baserom.u.z64\n"));
    }
}
