package com.glimmer.app;

import android.Manifest;
import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothStatusCodes;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.util.Log;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Queue;
import java.util.UUID;

final class GlimmerBleClient {
    private static final String TAG = "GlimmerBleClient";
    private static final String[] DEBUG_SERVICE_UUIDS = {
            "50544e44-0001-4b45-5931-444556000001",
            "56000001-4445-5931-454b-0100444e5450",
            "01000056-4544-3159-4b45-000150544e44",
    };
    private static final String[] DEBUG_CMD_UUIDS = {
            "50544e44-0002-4b45-5931-444556000001",
            "56000001-4445-5931-454b-0200444e5450",
            "01000056-4544-3159-4b45-000250544e44",
    };
    private static final String[] DEBUG_RSP_UUIDS = {
            "50544e44-0003-4b45-5931-444556000001",
            "56000001-4445-5931-454b-0300444e5450",
            "01000056-4544-3159-4b45-000350544e44",
    };
    private static final String[] DEBUG_LOG_UUIDS = {
            "50544e44-0004-4b45-5931-444556000001",
            "56000001-4445-5931-454b-0400444e5450",
            "01000056-4544-3159-4b45-000450544e44",
    };
    private static final String[] DEBUG_EVT_UUIDS = {
            "50544e44-0005-4b45-5931-444556000001",
            "56000001-4445-5931-454b-0500444e5450",
            "01000056-4544-3159-4b45-000550544e44",
    };
    private static final UUID CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private static final boolean CMD_WRITE_WITHOUT_RESPONSE = true;
    private static final long CMD_WRITE_PACE_MS = 35;

    private final Context context;
    private final Listener listener;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final HostProtocol.MessageAssembler assembler = new HostProtocol.MessageAssembler();
    private final Map<String, ScanDevice> scanMap = new HashMap<>();
    private final List<ScanDevice> scanDevices = new ArrayList<>();
    private final Queue<byte[]> txQueue = new ArrayDeque<>();
    private final Queue<BluetoothGattCharacteristic> notifyQueue = new ArrayDeque<>();

    private BluetoothAdapter adapter;
    private BluetoothLeScanner scanner;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic cmdChar;
    private BluetoothGattCharacteristic rspChar;
    private BluetoothGattCharacteristic logChar;
    private BluetoothGattCharacteristic evtChar;

    private boolean scanning;
    private boolean connected;
    private boolean debugReady;
    private boolean writeInProgress;
    private String connectedAddress;
    private String pendingAddress;
    private int seq = 1;

    GlimmerBleClient(Context context, Listener listener) {
        this.context = context.getApplicationContext();
        this.listener = listener;
        BluetoothManager manager = (BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE);
        adapter = manager == null ? null : manager.getAdapter();
    }

    boolean isScanning() {
        return scanning;
    }

    boolean isDebugReady() {
        return debugReady;
    }

    List<ScanDevice> getScanDevices() {
        return Collections.unmodifiableList(scanDevices);
    }

