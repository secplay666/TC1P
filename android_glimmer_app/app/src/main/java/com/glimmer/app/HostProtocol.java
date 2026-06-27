package com.glimmer.app;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

final class HostProtocol {
    static final int MAGIC = 0xA5;
    static final int VERSION = 0x01;
    static final int MAX_PACKET_LEN = 20;
    static final int HEADER_LEN = 9;
    static final int CRC_LEN = 2;
    static final int CHUNK_MAX_LEN = MAX_PACKET_LEN - HEADER_LEN - CRC_LEN;
    static final int MESSAGE_MAX_LEN = 192;
    static final int CHAT_TEXT_MAX_LEN = MESSAGE_MAX_LEN - 8;

    static final int TYPE_CMD = 1;
    static final int TYPE_RSP = 2;
    static final int TYPE_LOG = 3;
    static final int TYPE_EVENT = 4;

    static final int CMD_GET_DEVICE_INFO = 0x01;
    static final int CMD_GET_PEER_TABLE = 0x04;
    static final int CMD_LOG_ENABLE = 0x07;
    static final int CMD_SHELL_EXEC = 0x10;
    static final int CMD_P2P_CHAT_SEND = 0x11;
    static final int CMD_GET_PROFILE_SUMMARY = 0x12;
    static final int CMD_SET_PROFILE_SUMMARY = 0x13;
    static final int CMD_GET_PEER_PROFILES = 0x14;

    static final int PROFILE_FLAG_VISIBLE = 0x01;
    static final int PROFILE_TAG_MAX_COUNT = 6;
    static final int PROFILE_NICKNAME_MAX_BYTES = 18;
    static final int PROFILE_SIGNATURE_MAX_BYTES = 28;

    static final int EVENT_P2P_CHAT = 0x84;
    static final int EVENT_P2P_CHAT_TX_RESULT = 0x85;

    static final int P2P_CHAT_FLAG_TRUNCATED = 0x01;
    static final int P2P_CHAT_FLAG_DROPPED = 0x02;

    private HostProtocol() {
    }

    static List<byte[]> makeEmptyCommand(int seq, int cmd) {
        return encodeMessage(TYPE_CMD, seq, cmd, 0, new byte[0]);
    }

    static List<byte[]> makeLogEnable(int seq, boolean enabled) {
        return encodeMessage(TYPE_CMD, seq, CMD_LOG_ENABLE, 0, new byte[]{(byte) (enabled ? 1 : 0)});
    }

    static List<byte[]> makeShellExec(int seq, String line) {
        byte[] payload = line.getBytes(StandardCharsets.UTF_8);
        if (payload.length > MESSAGE_MAX_LEN) {
            payload = Arrays.copyOf(payload, MESSAGE_MAX_LEN);
        }
        return encodeMessage(TYPE_CMD, seq, CMD_SHELL_EXEC, 0, payload);
    }

    static List<byte[]> makeP2pChatSend(int seq, String text) {
        return makeP2pChatSend(seq, 0, text);
    }

    static List<byte[]> makeP2pChatSend(int seq, long targetShortId, String text) {
        byte[] payload = text.getBytes(StandardCharsets.UTF_8);
        if (payload.length == 0) {
            throw new IllegalArgumentException("chat text is empty");
        }
        if (payload.length > CHAT_TEXT_MAX_LEN) {
            throw new IllegalArgumentException("chat text too long: " + payload.length + " > " + CHAT_TEXT_MAX_LEN);
        }
        if (targetShortId == 0) {
            return encodeMessage(TYPE_CMD, seq, CMD_P2P_CHAT_SEND, 0, payload);
        }
        byte[] request = new byte[payload.length + 5];
        request[0] = 0x01;
        wr32le(request, 1, (int) targetShortId);
        System.arraycopy(payload, 0, request, 5, payload.length);
        return encodeMessage(TYPE_CMD, seq, CMD_P2P_CHAT_SEND, 0, request);
    }

    static List<byte[]> makePeerProfilesCommand(int seq, int startIndex) {
        return encodeMessage(TYPE_CMD, seq, CMD_GET_PEER_PROFILES, 0,
                new byte[]{(byte) Math.max(0, Math.min(255, startIndex))});
    }

