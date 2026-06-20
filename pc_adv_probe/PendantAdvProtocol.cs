using System.Buffers.Binary;

namespace PendantAdvProbe;

internal static class PendantAdvProtocol
{
    public const ushort CompanyId = 0xFFFF;
    public const byte AdvMagicLo = 0x44;
    public const byte AdvMagicHi = 0x50;
    public const byte AdvProtocolVersion = 0x01;
    public const byte AdvHeaderLen = 50;
    public const int AdvPayloadMaxLen = 193;

    public const byte FrameBeacon = 0x01;
    public const byte FrameData = 0x10;

    public const byte HostMagicLo = 0x48;
    public const byte HostMagicHi = 0x41;
    public const byte HostVersion = 0x01;
    public const int HostHeaderLen = 14;
    public const int HostChunkMaxLen = AdvPayloadMaxLen - HostHeaderLen;
    public const int HostMessageMaxLen = 192;

    public const byte HostTypeCmd = 1;
    public const byte HostTypeRsp = 2;
    public const byte HostTypeLog = 3;
    public const byte HostTypeEvent = 4;

    public const byte CmdGetDeviceInfo = 0x01;
    public const byte CmdGetSystemState = 0x02;
    public const byte CmdGetPeerTable = 0x04;
    public const byte CmdGetIdentity = 0x0B;

    public static readonly byte[] ZeroEid = new byte[16];
    public static readonly byte[] DefaultHostEid = "PENDANT-HOST-001"u8.ToArray();

    public sealed record AdvFrame(
        byte FrameType,
        byte Flags,
        byte KeyId,
        byte DeviceState,
        ushort FrameSeq,
        byte[] SourceEid,
        byte[] DestinationEid,
        uint MessageId,
        byte FragmentIndex,
        byte FragmentCount,
        byte[] Payload);

    public sealed record HostPacket(
        byte Type,
        byte Seq,
        byte Cmd,
        byte Status,
        byte FragmentIndex,
        byte FragmentCount,
        ushort TotalLen,
        ushort MessageCrc,
        byte[] Chunk);

    public static byte Crc8(ReadOnlySpan<byte> data)
    {
        byte crc = 0xFF;
        foreach (byte value in data)
        {
            crc ^= value;
            for (int i = 0; i < 8; i++)
            {
                crc = (crc & 0x80) != 0 ? (byte)((crc << 1) ^ 0x31) : (byte)(crc << 1);
            }
        }
        return crc;
    }

    public static ushort Crc16(ReadOnlySpan<byte> data)
    {
        ushort crc = 0xFFFF;
        foreach (byte value in data)
        {
            crc ^= value;
            for (int i = 0; i < 8; i++)
            {
                crc = (crc & 1) != 0 ? (ushort)((crc >> 1) ^ 0xA001) : (ushort)(crc >> 1);
            }
        }
        return crc;
    }

    public static uint Crc32(ReadOnlySpan<byte> data, uint init = 0)
    {
        uint crc = ~init;
        foreach (byte value in data)
        {
            crc ^= value;
            for (int i = 0; i < 8; i++)
            {
                crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
            }
        }
        return ~crc;
    }

    public static byte[] EncodeVendorPayload(AdvFrame frame)
    {
        if (frame.SourceEid.Length != 16 || frame.DestinationEid.Length != 16)
        {
            throw new ArgumentException("EID must be 16 bytes.");
        }
        if (frame.Payload.Length > AdvPayloadMaxLen)
        {
            throw new ArgumentException($"PENDANT-ADV payload too long: {frame.Payload.Length}");
        }

        byte[] vendor = new byte[AdvHeaderLen + frame.Payload.Length + 4];
        vendor[0] = AdvMagicLo;
        vendor[1] = AdvMagicHi;
        vendor[2] = AdvProtocolVersion;
        vendor[3] = AdvHeaderLen;
        vendor[4] = frame.FrameType;
        vendor[5] = frame.Flags;
        vendor[6] = frame.KeyId;
        vendor[7] = frame.DeviceState;
        BinaryPrimitives.WriteUInt16LittleEndian(vendor.AsSpan(8), frame.FrameSeq);
        frame.SourceEid.CopyTo(vendor.AsSpan(10));
        frame.DestinationEid.CopyTo(vendor.AsSpan(26));
        BinaryPrimitives.WriteUInt32LittleEndian(vendor.AsSpan(42), frame.MessageId);
        vendor[46] = frame.FragmentIndex;
        vendor[47] = frame.FragmentCount;
        vendor[48] = (byte)frame.Payload.Length;
        vendor[49] = Crc8(vendor.AsSpan(0, 49));
        frame.Payload.CopyTo(vendor.AsSpan(AdvHeaderLen));

        uint crc32 = Crc32(vendor.AsSpan(0, AdvHeaderLen + frame.Payload.Length));
        BinaryPrimitives.WriteUInt32LittleEndian(vendor.AsSpan(AdvHeaderLen + frame.Payload.Length), crc32);
        return vendor;
    }