    static List<String> runtimePermissions() {
        List<String> permissions = new ArrayList<>();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions.add(Manifest.permission.BLUETOOTH_SCAN);
            permissions.add(Manifest.permission.BLUETOOTH_CONNECT);
        }
        permissions.add(Manifest.permission.ACCESS_COARSE_LOCATION);
        permissions.add(Manifest.permission.ACCESS_FINE_LOCATION);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions.add(Manifest.permission.POST_NOTIFICATIONS);
        }
        return permissions;
    }

    boolean isBluetoothReady() {
        refreshAdapter();
        return adapter != null && adapter.isEnabled();
    }

    @SuppressLint("MissingPermission")
    void startScan() {
        refreshAdapter();
        if (adapter == null) {
            listener.onBleStatus("本机不支持蓝牙");
            return;
        }
        if (!adapter.isEnabled()) {
            listener.onBleStatus("蓝牙未开启");
            return;
        }

        stopScan();
        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) {
            listener.onBleStatus("无法获取 BLE Scanner");
            return;
        }

        scanMap.clear();
        scanDevices.clear();
        scanning = true;
        listener.onBleStatus("正在寻找你的 Glimmer");
        listener.onScanDevicesChanged(snapshot());

        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build();
        scanner.startScan(null, settings, scanCallback);
        handler.postDelayed(this::stopScan, 7000);
    }

    @SuppressLint("MissingPermission")
    void stopScan() {
        if (!scanning) {
            return;
        }
        scanning = false;
        if (scanner != null) {
            try {
                scanner.stopScan(scanCallback);
            } catch (RuntimeException ignored) {
            }
        }
        listener.onBleStatus("扫描结束，发现 " + scanDevices.size() + " 个候选设备");
        listener.onScanDevicesChanged(snapshot());
    }

    @SuppressLint("MissingPermission")
    void connect(ScanDevice device) {
        if (device == null) {
            return;
        }
        refreshAdapter();
        if (adapter == null) {
            listener.onBleStatus("蓝牙不可用");
            return;
        }
        stopScan();
        if (gatt != null && device.address.equals(connectedAddress)) {
            if (debugReady) {
                listener.onBleStatus("Glimmer 已连接，正在同步");
                requestFullSync();
            } else {
                listener.onBleStatus("Glimmer 正在完成连接");
            }
            return;
        }
        if (gatt != null && device.address.equals(pendingAddress)) {
            listener.onBleStatus("Glimmer 正在完成连接");
            return;
        }
        disconnect();
        try {
            BluetoothDevice remote = adapter.getRemoteDevice(device.address);
            listener.onBleStatus("正在连接 " + device.displayName());
            pendingAddress = device.address;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                gatt = remote.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE);
            } else {
                gatt = remote.connectGatt(context, false, gattCallback);
            }
        } catch (IllegalArgumentException e) {
            listener.onBleStatus("连接失败：" + e.getMessage());
        }
    }

    @SuppressLint("MissingPermission")
    void disconnect() {
        connected = false;
        debugReady = false;
        writeInProgress = false;
        txQueue.clear();
        notifyQueue.clear();
        assembler.reset();
        connectedAddress = null;
        pendingAddress = null;
        cmdChar = null;
        rspChar = null;
        logChar = null;
        evtChar = null;
        if (gatt != null) {
            try {
                gatt.disconnect();
                gatt.close();
            } catch (RuntimeException ignored) {
            }
            gatt = null;
        }
    }

    void requestDeviceInfo() {
        sendPackets(HostProtocol.makeEmptyCommand(nextSeq(), HostProtocol.CMD_GET_DEVICE_INFO));
    }

    void requestPeerTable() {
        sendPackets(HostProtocol.makeEmptyCommand(nextSeq(), HostProtocol.CMD_GET_PEER_TABLE));
    }

    void requestProfileSummary() {
        sendPackets(HostProtocol.makeEmptyCommand(nextSeq(), HostProtocol.CMD_GET_PROFILE_SUMMARY));
    }

    void requestPeerProfiles() {
        requestPeerProfilesPage(0);
    }

    int requestPeerProfilesPage(int startIndex) {
        int seq = nextSeq();
        sendPackets(HostProtocol.makePeerProfilesCommand(seq, startIndex));
        return seq;
    }

    void setProfileSummary(String nickname, String signature) {
        int seed = (int) (System.currentTimeMillis() & 0x7fffffff);
        sendPackets(HostProtocol.makeSetProfileSummary(nextSeq(), nickname, signature, seed, new int[]{1, 2, 3}));
    }

    void requestFullSync() {
        requestDeviceInfo();
        handler.postDelayed(this::requestPeerTable, 250);
        handler.postDelayed(this::requestProfileSummary, 500);
    }

    private void refreshAdapter() {
        if (adapter == null) {
            BluetoothManager manager = (BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE);
            adapter = manager == null ? null : manager.getAdapter();
        }
    }

    private final ScanCallback scanCallback = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            handler.post(() -> addScanResult(result));
        }

        @Override
        public void onBatchScanResults(List<ScanResult> results) {
            handler.post(() -> {
                for (ScanResult result : results) {
                    addScanResult(result);
                }
            });
        }

        @Override
        public void onScanFailed(int errorCode) {
            handler.post(() -> {
                scanning = false;
                listener.onBleStatus("扫描失败：" + errorCode);
                listener.onScanDevicesChanged(snapshot());
            });
        }
    };

    @SuppressLint("MissingPermission")
    private void addScanResult(ScanResult result) {
        BluetoothDevice device = result.getDevice();
        String address = device.getAddress();
        String name = "";
        ScanRecord record = result.getScanRecord();
        if (record != null && record.getDeviceName() != null) {
            name = record.getDeviceName();
        }
        if ((name == null || name.isEmpty()) && adapter != null) {
            name = device.getName();
        }
        if (name == null) {
            name = "";
        }

        boolean serviceMatch = hasDebugService(record);
        String upperName = name.toUpperCase(Locale.US);
        boolean nameMatch = upperName.contains("PENDANT")
                || upperName.contains("GLIMMER")
                || name.contains("微光");
        if (!serviceMatch && !nameMatch) {
            return;
        }

        ScanDevice item = scanMap.get(address);
        if (item == null) {
            item = new ScanDevice(address);
            scanMap.put(address, item);
            scanDevices.add(item);
        }
        item.name = name.isEmpty() ? "(unknown)" : name;
        item.rssi = result.getRssi();
        item.serviceMatch = serviceMatch;
        item.nameMatch = nameMatch;
        listener.onScanDevicesChanged(snapshot());
    }

    private boolean hasDebugService(ScanRecord record) {
        if (record == null || record.getServiceUuids() == null) {
            return false;
        }
        for (ParcelUuid parcelUuid : record.getServiceUuids()) {
            if (uuidMatches(parcelUuid.getUuid(), DEBUG_SERVICE_UUIDS)) {
                return true;
            }
        }
        return false;
    }

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            Log.i(TAG, "onConnectionStateChange status=" + status + " newState=" + newState
                    + " device=" + gatt.getDevice().getAddress());
            handler.post(() -> {
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    connected = true;
                    connectedAddress = pendingAddress == null ? gatt.getDevice().getAddress() : pendingAddress;
                    pendingAddress = null;
                    listener.onBleStatus("已连接，正在发现服务");
                    gatt.discoverServices();
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    Log.w(TAG, "Glimmer disconnected status=" + status);
                    listener.onBleStatus("Glimmer 已断开");
                    listener.onBleStatus("Glimmer 已断开 status=" + status);
                    connected = false;
                    debugReady = false;
                    writeInProgress = false;
                    txQueue.clear();
                    notifyQueue.clear();
                    assembler.reset();
                    connectedAddress = null;
                    pendingAddress = null;
                    if (GlimmerBleClient.this.gatt == gatt) {
                        try {
                            gatt.close();
                        } catch (RuntimeException ignored) {
                        }
                        GlimmerBleClient.this.gatt = null;
                    }
                    listener.onDebugReady(false);
                }
            });
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            Log.i(TAG, "onServicesDiscovered status=" + status);
            handler.post(() -> {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    listener.onBleStatus("GATT 服务发现失败");
                    return;
                }
                findDebugCharacteristics(gatt.getServices());
                if (cmdChar == null || rspChar == null || logChar == null || evtChar == null) {
                    listener.onBleStatus("未找到完整 Glimmer 调试通道");
                    return;
                }
                notifyQueue.clear();
                notifyQueue.add(rspChar);
                notifyQueue.add(logChar);
                notifyQueue.add(evtChar);
                subscribeNext();
            });
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt gatt, BluetoothGattDescriptor descriptor, int status) {
            Log.d(TAG, "onDescriptorWrite status=" + status + " uuid=" + descriptor.getUuid());
            handler.post(GlimmerBleClient.this::subscribeNext);
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, int status) {
            handler.post(() -> {
                Log.d(TAG, "onCharacteristicWrite status=" + status
                        + " uuid=" + characteristic.getUuid());
                if (CMD_WRITE_WITHOUT_RESPONSE) {
                    return;
                }
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    Log.w(TAG, "onCharacteristicWrite failed status=" + status
                            + " uuid=" + characteristic.getUuid());
                    listener.onBleStatus("发送失败：" + status);
                }
                handler.postDelayed(GlimmerBleClient.this::writeNextPacket, 20);
            });
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic) {
            byte[] value = characteristic.getValue();
            if (value != null) {
                handler.post(() -> onNotify(value));
            }
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, byte[] value) {
            handler.post(() -> onNotify(value));
        }
    };

    private void findDebugCharacteristics(List<BluetoothGattService> services) {
        cmdChar = null;
        rspChar = null;
        logChar = null;
        evtChar = null;
        for (BluetoothGattService service : services) {
            for (BluetoothGattCharacteristic characteristic : service.getCharacteristics()) {
                UUID uuid = characteristic.getUuid();
                if (uuidMatches(uuid, DEBUG_CMD_UUIDS)) {
                    cmdChar = characteristic;
                } else if (uuidMatches(uuid, DEBUG_RSP_UUIDS)) {
                    rspChar = characteristic;
                } else if (uuidMatches(uuid, DEBUG_LOG_UUIDS)) {
                    logChar = characteristic;
                } else if (uuidMatches(uuid, DEBUG_EVT_UUIDS)) {
                    evtChar = characteristic;
                }
            }
        }
    }

    @SuppressLint("MissingPermission")
    private void subscribeNext() {
        if (gatt == null) {
            return;
        }
        BluetoothGattCharacteristic characteristic = notifyQueue.poll();
        if (characteristic == null) {
            Log.i(TAG, "debug channel ready; requesting full sync");
            debugReady = true;
            listener.onBleStatus("我的 Glimmer 已连接");
            listener.onDebugReady(true);
            requestFullSync();
            return;
        }

        Log.d(TAG, "subscribe characteristic uuid=" + characteristic.getUuid()
                + " remaining=" + notifyQueue.size());
        gatt.setCharacteristicNotification(characteristic, true);
        BluetoothGattDescriptor descriptor = characteristic.getDescriptor(CCCD_UUID);
        if (descriptor == null) {
            Log.w(TAG, "missing CCCD for uuid=" + characteristic.getUuid());
            subscribeNext();
            return;
        }

        boolean ok;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            ok = gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                    == BluetoothStatusCodes.SUCCESS;
        } else {
            descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
            ok = gatt.writeDescriptor(descriptor);
        }
        if (!ok) {
            Log.w(TAG, "writeDescriptor returned false uuid=" + characteristic.getUuid());
            subscribeNext();
        }
    }

    private void sendPackets(List<byte[]> packets) {
        if (!debugReady || gatt == null || cmdChar == null) {
            Log.w(TAG, "drop host command: not ready debugReady=" + debugReady
                    + " gatt=" + (gatt != null) + " cmdChar=" + (cmdChar != null)
                    + " packets=" + packets.size());
            listener.onBleStatus("Glimmer 还没有连接好");
            return;
        }
        Log.d(TAG, "queue host packets count=" + packets.size()
                + " pendingBefore=" + txQueue.size());
        txQueue.addAll(packets);
        if (!writeInProgress) {
            writeNextPacket();
        }
    }

    @SuppressLint("MissingPermission")
    private void writeNextPacket() {
        if (gatt == null || cmdChar == null) {
            writeInProgress = false;
            txQueue.clear();
            return;
        }
        byte[] packet = txQueue.poll();
        if (packet == null) {
            Log.d(TAG, "tx queue drained");
            writeInProgress = false;
            return;
        }
        writeInProgress = true;
        Log.d(TAG, "write host packet len=" + packet.length
                + " remaining=" + txQueue.size());

        boolean ok;
        int writeType = CMD_WRITE_WITHOUT_RESPONSE
                ? BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                : BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            ok = gatt.writeCharacteristic(cmdChar, packet, writeType)
                    == BluetoothStatusCodes.SUCCESS;
        } else {
            cmdChar.setWriteType(writeType);
            cmdChar.setValue(packet);
            ok = gatt.writeCharacteristic(cmdChar);
        }

        if (!ok) {
            Log.w(TAG, "writeCharacteristic returned false len=" + packet.length);
            handler.postDelayed(this::writeNextPacket, 50);
        } else if (CMD_WRITE_WITHOUT_RESPONSE) {
            handler.postDelayed(this::writeNextPacket, CMD_WRITE_PACE_MS);
        }
    }

    private void onNotify(byte[] data) {
        Log.d(TAG, "notify len=" + (data == null ? 0 : data.length));
        try {
            HostProtocol.HostFrame frame = HostProtocol.decodePacket(data);
            HostProtocol.HostMessage message = assembler.push(frame);
            if (message != null) {
                listener.onHostMessage(message, HostProtocol.formatMessage(message));
            }
        } catch (Exception e) {
            listener.onBleStatus("Glimmer 数据解析失败");
        }
    }

    private int nextSeq() {
        int value = seq;
        seq = seq >= 255 ? 1 : seq + 1;
        return value;
    }

    private boolean uuidMatches(UUID uuid, String[] candidates) {
        String value = uuid.toString().toLowerCase(Locale.US);
        for (String candidate : candidates) {
            if (value.equals(candidate)) {
                return true;
            }
        }
        return false;
    }

    private List<ScanDevice> snapshot() {
        return new ArrayList<>(scanDevices);
    }

    interface Listener {
        void onBleStatus(String status);

        void onScanDevicesChanged(List<ScanDevice> devices);

        void onDebugReady(boolean ready);

        void onHostMessage(HostProtocol.HostMessage message, String formatted);
    }

    static final class ScanDevice {
        final String address;
        String name = "";
        int rssi = -127;
        boolean serviceMatch;
        boolean nameMatch;

        ScanDevice(String address) {
            this.address = address;
        }

        boolean isGlimmerCandidate() {
            return serviceMatch || nameMatch;
        }

        String displayName() {
            return name == null || name.isEmpty() ? "未知设备" : name;
        }
    }
}