    static List<byte[]> makeSetProfileSummary(int seq, String nickname, String signature, int avatarSeed, int[] tags) {
        byte[] nicknameBytes = utf8Limit(nickname == null ? "" : nickname.trim(), PROFILE_NICKNAME_MAX_BYTES);
        byte[] signatureBytes = utf8Limit(signature == null ? "" : signature.trim(), PROFILE_SIGNATURE_MAX_BYTES);
        byte[] payload = new byte[8 + PROFILE_TAG_MAX_COUNT + nicknameBytes.length + signatureBytes.length];
        int offset = 0;

        payload[offset++] = PROFILE_FLAG_VISIBLE;
        wr32le(payload, offset, avatarSeed);
        offset += 4;
        int tagCount = tags == null ? 0 : Math.min(tags.length, PROFILE_TAG_MAX_COUNT);
        payload[offset++] = (byte) tagCount;
        payload[offset++] = (byte) nicknameBytes.length;
        payload[offset++] = (byte) signatureBytes.length;
        for (int i = 0; i < PROFILE_TAG_MAX_COUNT; i++) {
            int value = i < tagCount ? tags[i] : 0;
            payload[offset++] = (byte) value;
        }
        System.arraycopy(nicknameBytes, 0, payload, offset, nicknameBytes.length);
        offset += nicknameBytes.length;
        System.arraycopy(signatureBytes, 0, payload, offset, signatureBytes.length);
        return encodeMessage(TYPE_CMD, seq, CMD_SET_PROFILE_SUMMARY, 0, payload);
    }

    static List<byte[]> encodeMessage(int frameType, int seq, int cmd, int status, byte[] payload) {
        if (payload.length > MESSAGE_MAX_LEN) {
            throw new IllegalArgumentException("payload too long: " + payload.length);
        }

        int fragCount = Math.max(1, (payload.length + CHUNK_MAX_LEN - 1) / CHUNK_MAX_LEN);
        List<byte[]> packets = new ArrayList<>();
        for (int fragIndex = 0; fragIndex < fragCount; fragIndex++) {
            int offset = fragIndex * CHUNK_MAX_LEN;
            int chunkLen = Math.min(CHUNK_MAX_LEN, payload.length - offset);
            if (chunkLen < 0) {
                chunkLen = 0;
            }

            byte[] body = new byte[HEADER_LEN + chunkLen];
            body[0] = (byte) MAGIC;
            body[1] = (byte) VERSION;
            body[2] = (byte) frameType;
            body[3] = (byte) seq;
            body[4] = (byte) cmd;
            body[5] = (byte) status;
            body[6] = (byte) fragIndex;
            body[7] = (byte) fragCount;
            body[8] = (byte) chunkLen;
            if (chunkLen > 0) {
                System.arraycopy(payload, offset, body, HEADER_LEN, chunkLen);
            }

            int crc = crc16(body);
            byte[] packet = Arrays.copyOf(body, body.length + CRC_LEN);
            packet[packet.length - 2] = (byte) (crc & 0xFF);
            packet[packet.length - 1] = (byte) ((crc >> 8) & 0xFF);
            packets.add(packet);
        }
        return packets;
    }

    static HostFrame decodePacket(byte[] data) throws ProtocolException {
        if (data.length < HEADER_LEN + CRC_LEN) {
            throw new ProtocolException("packet too short");
        }
        if (u8(data[0]) != MAGIC) {
            throw new ProtocolException(String.format(Locale.US, "bad magic: 0x%02X", u8(data[0])));
        }
        if (u8(data[1]) != VERSION) {
            throw new ProtocolException(String.format(Locale.US, "bad version: 0x%02X", u8(data[1])));
        }

        int payloadLen = u8(data[8]);
        int expectedLen = HEADER_LEN + payloadLen + CRC_LEN;
        if (payloadLen > CHUNK_MAX_LEN || data.length != expectedLen) {
            throw new ProtocolException("bad length: payload=" + payloadLen + ", packet=" + data.length);
        }

        int gotCrc = u16le(data, HEADER_LEN + payloadLen);
        int calcCrc = crc16(Arrays.copyOf(data, HEADER_LEN + payloadLen));
        if (gotCrc != calcCrc) {
            throw new ProtocolException(String.format(Locale.US, "crc mismatch: got=0x%04X calc=0x%04X", gotCrc, calcCrc));
        }

        int fragIndex = u8(data[6]);
        int fragCount = u8(data[7]);
        if (fragCount == 0 || fragIndex >= fragCount) {
            throw new ProtocolException("bad fragment: " + fragIndex + "/" + fragCount);
        }

        byte[] payload = Arrays.copyOfRange(data, HEADER_LEN, HEADER_LEN + payloadLen);
        return new HostFrame(u8(data[2]), u8(data[3]), u8(data[4]), u8(data[5]), fragIndex, fragCount, payload);
    }

