package com.glimmer.app;

import org.json.JSONException;
import org.json.JSONObject;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;
import java.util.Locale;

final class OtaPackage {
    private static final byte[] MAGIC = "GLIMMER-OTA-PKG1\n".getBytes(StandardCharsets.US_ASCII);
    private static final String PRODUCT_ID = "glimmer-b85";
    private static final String SIGNATURE_ALGORITHM = "RSA-2048-SHA256";
    private static final byte[] SHA256_DIGESTINFO_PREFIX = hex(
            "3031300d060960864801650304020105000420");
    private static final String PUBLIC_KEY_N_HEX =
            "c0bf0198efb38b1ddea60ce39785674343fd3e58e58ed1b121c7ba899f3b70b770f53af0da6cb8b5131a68196a0aaa08"
                    + "6b2d13ce2646824cc144c3dc90decdd55864cfb1696619fa39eb5a917b3aa63360991443d8c41a2d66c8253448c154edb"
                    + "4c645e74b7618a2960b8d8754499159bf09e2cdc108b9bdb86c8f9e904585720d789f0d4d64d837595c43092260baec2c"
                    + "6551a461fd76fac7d6bc3a1f4bbb176cbcd33ef7680aa7b61b559f9bbc5fc0d4113251321c408f5bfab9421ed5d193e4"
                    + "34e73efaa9c07824d63246cb96f3b120586e18922cd5ca4987ef55416b053a56673fe1d23c8db80fc4e62183fa7239118"
                    + "3a572161fe3e21c1f8de3694b2295";
    private static final BigInteger PUBLIC_KEY_N = new BigInteger(PUBLIC_KEY_N_HEX, 16);
    private static final BigInteger PUBLIC_KEY_E = BigInteger.valueOf(65537);

    private OtaPackage() {
    }

    static Image parse(byte[] data, int maxSize, long currentVersionCode) {
        if (data == null || data.length == 0) {
            throw new IllegalArgumentException("OTA file is empty");
        }
        if (!startsWith(data, MAGIC)) {
            throw new IllegalArgumentException("Raw .bin OTA is disabled; please select a signed .glimmerota package");
        }

        int offset = MAGIC.length;
        if (data.length < offset + 4) {
            throw new IllegalArgumentException("OTA package truncated before manifest");
        }
        int manifestLen = rd32(data, offset);
        offset += 4;
        if (manifestLen <= 0 || data.length < offset + manifestLen + 2) {
            throw new IllegalArgumentException("OTA package manifest truncated");
        }
        byte[] manifestBytes = Arrays.copyOfRange(data, offset, offset + manifestLen);
        offset += manifestLen;
        int signatureLen = rd16(data, offset);
        offset += 2;
        if (signatureLen <= 0 || data.length < offset + signatureLen) {
            throw new IllegalArgumentException("OTA package signature truncated");
        }
        byte[] signature = Arrays.copyOfRange(data, offset, offset + signatureLen);
        offset += signatureLen;
        byte[] firmware = Arrays.copyOfRange(data, offset, data.length);

        JSONObject manifest;
        try {
            manifest = new JSONObject(new String(manifestBytes, StandardCharsets.UTF_8));
        } catch (JSONException e) {
            throw new IllegalArgumentException("Invalid OTA manifest", e);
        }
        try {
            if (manifest.optInt("format", 0) != 1) {
                throw new IllegalArgumentException("Unsupported OTA package format");
            }
            if (!PRODUCT_ID.equals(manifest.optString("product", ""))) {
                throw new IllegalArgumentException("Unsupported OTA product: " + manifest.optString("product", ""));
            }
            if (!SIGNATURE_ALGORITHM.equals(manifest.optString("signature", ""))) {
                throw new IllegalArgumentException("Unsupported OTA signature algorithm");
            }
            if (!verifySignature(manifestBytes, signature)) {
                throw new IllegalArgumentException("OTA package signature verification failed");
            }
            int firmwareSize = manifest.optInt("firmware_size", -1);
            if (firmwareSize != firmware.length) {
                throw new IllegalArgumentException("OTA firmware size mismatch");
            }
            if (firmware.length > maxSize) {
                throw new IllegalArgumentException("OTA firmware exceeds " + maxSize + " bytes");
            }
            String expectedHash = manifest.optString("firmware_sha256", "");
            if (!sha256Hex(firmware).equalsIgnoreCase(expectedHash)) {
                throw new IllegalArgumentException("OTA firmware sha256 mismatch");
            }
            long versionCode = manifest.optLong("version_code", 0);
            if (versionCode <= 0) {
                throw new IllegalArgumentException("OTA version_code must be positive");
            }
            if (currentVersionCode > 0 && versionCode <= currentVersionCode) {
                throw new IllegalArgumentException("OTA downgrade blocked: package="
                        + versionCode + ", device=" + currentVersionCode);
            }
            return new Image(firmware, true, versionCode);
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalArgumentException("SHA-256 unavailable", e);
        }
    }

