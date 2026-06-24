package com.glimmer.probe;

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

    static final int TYPE_CMD = 1;
    static final int TYPE_RSP = 2;
    static final int TYPE_LOG = 3;
    static final int TYPE_EVENT = 4;

    static final int CMD_GET_DEVICE_INFO = 0x01;
    static final int CMD_GET_PEER_TABLE = 0x04;
    static final int CMD_LOG_ENABLE = 0x07;
    static final int CMD_SHELL_EXEC = 0x10;
    static final int CMD_P2P_CHAT_SEND = 0x11;

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
        byte[] payload = text.getBytes(StandardCharsets.UTF_8);
        if (payload.length == 0) {
            throw new IllegalArgumentException("chat text is empty");
        }
        if (payload.length > MESSAGE_MAX_LEN) {
            throw new IllegalArgumentException("chat text too long: " + payload.length + " > " + MESSAGE_MAX_LEN);
        }
        return encodeMessage(TYPE_CMD, seq, CMD_P2P_CHAT_SEND, 0, payload);
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
                        return String.format(Locale.US,
                                "%s: queued %d bytes, peers=%d, p2p_max=%d, frag_payload=%d, max_frag=%d",
                                name,
                                u16le(message.payload, 2),
                                u8(message.payload[1]),
                                u16le(message.payload, 4),
                                u8(message.payload[6]),
                                u8(message.payload[7]));
                    }
                    return name + ": " + status;
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