    static String formatMessage(HostMessage message) {
        if (message.frameType == TYPE_LOG) {
            return "LOG " + parseLog(message.payload);
        }
        if (message.frameType == TYPE_EVENT) {
            return formatEvent(message.cmd, message.payload);
        }
        if (message.frameType == TYPE_RSP) {
            return formatResponse(message);
        }
        return "MSG type=" + message.frameType + " cmd=0x" + hex2(message.cmd) + " len=" + message.payload.length;
    }

    static DeviceInfo parseDeviceInfo(byte[] payload) {
        if (payload.length < 36) {
            throw new IllegalArgumentException("device info payload too short");
        }
        return new DeviceInfo(
                u8(payload[0]),
                u8(payload[1]),
                u8(payload[3]),
                u8(payload[2]),
                u8(payload[4]),
                u8(payload[5]),
                u8(payload[6]),
                u32le(payload, 8),
                Arrays.copyOfRange(payload, 12, 28),
                u16le(payload, 28),
                u16le(payload, 30),
                u16le(payload, 32),
                u8(payload[34]),
                u8(payload[35]));
    }

    static List<PeerInfo> parsePeerTable(byte[] payload) {
        List<PeerInfo> peers = new ArrayList<>();
        if (payload.length == 0) {
            return peers;
        }

        int count = u8(payload[0]);
        int offset = 1;
        for (int i = 0; i < count && offset + 8 <= payload.length; i++) {
            peers.add(new PeerInfo(
                    u32le(payload, offset),
                    u8(payload[offset + 4]),
                    i8(payload[offset + 5]),
                    i8(payload[offset + 6]),
                    u8(payload[offset + 7])));
            offset += 8;
        }
        return peers;
    }

    static ChatEvent parseChatEvent(byte[] payload) {
        if (payload.length < 8) {
            throw new IllegalArgumentException("p2p chat event too short");
        }

        long shortId = u32le(payload, 0);
        int rssi = i8(payload[4]);
        int flags = u8(payload[5]);
        int declaredTextLen = u16le(payload, 6);
        byte[] textBytes = Arrays.copyOfRange(payload, 8, payload.length);
        String text = new String(textBytes, StandardCharsets.UTF_8);
        return new ChatEvent(shortId, rssi, flags, declaredTextLen, text);
    }

    static ChatTxResult parseChatTxResult(byte[] payload) {
        if (payload.length < 14) {
            throw new IllegalArgumentException("p2p chat tx result too short");
        }
        return new ChatTxResult(
                u32le(payload, 0),
                u8(payload[4]),
                u8(payload[5]),
                u8(payload[6]),
                u16le(payload, 8),
                u32le(payload, 10));
    }

    static ProfileSummary parseProfileSummary(byte[] payload) {
        if (payload.length < 12 + PROFILE_TAG_MAX_COUNT) {
            throw new IllegalArgumentException("profile payload too short");
        }

        int offset = 0;
        int version = u8(payload[offset++]);
        int flags = u8(payload[offset++]);
        int seq = u16le(payload, offset);
        offset += 2;
        int keyId = u8(payload[offset++]);
        long avatarSeed = u32le(payload, offset);
        offset += 4;
        int tagCount = Math.min(u8(payload[offset++]), PROFILE_TAG_MAX_COUNT);
        int nicknameLen = u8(payload[offset++]);
        int signatureLen = u8(payload[offset++]);
        int[] tags = new int[PROFILE_TAG_MAX_COUNT];
        for (int i = 0; i < PROFILE_TAG_MAX_COUNT; i++) {
            tags[i] = u8(payload[offset++]);
        }
        if (payload.length < offset + nicknameLen + signatureLen) {
            throw new IllegalArgumentException("profile string length mismatch");
        }
        String nickname = new String(payload, offset, nicknameLen, StandardCharsets.UTF_8);
        offset += nicknameLen;
        String signature = new String(payload, offset, signatureLen, StandardCharsets.UTF_8);
        return new ProfileSummary(version, flags, seq, keyId, avatarSeed, tagCount, tags, nickname, signature);
    }