    private static boolean verifySignature(byte[] message, byte[] signature)
            throws NoSuchAlgorithmException {
        int keyLen = (PUBLIC_KEY_N.bitLength() + 7) / 8;
        if (signature.length != keyLen) {
            return false;
        }
        BigInteger sig = new BigInteger(1, signature);
        byte[] encoded = fixedLength(sig.modPow(PUBLIC_KEY_E, PUBLIC_KEY_N).toByteArray(), keyLen);
        byte[] digest = MessageDigest.getInstance("SHA-256").digest(message);
        byte[] expectedTail = concat(SHA256_DIGESTINFO_PREFIX, digest);
        int paddingLen = keyLen - expectedTail.length - 3;
        if (paddingLen < 8) {
            return false;
        }
        byte[] expected = new byte[keyLen];
        expected[0] = 0x00;
        expected[1] = 0x01;
        Arrays.fill(expected, 2, 2 + paddingLen, (byte) 0xFF);
        expected[2 + paddingLen] = 0x00;
        System.arraycopy(expectedTail, 0, expected, 3 + paddingLen, expectedTail.length);
        return Arrays.equals(encoded, expected);
    }

    private static byte[] fixedLength(byte[] value, int len) {
        if (value.length == len) {
            return value;
        }
        byte[] out = new byte[len];
        if (value.length > len) {
            System.arraycopy(value, value.length - len, out, 0, len);
        } else {
            System.arraycopy(value, 0, out, len - value.length, value.length);
        }
        return out;
    }

    private static byte[] concat(byte[] a, byte[] b) {
        byte[] out = new byte[a.length + b.length];
        System.arraycopy(a, 0, out, 0, a.length);
        System.arraycopy(b, 0, out, a.length, b.length);
        return out;
    }

    private static String sha256Hex(byte[] data) throws NoSuchAlgorithmException {
        byte[] digest = MessageDigest.getInstance("SHA-256").digest(data);
        StringBuilder builder = new StringBuilder(digest.length * 2);
        for (byte value : digest) {
            builder.append(String.format(Locale.US, "%02x", value & 0xff));
        }
        return builder.toString();
    }

    private static boolean startsWith(byte[] data, byte[] prefix) {
        if (data.length < prefix.length) {
            return false;
        }
        for (int i = 0; i < prefix.length; i++) {
            if (data[i] != prefix[i]) {
                return false;
            }
        }
        return true;
    }

    private static int rd16(byte[] data, int offset) {
        return (data[offset] & 0xff) | ((data[offset + 1] & 0xff) << 8);
    }

    private static int rd32(byte[] data, int offset) {
        return (data[offset] & 0xff)
                | ((data[offset + 1] & 0xff) << 8)
                | ((data[offset + 2] & 0xff) << 16)
                | ((data[offset + 3] & 0xff) << 24);
    }

    private static byte[] hex(String value) {
        int len = value.length() / 2;
        byte[] out = new byte[len];
        for (int i = 0; i < len; i++) {
            out[i] = (byte) Integer.parseInt(value.substring(i * 2, i * 2 + 2), 16);
        }
        return out;
    }

    static final class Image {
        final byte[] firmware;
        final boolean signed;
        final long versionCode;

        Image(byte[] firmware, boolean signed, long versionCode) {
            this.firmware = firmware;
            this.signed = signed;
            this.versionCode = versionCode;
        }
    }
}
