package org.midas.android;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import org.junit.Assume;
import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;

/**
 * Pure-JVM tests for the import rules. No ROM data is stored in the repository;
 * the synthetic images below deliberately fail the stock SHA-1 check.
 */
public class RomNormalizerTest {
    private static final byte[] Z64_MAGIC = {(byte) 0x80, 0x37, 0x12, 0x40};
    private static final byte[] V64_MAGIC = {0x37, (byte) 0x80, 0x40, 0x12};
    private static final byte[] N64_MAGIC = {0x40, 0x12, 0x37, (byte) 0x80};

    private static byte[] syntheticZ64() {
        byte[] rom = new byte[RomNormalizer.ROM_SIZE];
        System.arraycopy(Z64_MAGIC, 0, rom, 0, 4);
        System.arraycopy("GOLDENEYE".getBytes(java.nio.charset.StandardCharsets.US_ASCII),
                0, rom, 0x20, 9);
        rom[0x3B] = 'N';
        rom[0x3C] = 'G';
        rom[0x3D] = 'E';
        for (int i = 0x1000; i < rom.length; i++) {
            rom[i] = (byte) (i * 31 + (i >>> 8));
        }
        return rom;
    }

    private static byte[] asV64(byte[] z64) {
        byte[] out = z64.clone();
        for (int i = 0; i + 1 < out.length; i += 2) {
            byte t = out[i];
            out[i] = out[i + 1];
            out[i + 1] = t;
        }
        return out;
    }

    private static byte[] asN64(byte[] z64) {
        byte[] out = z64.clone();
        for (int i = 0; i + 3 < out.length; i += 4) {
            byte t0 = out[i];
            byte t1 = out[i + 1];
            out[i] = out[i + 3];
            out[i + 1] = out[i + 2];
            out[i + 2] = t1;
            out[i + 3] = t0;
        }
        return out;
    }

    @Test
    public void detectsAllThreeByteOrders() {
        assertEquals(RomNormalizer.ByteOrder.Z64, RomNormalizer.detectOrder(Z64_MAGIC));
        assertEquals(RomNormalizer.ByteOrder.V64, RomNormalizer.detectOrder(V64_MAGIC));
        assertEquals(RomNormalizer.ByteOrder.N64, RomNormalizer.detectOrder(N64_MAGIC));
        assertEquals(RomNormalizer.ByteOrder.UNKNOWN, RomNormalizer.detectOrder(new byte[3]));
    }

    @Test
    public void z64IsUnchanged() {
        byte[] rom = syntheticZ64();
        byte[] copy = rom.clone();
        RomNormalizer.normalizeInPlace(rom, RomNormalizer.ByteOrder.Z64);
        assertArrayEquals(copy, rom);
    }

    @Test
    public void v64ByteSwapsBackToZ64() {
        byte[] z64 = syntheticZ64();
        byte[] v64 = asV64(z64);
        assertEquals(RomNormalizer.ByteOrder.V64, RomNormalizer.detectOrder(v64));

        RomNormalizer.normalizeInPlace(v64, RomNormalizer.ByteOrder.V64);

        assertEquals(RomNormalizer.ByteOrder.Z64, RomNormalizer.detectOrder(v64));
        assertArrayEquals(z64, v64);
    }

    @Test
    public void n64WordSwapsBackToZ64() {
        byte[] z64 = syntheticZ64();
        byte[] n64 = asN64(z64);
        assertEquals(RomNormalizer.ByteOrder.N64, RomNormalizer.detectOrder(n64));

        RomNormalizer.normalizeInPlace(n64, RomNormalizer.ByteOrder.N64);

        assertEquals(RomNormalizer.ByteOrder.Z64, RomNormalizer.detectOrder(n64));
        assertArrayEquals(z64, n64);
    }

    @Test
    public void rejectsTruncatedImage() {
        byte[] truncated = Arrays.copyOf(syntheticZ64(), RomNormalizer.ROM_SIZE - 1);
        RomNormalizer.RomValidationException error = assertThrows(
                RomNormalizer.RomValidationException.class,
                () -> RomNormalizer.validateAndNormalize(truncated));
        assertTrue(error.getMessage(), error.getMessage().contains("12 MiB"));
    }

    @Test
    public void rejectsOversizedImage() {
        byte[] oversized = Arrays.copyOf(syntheticZ64(), RomNormalizer.ROM_SIZE + 1);
        RomNormalizer.RomValidationException error = assertThrows(
                RomNormalizer.RomValidationException.class,
                () -> RomNormalizer.validateAndNormalize(oversized));
        assertTrue(error.getMessage(), error.getMessage().contains("12 MiB"));
    }

    @Test
    public void rejectsUnknownHeader() {
        byte[] notARom = new byte[RomNormalizer.ROM_SIZE];
        RomNormalizer.RomValidationException error = assertThrows(
                RomNormalizer.RomValidationException.class,
                () -> RomNormalizer.validateAndNormalize(notARom));
        assertTrue(error.getMessage(), error.getMessage().contains("Not an N64 ROM image"));
    }

    @Test
    public void rejectsWrongRomWithAValidN64Header() {
        byte[] wrong = syntheticZ64();
        RomNormalizer.RomValidationException error = assertThrows(
                RomNormalizer.RomValidationException.class,
                () -> RomNormalizer.validateAndNormalize(wrong));
        assertTrue(error.getMessage(), error.getMessage().contains("SHA-1"));
        assertTrue(error.getMessage(), error.getMessage().contains("stock GoldenEye 007 (USA)"));
    }

    @Test
    public void sha1MatchesKnownVector() throws IOException {
        byte[] empty = new byte[0];
        assertEquals("da39a3ee5e6b4b0d3255bfef95601890afd80709",
                RomNormalizer.sha1Hex(empty));
        assertEquals(RomNormalizer.sha1Hex(empty),
                RomNormalizer.sha1Hex(new ByteArrayInputStream(empty)));
    }

    @Test
    public void optionalLocalUsaRomValidatesInEveryByteOrder() throws Exception {
        String path = System.getenv("MIDAS_TEST_ROM_PATH");
        Assume.assumeTrue("No user-supplied local ROM", path != null && !path.isEmpty());
        byte[] z64 = Files.readAllBytes(Paths.get(path));
        assertEquals(RomNormalizer.STOCK_SHA1, RomNormalizer.sha1Hex(z64));
        assertArrayEquals(z64, RomNormalizer.validateAndNormalize(z64.clone()).getBytes());
        assertArrayEquals(z64, RomNormalizer.validateAndNormalize(asV64(z64)).getBytes());
        assertArrayEquals(z64, RomNormalizer.validateAndNormalize(asN64(z64)).getBytes());
    }

    @Test
    public void stockSha1MatchesTheRepositoryReference() throws IOException {
        Path dir = Paths.get(System.getProperty("user.dir")).toAbsolutePath();
        Path reference = null;
        for (int i = 0; i < 8 && dir != null; i++, dir = dir.getParent()) {
            Path candidate = dir.resolve("ge007.u.sha1");
            if (Files.isRegularFile(candidate)) {
                reference = candidate;
                break;
            }
        }
        Assume.assumeTrue("ge007.u.sha1 not present (running outside the repo)", reference != null);

        String line = Files.readAllLines(reference, StandardCharsets.US_ASCII).get(0);
        assertEquals(RomNormalizer.STOCK_SHA1, line.trim().split("\\s+")[0].toLowerCase());
    }
}