    static List<PeerProfileInfo> parsePeerProfiles(byte[] payload) {
        return parsePeerProfilePage(payload).profiles;
    }

    static PeerProfilePage parsePeerProfilePage(byte[] payload) {
        List<PeerProfileInfo> profiles = new ArrayList<>();
        if (payload.length == 0) {
            return new PeerProfilePage(profiles, -1);
        }

        int count = u8(payload[0]);
        int offset = 1;
        for (int i = 0; i < count && offset + 18 + PROFILE_TAG_MAX_COUNT <= payload.length; i++) {
            long shortId = u32le(payload, offset);
            offset += 4;
            int level = u8(payload[offset++]);
            int rssi = i8(payload[offset++]);
            int rssiAvg = i8(payload[offset++]);
            int peerFlags = u8(payload[offset++]);
            int profileFlags = u8(payload[offset++]);
            int seq = u16le(payload, offset);
            offset += 2;
            long avatarSeed = u32le(payload, offset);
            offset += 4;
            int tagCount = Math.min(u8(payload[offset++]), PROFILE_TAG_MAX_COUNT);
            int nicknameLen = u8(payload[offset++]);
            int signatureLen = u8(payload[offset++]);
            int[] tags = new int[PROFILE_TAG_MAX_COUNT];
            for (int j = 0; j < PROFILE_TAG_MAX_COUNT; j++) {
                tags[j] = u8(payload[offset++]);
            }
            if (payload.length < offset + nicknameLen + signatureLen) {
                break;
            }
            String nickname = new String(payload, offset, nicknameLen, StandardCharsets.UTF_8);
            offset += nicknameLen;
            String signature = new String(payload, offset, signatureLen, StandardCharsets.UTF_8);
            offset += signatureLen;
            profiles.add(new PeerProfileInfo(shortId, level, rssi, rssiAvg, peerFlags,
                    profileFlags, seq, avatarSeed, tagCount, tags, nickname, signature));
        }
        int nextIndex = -1;
        if (offset < payload.length) {
            int rawNext = u8(payload[offset]);
            nextIndex = rawNext == 0xFF ? -1 : rawNext;
        }
        return new PeerProfilePage(profiles, nextIndex);
    }

    static String formatResponse(HostMessage message) {
        String name = cmdName(message.cmd);
        String status = statusName(message.status);
        if (message.status != 0) {
            if (message.cmd == CMD_P2P_CHAT_SEND && message.status == 0x02 && message.payload.length > 0) {
                return name + ": " + status + " peer_count=" + u8(message.payload[0]) + ", target selection not implemented";
            }
            return name + ": " + status;
        }

        try {
            switch (message.cmd) {
                case CMD_GET_DEVICE_INFO:
                    return formatDeviceInfo(name, message.payload);
                case CMD_GET_PEER_TABLE:
                    return formatPeerTable(name, message.payload);
                case CMD_SHELL_EXEC:
                    return new String(message.payload, StandardCharsets.UTF_8).trim();
                case CMD_P2P_CHAT_SEND:
                    if (message.payload.length >= 8) {
                        String target = message.payload.length >= 12
                                ? String.format(Locale.US, ", target=0x%08X", u32le(message.payload, 8))
                                : "";
                        return String.format(Locale.US,
                                "%s: queued %d bytes, peers=%d, p2p_max=%d, frag_payload=%d, max_frag=%d%s",
                                name,
                                u16le(message.payload, 2),
                                u8(message.payload[1]),
                                u16le(message.payload, 4),
                                u8(message.payload[6]),
                                u8(message.payload[7]),
                                target);
                    }
                    return name + ": " + status;
                case CMD_GET_PROFILE_SUMMARY:
                    ProfileSummary profile = parseProfileSummary(message.payload);
                    return String.format(Locale.US, "%s: %s / %s", name, profile.displayName(), profile.signature);
                case CMD_GET_PEER_PROFILES:
                    return name + ": " + parsePeerProfiles(message.payload).size() + " profile(s)";
                default:
                    return name + ": " + status + ", payload=" + hex(message.payload);
            }
        } catch (Exception e) {
            return name + ": parse error: " + e.getMessage() + ", raw=" + hex(message.payload);
        }
    }

