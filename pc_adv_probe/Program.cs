using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Storage.Streams;

namespace PendantAdvProbe;

internal static class Program
{
    private static byte _seq = 1;

    public static async Task<int> Main(string[] args)
    {
        Console.OutputEncoding = System.Text.Encoding.UTF8;

        try
        {
            if (args.Length == 0 || IsHelp(args[0]))
            {
                PrintHelp();
                return 0;
            }

            string command = args[0].ToLowerInvariant();
            return command switch
            {
                "scan" => await RunScanAsync(args),
                "info" => await RunInfoAsync(args),
                "send" => await RunSendAsync(args),
                "selftest" => RunSelfTest(),
                _ => UnknownCommand(command),
            };
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("ERROR " + ex);
            return 1;
        }
    }

    private static bool IsHelp(string value)
    {
        return value is "-h" or "--help" or "help" or "/?";
    }

    private static void PrintHelp()
    {
        Console.WriteLine("""
蓝牙吊坠 WinRT 扩展广播验证工具

用法:
  dotnet run --project pc_adv_probe -- scan [seconds]
  dotnet run --project pc_adv_probe -- info [eid_hex] [listen_seconds]
  dotnet run --project pc_adv_probe -- send <eid_hex> <cmd_hex> [payload_hex] [listen_seconds]
  dotnet run --project pc_adv_probe -- selftest

示例:
  dotnet run --project pc_adv_probe -- scan 15
  dotnet run --project pc_adv_probe -- info
  dotnet run --project pc_adv_probe -- info 00112233445566778899AABBCCDDEEFF
  dotnet run --project pc_adv_probe -- send 00112233445566778899AABBCCDDEEFF 0x02

说明:
  scan 只扫描并解析 PENDANT-ADV。
  info 如果不传 EID，会先扫描 8 秒，选择 RSSI 最强的 Beacon 作为目标。
  send 使用 HOST-ADV 通过扩展广播发送命令，并继续监听设备回包。
""");
    }

    private static int UnknownCommand(string command)
    {
        Console.Error.WriteLine($"未知命令: {command}");
        PrintHelp();
        return 2;
    }

    private static async Task<int> RunScanAsync(string[] args)
    {
        int seconds = args.Length >= 2 ? int.Parse(args[1]) : 10;
        Console.WriteLine($"扫描 {seconds}s，AllowExtendedAdvertisements=True ...");
        var result = await ScanPendantFramesAsync(TimeSpan.FromSeconds(seconds), printFrames: true);
        Console.WriteLine();
        Console.WriteLine($"扫描结束，发现 {result.Beacons.Count} 个 Beacon EID。");
        foreach (var target in result.Beacons.Values.OrderByDescending(x => x.Rssi))
        {
            Console.WriteLine($"  EID={PendantAdvProtocol.Hex(target.Frame.SourceEid)} RSSI={target.Rssi} addr=0x{target.BluetoothAddress:X12}");
        }
        return 0;
    }

    private static async Task<int> RunInfoAsync(string[] args)
    {
        byte[]? eid = null;
        int listenSeconds = 10;

        if (args.Length >= 2)
        {
            eid = PendantAdvProtocol.ParseHex(args[1]);
            if (args.Length >= 3)
            {
                listenSeconds = int.Parse(args[2]);
            }
        }
        else
        {
            Console.WriteLine("未指定 EID，先扫描 8s 自动选择 RSSI 最强的 Beacon。");
            var scan = await ScanPendantFramesAsync(TimeSpan.FromSeconds(8), printFrames: true);
            var best = scan.Beacons.Values.OrderByDescending(x => x.Rssi).FirstOrDefault();
            if (best is null)
            {
                Console.Error.WriteLine("未发现吊坠 Beacon。");
                return 3;
            }
            eid = best.Frame.SourceEid;
            Console.WriteLine($"选择目标 EID={PendantAdvProtocol.Hex(eid)} RSSI={best.Rssi}");
        }

        return await SendAndListenAsync(eid, PendantAdvProtocol.CmdGetDeviceInfo, Array.Empty<byte>(), listenSeconds);
    }