    public static bool TryDecodeVendorPayload(ReadOnlySpan<byte> vendor, out AdvFrame? frame, out string error)
    {
        frame = null;
        error = string.Empty;

        if (vendor.Length < AdvHeaderLen + 4)
        {
            error = "vendor payload too short";
            return false;
        }
        if (vendor[0] != AdvMagicLo || vendor[1] != AdvMagicHi)
        {
            error = "bad PENDANT-ADV magic";
            return false;
        }
        if (vendor[2] != AdvProtocolVersion || vendor[3] != AdvHeaderLen)
        {
            error = "unsupported PENDANT-ADV version/header";
            return false;
        }
        if (Crc8(vendor[..49]) != vendor[49])
        {
            error = "header crc8 mismatch";
            return false;
        }

        int payloadLen = vendor[48];
        int totalLen = AdvHeaderLen + payloadLen + 4;
        if (vendor.Length < totalLen)
        {
            error = "vendor payload truncated";
            return false;
        }

        uint expectedCrc = BinaryPrimitives.ReadUInt32LittleEndian(vendor.Slice(AdvHeaderLen + payloadLen, 4));
        uint actualCrc = Crc32(vendor[..(AdvHeaderLen + payloadLen)]);
        if (expectedCrc != actualCrc)
        {
            error = $"frame crc32 mismatch: got 0x{expectedCrc:X8}, calc 0x{actualCrc:X8}";
            return false;
        }

        frame = new AdvFrame(
            vendor[4],
            vendor[5],
            vendor[6],
            vendor[7],
            BinaryPrimitives.ReadUInt16LittleEndian(vendor.Slice(8, 2)),
            vendor.Slice(10, 16).ToArray(),
            vendor.Slice(26, 16).ToArray(),
            BinaryPrimitives.ReadUInt32LittleEndian(vendor.Slice(42, 4)),
            vendor[46],
            vendor[47],
            vendor.Slice(AdvHeaderLen, payloadLen).ToArray());
        return true;
    }

    public static byte[] EncodeHostPayload(byte type, byte seq, byte cmd, byte status, byte fragIndex, byte fragCount, ushort totalLen, ushort messageCrc, ReadOnlySpan<byte> chunk)
    {
        if (chunk.Length > HostChunkMaxLen)
        {
            throw new ArgumentException($"HOST-ADV chunk too long: {chunk.Length}");
        }

        byte[] payload = new byte[HostHeaderLen + chunk.Length];
        payload[0] = HostMagicLo;
        payload[1] = HostMagicHi;
        payload[2] = HostVersion;
        payload[3] = type;
        payload[4] = seq;
        payload[5] = cmd;
        payload[6] = status;
        payload[7] = fragIndex;
        payload[8] = fragCount;
        BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(9), totalLen);
        BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(11), messageCrc);
        payload[13] = (byte)chunk.Length;
        chunk.CopyTo(payload.AsSpan(HostHeaderLen));
        return payload;
    }

    public static bool TryDecodeHostPayload(ReadOnlySpan<byte> payload, out HostPacket? packet, out string error)
    {
        packet = null;
        error = string.Empty;

        if (payload.Length < HostHeaderLen)
        {
            error = "HOST-ADV payload too short";
            return false;
        }
        if (payload[0] != HostMagicLo || payload[1] != HostMagicHi || payload[2] != HostVersion)
        {
            error = "bad HOST-ADV magic/version";
            return false;
        }
        int chunkLen = payload[13];
        if (payload.Length != HostHeaderLen + chunkLen)
        {
            error = "bad HOST-ADV chunk length";
            return false;
        }

        packet = new HostPacket(
            payload[3],
            payload[4],
            payload[5],
            payload[6],
            payload[7],
            payload[8],
            BinaryPrimitives.ReadUInt16LittleEndian(payload.Slice(9, 2)),
            BinaryPrimitives.ReadUInt16LittleEndian(payload.Slice(11, 2)),
            payload.Slice(HostHeaderLen, chunkLen).ToArray());
        return true;
    }

    public static List<byte[]> BuildHostCommandVendorPayloads(byte[] dstEid, byte cmd, byte[] payload, byte seq, byte[]? srcEid = null, int repeat = 3)
    {
        srcEid ??= DefaultHostEid;
        if (srcEid.Length != 16 || dstEid.Length != 16)
        {
            throw new ArgumentException("EID must be 16 bytes.");
        }
        if (payload.Length > HostMessageMaxLen)
        {
            throw new ArgumentException($"Host message too long: {payload.Length}");
        }

        ushort messageCrc = Crc16(payload);
        var chunks = payload.Chunk(HostChunkMaxLen).Select(c => c.ToArray()).ToList();
        if (chunks.Count == 0)
        {
            chunks.Add(Array.Empty<byte>());
        }

        var result = new List<byte[]>();
        ushort frameSeq = 0;
        uint messageId = seq;
        for (int r = 0; r < repeat; r++)
        {
            for (int i = 0; i < chunks.Count; i++)
            {
                byte[] hostPayload = EncodeHostPayload(
                    HostTypeCmd,
                    seq,
                    cmd,
                    0,
                    (byte)i,
                    (byte)chunks.Count,
                    (ushort)payload.Length,
                    messageCrc,
                    chunks[i]);

                var frame = new AdvFrame(
                    FrameData,
                    0,
                    1,
                    0,
                    frameSeq++,
                    srcEid,
                    dstEid,
                    messageId++,
                    (byte)i,
                    (byte)chunks.Count,
                    hostPayload);
                result.Add(EncodeVendorPayload(frame));
            }
        }
        return result;
    }

    public static string Hex(ReadOnlySpan<byte> data)
    {
        return Convert.ToHexString(data);
    }

    public static byte[] ParseHex(string text)
    {
        string normalized = text.Replace("0x", "", StringComparison.OrdinalIgnoreCase)
            .Replace(":", "")
            .Replace("-", "")
            .Replace(" ", "")
            .Replace(",", "");
        if (normalized.Length % 2 != 0)
        {
            throw new ArgumentException("Hex string length must be even.");
        }
        return Convert.FromHexString(normalized);
    }
}