    static String formatEvent(int cmd, byte[] payload) {
        try {
            if (cmd == EVENT_P2P_CHAT) {
                if (payload.length < 8) {
                    throw new IllegalArgumentException("p2p chat event too short");
                }
                long shortId = u32le(payload, 0);
                int rssi = i8(payload[4]);
                int flags = u8(payload[5]);
                int textLen = u16le(payload, 6);
                byte[] textBytes = Arrays.copyOfRange(payload, 8, payload.length);
                String text = new String(textBytes, StandardCharsets.UTF_8);
                if ((flags & P2P_CHAT_FLAG_DROPPED) != 0) {
                    return String.format(Locale.US, "CHAT_DROPPED from=0x%08X len=%d", shortId, textLen);
                }
                String suffix = "";
                if ((flags & P2P_CHAT_FLAG_TRUNCATED) != 0 || textBytes.length < textLen) {
                    suffix = " [truncated]";
                }
                return String.format(Locale.US, "CHAT from=0x%08X rssi=%d len=%d %s%s", shortId, rssi, textLen, text, suffix);
            }
            if (cmd == EVENT_P2P_CHAT_TX_RESULT) {
                if (payload.length < 14) {
                    throw new IllegalArgumentException("p2p chat tx result too short");
                }
                long shortId = u32le(payload, 0);
                int hostStatus = u8(payload[4]);
                int appStatus = u8(payload[5]);
                int flags = u8(payload[6]);
                int textLen = u16le(payload, 8);
                long peerMessageId = u32le(payload, 10);
                String result = hostStatus == 0 ? "CHAT_TX_OK" : "CHAT_TX_FAIL";
                return String.format(Locale.US,
                        "%s peer=0x%08X host=%s app=%d flags=0x%02X len=%d id=0x%08X",
                        result, shortId, statusName(hostStatus), appStatus, flags, textLen, peerMessageId);
            }
        } catch (Exception e) {
            return String.format(Locale.US, "EVT 0x%02X parse error: %s raw=%s", cmd, e.getMessage(), hex(payload));
        }
        return String.format(Locale.US, "EVT 0x%02X payload=%s", cmd, hex(payload));
    }

    private static String formatDeviceInfo(String name, byte[] payload) {
        if (payload.length < 36) {
            throw new IllegalArgumentException("device info payload too short");
        }
        int fwMajor = u8(payload[3]);
        int fwMinor = u8(payload[2]);
        int fwPatch = u8(payload[4]);
        return String.format(Locale.US,
                "%s: short_id=0x%08X eid=%s fw=%d.%d.%d hw=%d key=%d cmds=%d logs=%d host_frame=%d adv=%d",
                name,
                u32le(payload, 8),
                hex(Arrays.copyOfRange(payload, 12, 28)),
                fwMajor,
                fwMinor,
                fwPatch,
                u8(payload[1]),
                u8(payload[5]),
                u16le(payload, 28),
                u16le(payload, 30),
                u8(payload[34]),
                u8(payload[35]));
    }

    private static String formatPeerTable(String name, byte[] payload) {
        if (payload.length == 0) {
            return name + ": 0 peer(s)";
        }
        int count = u8(payload[0]);
        StringBuilder builder = new StringBuilder();
        builder.append(name).append(": ").append(count).append(" peer(s)");
        int offset = 1;
        for (int i = 0; i < count && offset + 8 <= payload.length; i++) {
            long shortId = u32le(payload, offset);
            int level = u8(payload[offset + 4]);
            int rssi = i8(payload[offset + 5]);
            int rssiAvg = i8(payload[offset + 6]);
            int flags = u8(payload[offset + 7]);
            builder.append('\n').append(String.format(Locale.US,
                    "  #%d short_id=0x%08X level=%s rssi=%d avg=%d flags=0x%02X",
                    i + 1, shortId, peerLevelName(level), rssi, rssiAvg, flags));
            offset += 8;
        }
        return builder.toString();
    }

    private static String parseLog(byte[] payload) {
        if (payload.length < 4) {
            return hex(payload);
        }
        return new String(Arrays.copyOfRange(payload, 4, payload.length), StandardCharsets.UTF_8);
    }