    private static async Task<int> RunSendAsync(string[] args)
    {
        if (args.Length < 3)
        {
            Console.Error.WriteLine("send 需要参数: <eid_hex> <cmd_hex> [payload_hex] [listen_seconds]");
            return 2;
        }

        byte[] eid = PendantAdvProtocol.ParseHex(args[1]);
        byte cmd = Convert.ToByte(args[2].StartsWith("0x", StringComparison.OrdinalIgnoreCase) ? args[2][2..] : args[2], 16);
        byte[] payload = args.Length >= 4 ? PendantAdvProtocol.ParseHex(args[3]) : Array.Empty<byte>();
        int listenSeconds = args.Length >= 5 ? int.Parse(args[4]) : 10;

        return await SendAndListenAsync(eid, cmd, payload, listenSeconds);
    }

    private static int RunSelfTest()
    {
        byte[] dst = PendantAdvProtocol.ParseHex("00112233445566778899AABBCCDDEEFF");
        List<byte[]> payloads = PendantAdvProtocol.BuildHostCommandVendorPayloads(
            dst,
            PendantAdvProtocol.CmdGetDeviceInfo,
            Array.Empty<byte>(),
            7);

        if (payloads.Count != 3)
        {
            throw new InvalidOperationException("repeat count mismatch");
        }
        if (!PendantAdvProtocol.TryDecodeVendorPayload(payloads[0], out var frame, out var advError) || frame is null)
        {
            throw new InvalidOperationException(advError);
        }
        if (!PendantAdvProtocol.TryDecodeHostPayload(frame.Payload, out var packet, out var hostError) || packet is null)
        {
            throw new InvalidOperationException(hostError);
        }
        if (packet.Seq != 7 || packet.Cmd != PendantAdvProtocol.CmdGetDeviceInfo || packet.TotalLen != 0)
        {
            throw new InvalidOperationException("HOST-ADV decode mismatch");
        }

        Console.WriteLine("selftest ok");
        Console.WriteLine($"sample vendor payload len={payloads[0].Length}, hex={PendantAdvProtocol.Hex(payloads[0])}");
        return 0;
    }

