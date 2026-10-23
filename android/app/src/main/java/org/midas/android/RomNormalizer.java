package org.midas.android;

import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;

/**
 * Validates and byte-normalises a GoldenEye 007 (USA) N64 cartridge image.
 *
 * <p>This class is deliberately free of Android imports so the import rules can
 * be exercised by plain JVM unit tests without shipping ROM data.</p>
 */
public final class RomNormalizer {
    /** A stock GoldenEye 007 (U) cart image: 0xC00000 bytes (12 MiB). */
    public static final int ROM_SIZE = 12 * 1024 * 1024;

    /** SHA-1 of the stock (U) image after conversion to .z64 byte order. */
    public static final String STOCK_SHA1 = "abe01e4aeb033b6c0836819f549c791b26cfde83";

    public enum ByteOrder {
        /** Big-endian 80 37 12 40. */
        Z64,
        /** Byte-swapped 37 80 40 12. */
        V64,
        /** Little-endian 40 12 37 80. */
        N64,
        UNKNOWN
    }

    public static final class Result {
        private final ByteOrder order;
        private final byte[] bytes;
        private final String sha1;

        Result(ByteOrder order, byte[] bytes, String sha1) {
            this.order = order;
            this.bytes = bytes;
            this.sha1 = sha1;
        }

        /** Byte order of the input file, before normalisation. */
        public ByteOrder getOrder() {
            return order;
        }

        /** The same array that was passed in, now in .z64 byte order. */
        public byte[] getBytes() {
            return bytes;
        }

        public String getSha1() {
            return sha1;
        }
    }

    public static final class RomValidationException extends Exception {
        public RomValidationException(String message) {
            super(message);
        }
    }

    private RomNormalizer() {
    }

    public static ByteOrder detectOrder(byte[] data) {
        if (data == null || data.length < 4) {
            return ByteOrder.UNKNOWN;
        }
        int b0 = data[0] & 0xFF;
        int b1 = data[1] & 0xFF;
        int b2 = data[2] & 0xFF;
        int b3 = data[3] & 0xFF;
        if (b0 == 0x80 && b1 == 0x37 && b2 == 0x12 && b3 == 0x40) {
            return ByteOrder.Z64;
        }
        if (b0 == 0x37 && b1 == 0x80 && b2 == 0x40 && b3 == 0x12) {
            return ByteOrder.V64;
        }
        if (b0 == 0x40 && b1 == 0x12 && b2 == 0x37 && b3 == 0x80) {
            return ByteOrder.N64;
        }
        return ByteOrder.UNKNOWN;
    }

    /**
     * Validates size, header byte order and the stock ROM SHA-1, converting the
     * supplied array to .z64 in place. Nothing is written anywhere: callers can
     * still abandon a failed import without touching an existing ROM.
     */
    public static Result validateAndNormalize(byte[] raw) throws RomValidationException {
        if (raw == null) {
            throw new RomValidationException("No ROM data was read.");
        }
        if (raw.length != ROM_SIZE) {
            throw new RomValidationException(String.format(Locale.ROOT,
                    "ROM is %d bytes. GoldenEye 007 (U) must be exactly 12 MiB (%d bytes); truncated or padded images are not accepted.",
                    raw.length, ROM_SIZE));
        }

        ByteOrder order = detectOrder(raw);
        if (order == ByteOrder.UNKNOWN) {
            throw new RomValidationException(String.format(Locale.ROOT,
                    "Not an N64 ROM image (header %02X %02X %02X %02X). Expected .z64, .v64 or .n64 byte order.",
                    raw[0] & 0xFF, raw[1] & 0xFF, raw[2] & 0xFF, raw[3] & 0xFF));
        }

        normalizeInPlace(raw, order);

        String sha1 = sha1Hex(raw);
        if (!STOCK_SHA1.equals(sha1)) {
            throw new RomValidationException(
                    "This is not the stock GoldenEye 007 (USA) ROM (SHA-1 mismatch). "
                            + "Modded ROMs, other regions and bad dumps are not supported.");
        }
        return new Result(order, raw, sha1);
    }

    /** Converts a cart image to big-endian .z64 order. No-op for .z64. */
    public static void normalizeInPlace(byte[] data, ByteOrder order) {
        if (data == null || order == null || order == ByteOrder.Z64 || order == ByteOrder.UNKNOWN) {
            return;
        }
        if (order == ByteOrder.V64) {
            for (int i = 0; i + 1 < data.length; i += 2) {
                byte t = data[i];
                data[i] = data[i + 1];
                data[i + 1] = t;
            }
        } else if (order == ByteOrder.N64) {
            for (int i = 0; i + 3 < data.length; i += 4) {
                byte t0 = data[i];
                byte t1 = data[i + 1];
                data[i] = data[i + 3];
                data[i + 1] = data[i + 2];
                data[i + 2] = t1;
                data[i + 3] = t0;
            }
        }
    }

    public static String sha1Hex(byte[] data) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-1");
            return toHex(digest.digest(data));
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-1 is required by the JVM", e);
        }
    }

    public static String sha1Hex(InputStream input) throws IOException {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-1");
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = input.read(buffer)) != -1) {
                digest.update(buffer, 0, read);
            }
            return toHex(digest.digest());
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-1 is required by the JVM", e);
        }
    }

    private static String toHex(byte[] bytes) {
        char[] out = new char[bytes.length * 2];
        final char[] digits = "0123456789abcdef".toCharArray();
        for (int i = 0; i < bytes.length; i++) {
            int b = bytes[i] & 0xFF;
            out[i * 2] = digits[b >>> 4];
            out[i * 2 + 1] = digits[b & 0x0F];
        }
        return new String(out);
    }
}