    private static int crc16(byte[] data) {
        int crc = 0xFFFF;
        for (byte value : data) {
            crc ^= u8(value);
            for (int i = 0; i < 8; i++) {
                if ((crc & 1) != 0) {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
                crc &= 0xFFFF;
            }
        }
        return crc;
    }

    private static int u8(byte value) {
        return value & 0xFF;
    }

    private static int i8(byte value) {
        return (int) value;
    }

    private static int u16le(byte[] data, int offset) {
        return u8(data[offset]) | (u8(data[offset + 1]) << 8);
    }

    private static long u32le(byte[] data, int offset) {
        return ((long) u8(data[offset]))
                | ((long) u8(data[offset + 1]) << 8)
                | ((long) u8(data[offset + 2]) << 16)
                | ((long) u8(data[offset + 3]) << 24);
    }

    private static String cmdName(int cmd) {
        switch (cmd) {
            case CMD_GET_DEVICE_INFO:
                return "GET_DEVICE_INFO";
            case CMD_GET_PEER_TABLE:
                return "GET_PEER_TABLE";
            case CMD_LOG_ENABLE:
                return "LOG_ENABLE";
            case CMD_SHELL_EXEC:
                return "SHELL_EXEC";
            case CMD_P2P_CHAT_SEND:
                return "P2P_CHAT_SEND";
            case CMD_GET_PROFILE_SUMMARY:
                return "GET_PROFILE_SUMMARY";
            case CMD_SET_PROFILE_SUMMARY:
                return "SET_PROFILE_SUMMARY";
            case CMD_GET_PEER_PROFILES:
                return "GET_PEER_PROFILES";
            default:
                return "0x" + hex2(cmd);
        }
    }

    private static String statusName(int status) {
        switch (status) {
            case 0x00:
                return "OK";
            case 0x01:
                return "ERR_PARAM";
            case 0x02:
                return "ERR_STATE";
            case 0x03:
                return "ERR_BUSY";
            case 0x04:
                return "ERR_UNSUPPORTED";
            case 0x05:
                return "ERR_CRC";
            case 0x06:
                return "ERR_NO_MEM";
            case 0x07:
                return "ERR_PERMISSION";
            case 0x08:
                return "ERR_NOT_FOUND";
            case 0x09:
                return "ERR_FLASH";
            default:
                return "0x" + hex2(status);
        }
    }

    private static String peerLevelName(int level) {
        switch (level) {
            case 0:
                return "NONE";
            case 1:
                return "S1";
            case 2:
                return "S2";
            case 3:
                return "S3";
            case 4:
                return "LOST";
            default:
                return Integer.toString(level);
        }
    }

    static String hex(byte[] data) {
        StringBuilder builder = new StringBuilder(data.length * 2);
        for (byte value : data) {
            builder.append(hex2(u8(value)));
        }
        return builder.toString();
    }

    private static String hex2(int value) {
        return String.format(Locale.US, "%02X", value & 0xFF);
    }

    private static void wr32le(byte[] data, int offset, int value) {
        data[offset] = (byte) value;
        data[offset + 1] = (byte) (value >> 8);
        data[offset + 2] = (byte) (value >> 16);
        data[offset + 3] = (byte) (value >> 24);
    }

    private static byte[] utf8Limit(String value, int maxBytes) {
        byte[] raw = value.getBytes(StandardCharsets.UTF_8);
        if (raw.length <= maxBytes) {
            return raw;
        }
        int end = value.length();
        while (end > 0) {
            byte[] candidate = value.substring(0, end).getBytes(StandardCharsets.UTF_8);
            if (candidate.length <= maxBytes) {
                return candidate;
            }
            end--;
        }
        return new byte[0];
    }

    static String shortIdText(long shortId) {
        return String.format(Locale.US, "%08X", shortId);
    }

    static final class DeviceInfo {
        final int protocolVersion;
        final int hardwareRevision;
        final int firmwareMajor;
        final int firmwareMinor;
        final int firmwarePatch;
        final int keyId;
        final int privacyMode;
        final long shortId;
        final byte[] eid;
        final int commandCount;
        final int logCount;
        final int crcErrorCount;
        final int hostFrameVersion;
        final int advProtocolVersion;

        DeviceInfo(int protocolVersion, int hardwareRevision, int firmwareMajor, int firmwareMinor,
                   int firmwarePatch, int keyId, int privacyMode, long shortId, byte[] eid,
                   int commandCount, int logCount, int crcErrorCount, int hostFrameVersion,
                   int advProtocolVersion) {
            this.protocolVersion = protocolVersion;
            this.hardwareRevision = hardwareRevision;
            this.firmwareMajor = firmwareMajor;
            this.firmwareMinor = firmwareMinor;
            this.firmwarePatch = firmwarePatch;
            this.keyId = keyId;
            this.privacyMode = privacyMode;
            this.shortId = shortId;
            this.eid = eid;
            this.commandCount = commandCount;
            this.logCount = logCount;
            this.crcErrorCount = crcErrorCount;
            this.hostFrameVersion = hostFrameVersion;
            this.advProtocolVersion = advProtocolVersion;
        }

        String firmwareVersion() {
            return firmwareMajor + "." + firmwareMinor + "." + firmwarePatch;
        }

        String shortCode() {
            return shortIdText(shortId);
        }

        boolean hasPrivacyKey() {
            return keyId != 0;
        }
    }

    static final class PeerInfo {
        final long shortId;
        final int level;
        final int rssi;
        final int rssiAvg;
        final int flags;

        PeerInfo(long shortId, int level, int rssi, int rssiAvg, int flags) {
            this.shortId = shortId;
            this.level = level;
            this.rssi = rssi;
            this.rssiAvg = rssiAvg;
            this.flags = flags;
        }

        String shortCode() {
            return shortIdText(shortId);
        }

        String proximityText() {
            switch (level) {
                case 3:
                    return "很近";
                case 2:
                    return "附近";
                case 1:
                    return "稍远";
                case 4:
                    return "刚离开";
                default:
                    return "可见";
            }
        }

        String signalText() {
            int value = rssiAvg != 0 ? rssiAvg : rssi;
            if (value >= -55) {
                return "信号稳定";
            }
            if (value >= -72) {
                return "信号良好";
            }
            return "信号较弱";
        }
    }

    static final class ChatEvent {
        final long shortId;
        final int rssi;
        final int flags;
        final int declaredTextLen;
        final String text;

        ChatEvent(long shortId, int rssi, int flags, int declaredTextLen, String text) {
            this.shortId = shortId;
            this.rssi = rssi;
            this.flags = flags;
            this.declaredTextLen = declaredTextLen;
            this.text = text;
        }

        boolean isDropped() {
            return (flags & P2P_CHAT_FLAG_DROPPED) != 0;
        }

        boolean isTruncated() {
            return (flags & P2P_CHAT_FLAG_TRUNCATED) != 0 || text.getBytes(StandardCharsets.UTF_8).length < declaredTextLen;
        }
    }

    static final class ChatTxResult {
        final long shortId;
        final int hostStatus;
        final int appStatus;
        final int flags;
        final int textLen;
        final long peerMessageId;

        ChatTxResult(long shortId, int hostStatus, int appStatus, int flags, int textLen, long peerMessageId) {
            this.shortId = shortId;
            this.hostStatus = hostStatus;
            this.appStatus = appStatus;
            this.flags = flags;
            this.textLen = textLen;
            this.peerMessageId = peerMessageId;
        }

        boolean isSuccess() {
            return hostStatus == 0;
        }
    }

    static final class ProfileSummary {
        final int version;
        final int flags;
        final int seq;
        final int keyId;
        final long avatarSeed;
        final int tagCount;
        final int[] tags;
        final String nickname;
        final String signature;

        ProfileSummary(int version, int flags, int seq, int keyId, long avatarSeed,
                       int tagCount, int[] tags, String nickname, String signature) {
            this.version = version;
            this.flags = flags;
            this.seq = seq;
            this.keyId = keyId;
            this.avatarSeed = avatarSeed;
            this.tagCount = tagCount;
            this.tags = tags;
            this.nickname = nickname;
            this.signature = signature;
        }

        String displayName() {
            return nickname == null || nickname.isEmpty() ? "未命名的微光" : nickname;
        }

        String signatureText() {
            return signature == null || signature.isEmpty() ? "一束安静的微光。" : signature;
        }
    }

    static final class PeerProfileInfo {
        final long shortId;
        final int level;
        final int rssi;
        final int rssiAvg;
        final int peerFlags;
        final int profileFlags;
        final int seq;
        final long avatarSeed;
        final int tagCount;
        final int[] tags;
        final String nickname;
        final String signature;

        PeerProfileInfo(long shortId, int level, int rssi, int rssiAvg, int peerFlags,
                        int profileFlags, int seq, long avatarSeed, int tagCount,
                        int[] tags, String nickname, String signature) {
            this.shortId = shortId;
            this.level = level;
            this.rssi = rssi;
            this.rssiAvg = rssiAvg;
            this.peerFlags = peerFlags;
            this.profileFlags = profileFlags;
            this.seq = seq;
            this.avatarSeed = avatarSeed;
            this.tagCount = tagCount;
            this.tags = tags;
            this.nickname = nickname;
            this.signature = signature;
        }

        String shortCode() {
            return shortIdText(shortId);
        }

        String displayName() {
            return nickname == null || nickname.isEmpty() ? "一束微光" : nickname;
        }

        String signatureText() {
            return signature == null || signature.isEmpty() ? "对方还没有留下一句话。" : signature;
        }

        String proximityText() {
            switch (level) {
                case 3:
                    return "很近";
                case 2:
                    return "附近";
                case 1:
                    return "稍远";
                case 4:
                    return "刚离开";
                default:
                    return "可见";
            }
        }

        String signalText() {
            int value = rssiAvg != 0 ? rssiAvg : rssi;
            if (value >= -55) {
                return "信号稳定";
            }
            if (value >= -72) {
                return "信号良好";
            }
            return "信号较弱";
        }
    }

    static final class PeerProfilePage {
        final List<PeerProfileInfo> profiles;
        final int nextIndex;

        PeerProfilePage(List<PeerProfileInfo> profiles, int nextIndex) {
            this.profiles = profiles;
            this.nextIndex = nextIndex;
        }

        boolean hasMore() {
            return nextIndex >= 0;
        }
    }

    static final class HostFrame {
        final int frameType;
        final int seq;
        final int cmd;
        final int status;
        final int fragIndex;
        final int fragCount;
        final byte[] payload;

        HostFrame(int frameType, int seq, int cmd, int status, int fragIndex, int fragCount, byte[] payload) {
            this.frameType = frameType;
            this.seq = seq;
            this.cmd = cmd;
            this.status = status;
            this.fragIndex = fragIndex;
            this.fragCount = fragCount;
            this.payload = payload;
        }
    }

    static final class HostMessage {
        final int frameType;
        final int seq;
        final int cmd;
        final int status;
        final byte[] payload;

        HostMessage(int frameType, int seq, int cmd, int status, byte[] payload) {
            this.frameType = frameType;
            this.seq = seq;
            this.cmd = cmd;
            this.status = status;
            this.payload = payload;
        }
    }

    static final class MessageAssembler {
        private final Map<String, Assembly> rx = new HashMap<>();

        void reset() {
            rx.clear();
        }

        HostMessage push(HostFrame frame) throws ProtocolException {
            String key = frame.frameType + ":" + frame.seq + ":" + frame.cmd;
            Assembly assembly = rx.get(key);
            if (assembly == null) {
                assembly = new Assembly(frame.fragCount);
                rx.put(key, assembly);
            } else if (assembly.fragCount != frame.fragCount) {
                rx.remove(key);
                throw new ProtocolException("fragment count changed");
            }

            assembly.chunks[frame.fragIndex] = frame.payload;
            for (byte[] chunk : assembly.chunks) {
                if (chunk == null) {
                    return null;
                }
            }

            ByteArrayOutputStream out = new ByteArrayOutputStream();
            for (byte[] chunk : assembly.chunks) {
                out.write(chunk, 0, chunk.length);
            }
            rx.remove(key);
            return new HostMessage(frame.frameType, frame.seq, frame.cmd, frame.status, out.toByteArray());
        }
    }

    private static final class Assembly {
        final int fragCount;
        final byte[][] chunks;

        Assembly(int fragCount) {
            this.fragCount = fragCount;
            this.chunks = new byte[fragCount][];
        }
    }

    static final class ProtocolException extends Exception {
        ProtocolException(String message) {
            super(message);
        }
    }
}