    private static async Task<int> SendAndListenAsync(byte[] eid, byte cmd, byte[] payload, int listenSeconds)
    {
        if (eid.Length != 16)
        {
            Console.Error.WriteLine("目标 EID 必须是 16 字节。");
            return 2;
        }

        byte seq = NextSeq();
        List<byte[]> vendorPayloads = PendantAdvProtocol.BuildHostCommandVendorPayloads(eid, cmd, payload, seq);

        Console.WriteLine($"TX cmd=0x{cmd:X2} seq={seq} target={PendantAdvProtocol.Hex(eid)} payload={payload.Length} bytes");
        Console.WriteLine($"将发送 {vendorPayloads.Count} 个扩展广播 manufacturer payload。");

        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(listenSeconds));
        Task listenTask = ListenForResponsesAsync(seq, cts.Token);
        await PublishVendorPayloadsAsync(vendorPayloads, TimeSpan.FromMilliseconds(180), TimeSpan.FromMilliseconds(80));
        Console.WriteLine($"广播发送完成，继续监听 {listenSeconds}s ...");
        await listenTask;
        return 0;
    }

    private static byte NextSeq()
    {
        byte value = _seq;
        _seq = _seq >= 255 ? (byte)1 : (byte)(_seq + 1);
        return value;
    }

    private static async Task PublishVendorPayloadsAsync(IReadOnlyList<byte[]> vendorPayloads, TimeSpan onAirTime, TimeSpan gapTime)
    {
        foreach (byte[] vendorPayload in vendorPayloads)
        {
            var publisher = new BluetoothLEAdvertisementPublisher
            {
                UseExtendedAdvertisement = true,
            };
            publisher.Advertisement.ManufacturerData.Add(
                new BluetoothLEManufacturerData(PendantAdvProtocol.CompanyId, ToBuffer(vendorPayload)));

            var statusChanged = new TaskCompletionSource<BluetoothLEAdvertisementPublisherStatus>(TaskCreationOptions.RunContinuationsAsynchronously);
            publisher.StatusChanged += (_, args) =>
            {
                Console.WriteLine($"  publisher status={args.Status} error={args.Error}");
                statusChanged.TrySetResult(args.Status);
            };

            Console.WriteLine($"  publish len={vendorPayload.Length} hex={PendantAdvProtocol.Hex(vendorPayload.AsSpan(0, Math.Min(24, vendorPayload.Length)))}...");
            publisher.Start();
            await Task.WhenAny(statusChanged.Task, Task.Delay(800));
            if (publisher.Status == BluetoothLEAdvertisementPublisherStatus.Aborted)
            {
                Console.WriteLine("  publisher aborted，停止本轮发送。");
                publisher.Stop();
                break;
            }
            await Task.Delay(onAirTime);
            publisher.Stop();
            await Task.Delay(gapTime);
        }
    }

    private static async Task ListenForResponsesAsync(byte seq, CancellationToken cancellationToken)
    {
        var assembler = new HostAdvAssembler();
        var watcher = CreateWatcher();
        watcher.Received += (_, args) =>
        {
            foreach (BluetoothLEManufacturerData item in args.Advertisement.ManufacturerData)
            {
                if (item.CompanyId != PendantAdvProtocol.CompanyId)
                {
                    continue;
                }

                byte[] vendor = FromBuffer(item.Data);
                if (!PendantAdvProtocol.TryDecodeVendorPayload(vendor, out var frame, out string _advError) || frame is null)
                {
                    continue;
                }
                if (frame.FrameType != PendantAdvProtocol.FrameData)
                {
                    continue;
                }
                if (!PendantAdvProtocol.TryDecodeHostPayload(frame.Payload, out var packet, out string _hostError) || packet is null)
                {
                    continue;
                }

                Console.WriteLine(
                    $"RX HOST type={packet.Type} seq={packet.Seq} cmd=0x{packet.Cmd:X2} status=0x{packet.Status:X2} " +
                    $"frag={packet.FragmentIndex + 1}/{packet.FragmentCount} rssi={args.RawSignalStrengthInDBm}");

                if (packet.Seq != seq)
                {
                    return;
                }
                if (assembler.Push(packet, out byte[]? message) && message is not null)
                {
                    PrintHostMessage(packet, message);
                }
            }
        };

        watcher.Start();
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(100, cancellationToken).ContinueWith(_ => { });
            }
        }
        finally
        {
            watcher.Stop();
        }
    }

    private static async Task<ScanResult> ScanPendantFramesAsync(TimeSpan duration, bool printFrames)
    {
        var result = new ScanResult();
        var watcher = CreateWatcher();

        watcher.Received += (_, args) =>
        {
            foreach (BluetoothLEManufacturerData item in args.Advertisement.ManufacturerData)
            {
                if (item.CompanyId != PendantAdvProtocol.CompanyId)
                {
                    continue;
                }

                byte[] vendor = FromBuffer(item.Data);
                if (!PendantAdvProtocol.TryDecodeVendorPayload(vendor, out var frame, out string error) || frame is null)
                {
                    if (printFrames)
                    {
                        Console.WriteLine($"RX manufacturer len={vendor.Length}, parse error={error}");
                    }
                    continue;
                }

                string eid = PendantAdvProtocol.Hex(frame.SourceEid);
                if (frame.FrameType == PendantAdvProtocol.FrameBeacon)
                {
                    result.Beacons[eid] = new ScanTarget(frame, args.RawSignalStrengthInDBm, args.BluetoothAddress);
                }

                if (printFrames)
                {
                    string name = string.IsNullOrWhiteSpace(args.Advertisement.LocalName) ? "-" : args.Advertisement.LocalName;
                    Console.WriteLine(
                        $"RX addr=0x{args.BluetoothAddress:X12} name={name} rssi={args.RawSignalStrengthInDBm} " +
                        $"type=0x{frame.FrameType:X2} seq={frame.FrameSeq} eid={eid} payload={frame.Payload.Length}");
                }
            }
        };

        watcher.Start();
        await Task.Delay(duration);
        watcher.Stop();
        return result;
    }

    private static BluetoothLEAdvertisementWatcher CreateWatcher()
    {
        var watcher = new BluetoothLEAdvertisementWatcher
        {
            ScanningMode = BluetoothLEScanningMode.Active,
            AllowExtendedAdvertisements = true,
        };
        watcher.Stopped += (_, args) =>
        {
            if (args.Error != BluetoothError.Success)
            {
                Console.WriteLine($"watcher stopped, error={args.Error}");
            }
        };
        return watcher;
    }

    private static IBuffer ToBuffer(byte[] bytes)
    {
        var writer = new DataWriter();
        writer.WriteBytes(bytes);
        return writer.DetachBuffer();
    }

    private static byte[] FromBuffer(IBuffer buffer)
    {
        var reader = DataReader.FromBuffer(buffer);
        byte[] bytes = new byte[buffer.Length];
        reader.ReadBytes(bytes);
        return bytes;
    }

    private static void PrintHostMessage(PendantAdvProtocol.HostPacket packet, byte[] payload)
    {
        Console.WriteLine($"RX MESSAGE type={packet.Type} cmd=0x{packet.Cmd:X2} status=0x{packet.Status:X2} payload={PendantAdvProtocol.Hex(payload)}");
        if (packet.Type == PendantAdvProtocol.HostTypeRsp && packet.Cmd == PendantAdvProtocol.CmdGetDeviceInfo && payload.Length >= 36)
        {
            uint shortId = BitConverter.ToUInt32(payload, 8);
            string eid = PendantAdvProtocol.Hex(payload.AsSpan(12, 16));
            ushort cmdCount = BitConverter.ToUInt16(payload, 28);
            ushort logCount = BitConverter.ToUInt16(payload, 30);
            Console.WriteLine($"  DEVICE_INFO short_id=0x{shortId:X8} eid={eid} cmd_count={cmdCount} log_count={logCount}");
        }
    }

    private sealed class HostAdvAssembler
    {
        private byte _type;
        private byte _seq;
        private byte _cmd;
        private byte _status;
        private byte _fragCount;
        private ushort _totalLen;
        private ushort _messageCrc;
        private readonly byte[][] _chunks = new byte[8][];

        public bool Push(PendantAdvProtocol.HostPacket packet, out byte[]? message)
        {
            message = null;
            if (packet.FragmentCount == 0 || packet.FragmentCount > _chunks.Length || packet.FragmentIndex >= packet.FragmentCount)
            {
                Reset();
                return false;
            }

            if (_fragCount == 0 ||
                _type != packet.Type ||
                _seq != packet.Seq ||
                _cmd != packet.Cmd ||
                _fragCount != packet.FragmentCount ||
                _totalLen != packet.TotalLen ||
                _messageCrc != packet.MessageCrc)
            {
                Reset();
                _type = packet.Type;
                _seq = packet.Seq;
                _cmd = packet.Cmd;
                _status = packet.Status;
                _fragCount = packet.FragmentCount;
                _totalLen = packet.TotalLen;
                _messageCrc = packet.MessageCrc;
            }

            _chunks[packet.FragmentIndex] = packet.Chunk;
            for (int i = 0; i < _fragCount; i++)
            {
                if (_chunks[i] is null)
                {
                    return false;
                }
            }

            byte[] joined = _chunks.Take(_fragCount).SelectMany(x => x).Take(_totalLen).ToArray();
            if (joined.Length != _totalLen || PendantAdvProtocol.Crc16(joined) != _messageCrc)
            {
                Reset();
                return false;
            }

            message = joined;
            packet = packet with { Status = _status };
            Reset();
            return true;
        }

        private void Reset()
        {
            _type = 0;
            _seq = 0;
            _cmd = 0;
            _status = 0;
            _fragCount = 0;
            _totalLen = 0;
            _messageCrc = 0;
            Array.Clear(_chunks);
        }
    }

    private sealed record ScanTarget(PendantAdvProtocol.AdvFrame Frame, short Rssi, ulong BluetoothAddress);

    private sealed class ScanResult
    {
        public Dictionary<string, ScanTarget> Beacons { get; } = new();
    }
}
