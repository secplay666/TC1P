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
    private static final String[] OTA_SERVICE_UUIDS = {
            "12190d0c-0b0a-0908-0706-050403020100",
            "0c0d1912-0a0b-0809-0706-050403020100",
            "00010203-0405-0607-0809-0a0b0c0d1912",
    };
    private static final String[] OTA_DATA_UUIDS = {
            "122b0d0c-0b0a-0908-0706-050403020100",
            "0c0d2b12-0a0b-0809-0706-050403020100",
            "00010203-0405-0607-0809-0a0b0c0d2b12",
    };
    private static final UUID CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private static final boolean CMD_WRITE_WITHOUT_RESPONSE = false;
    private static final long CMD_WRITE_PACE_MS = 35;
    private static final int OTA_CMD_START_EXT = 0xFF03;
    private static final int OTA_CMD_END = 0xFF02;
    private static final int OTA_CMD_RESULT = 0xFF06;
    private static final int OTA_CMD_SCHEDULE_PDU_NUM = 0xFF08;
    private static final int OTA_CMD_SCHEDULE_FW_SIZE = 0xFF09;
    private static final int OTA_PDU_LEN = 16;
    private static final int OTA_REQUEST_MTU = 83;
    private static final int OTA_MAX_FIRMWARE_BYTES = 192 * 1024;
    private static final long OTA_WRITE_PACE_MS = 2;

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
    private BluetoothGattCharacteristic otaChar;

    private boolean scanning;
    private boolean connected;
    private boolean debugReady;
    private boolean writeInProgress;
    private boolean otaActive;
    private boolean serviceDiscoveryRequested;
    private String connectedAddress;
    private String pendingAddress;
    private int seq = 1;
    private OtaSession otaSession;

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

    boolean isOtaReady() {
        return debugReady && gatt != null && otaChar != null && !otaActive;
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
        otaChar = null;
        otaActive = false;
        otaSession = null;
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
        setProfileSummary(nickname, signature, true);
    }

    void setProfileSummary(String nickname, String signature, boolean visible) {
        int seed = (int) (System.currentTimeMillis() & 0x7fffffff);
        sendPackets(HostProtocol.makeSetProfileSummary(nextSeq(), nickname, signature, seed, new int[]{1, 2, 3}, visible));
    }

    int sendP2pChat(String text) {
        ensureCommandReady();
        int seq = nextSeq();
        sendPackets(HostProtocol.makeP2pChatSend(seq, text));
        return seq;
    }

    int sendP2pChat(long targetShortId, String text) {
        ensureCommandReady();
        int seq = nextSeq();
        sendPackets(HostProtocol.makeP2pChatSend(seq, targetShortId, text));
        return seq;
    }

    int sendP2pFileFrame(long targetShortId, byte[] fileFrame) {
        ensureCommandReady();
        int seq = nextSeq();
        sendPackets(HostProtocol.makeP2pFileSend(seq, targetShortId, fileFrame));
        return seq;
    }

    void startOta(byte[] firmware, OtaListener otaListener) {
        if (otaActive) {
            if (otaListener != null) {
                otaListener.onOtaFailed("OTA already running");
            }
            return;
        }
        if (gatt == null || otaChar == null) {
            if (otaListener != null) {
                otaListener.onOtaFailed("OTA characteristic not found");
            }
            return;
        }
        String error = validateFirmware(firmware);
        if (error != null) {
            if (otaListener != null) {
                otaListener.onOtaFailed(error);
            }
            return;
        }

        otaActive = true;
        txQueue.clear();
        otaSession = new OtaSession(firmware, otaListener);
        listener.onBleStatus("OTA start");
        if (otaListener != null) {
            otaListener.onOtaProgress(0, otaSession.totalChunks, 0);
        }
        writeOtaStartWhenIdle();
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
                    serviceDiscoveryRequested = false;
                    connectedAddress = pendingAddress == null ? gatt.getDevice().getAddress() : pendingAddress;
                    pendingAddress = null;
                    listener.onBleStatus("已连接，正在发现服务");
                    if (!gatt.requestMtu(OTA_REQUEST_MTU)) {
                        discoverServices(gatt);
                    } else {
                        handler.postDelayed(() -> discoverServices(gatt), 1200);
                    }
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    Log.w(TAG, "Glimmer disconnected status=" + status);
                    listener.onBleStatus("Glimmer 已断开");
                    listener.onBleStatus("Glimmer 已断开 status=" + status);
                    if (otaActive && otaSession != null && otaSession.listener != null) {
                        if (otaSession.endSent) {
                            otaSession.listener.onOtaCompleted();
                        } else {
                            otaSession.listener.onOtaFailed("OTA disconnected");
                        }
                    }
                    connected = false;
                    debugReady = false;
                    serviceDiscoveryRequested = false;
                    writeInProgress = false;
                    otaActive = false;
                    otaSession = null;
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
                findOtaCharacteristic(gatt.getServices());
                if (cmdChar == null || rspChar == null || logChar == null || evtChar == null) {
                    listener.onBleStatus("未找到完整 Glimmer 调试通道");
                    return;
                }
                notifyQueue.clear();
                notifyQueue.add(rspChar);
                notifyQueue.add(logChar);
                notifyQueue.add(evtChar);
                if (otaChar != null) {
                    notifyQueue.add(otaChar);
                }
                subscribeNext();
            });
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt gatt, BluetoothGattDescriptor descriptor, int status) {
            Log.d(TAG, "onDescriptorWrite status=" + status + " uuid=" + descriptor.getUuid());
            handler.post(GlimmerBleClient.this::subscribeNext);
        }

        @Override
        public void onMtuChanged(BluetoothGatt gatt, int mtu, int status) {
            Log.i(TAG, "onMtuChanged status=" + status + " mtu=" + mtu);
            handler.post(() -> discoverServices(gatt));
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, int status) {
            handler.post(() -> {
                Log.d(TAG, "onCharacteristicWrite status=" + status
                        + " uuid=" + characteristic.getUuid());
                if (otaChar != null && characteristic.getUuid().equals(otaChar.getUuid())) {
                    if (status != BluetoothGatt.GATT_SUCCESS) {
                        failOta("OTA write failed: " + status);
                        return;
                    }
                    if (otaSession != null && otaSession.endSent
                            && otaSession.nextIndex >= otaSession.totalChunks) {
                        if (otaSession.listener != null) {
                            otaSession.listener.onOtaWaitingForResult();
                        }
                        return;
                    }
                    long delay = otaSession != null && otaSession.nextIndex == 0
                            ? 150
                            : OTA_WRITE_PACE_MS;
                    handler.postDelayed(GlimmerBleClient.this::writeNextOtaData, delay);
                    return;
                }
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
                handler.post(() -> onNotify(characteristic, value));
            }
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, byte[] value) {
            handler.post(() -> onNotify(characteristic, value));
        }
    };

    @SuppressLint("MissingPermission")
    private void discoverServices(BluetoothGatt targetGatt) {
        if (targetGatt == null || serviceDiscoveryRequested || !connected) {
            return;
        }
        serviceDiscoveryRequested = true;
        targetGatt.discoverServices();
    }

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

    private void findOtaCharacteristic(List<BluetoothGattService> services) {
        otaChar = null;
        BluetoothGattCharacteristic fallback = null;
        for (BluetoothGattService service : services) {
            boolean otaService = uuidMatches(service.getUuid(), OTA_SERVICE_UUIDS);
            for (BluetoothGattCharacteristic characteristic : service.getCharacteristics()) {
                UUID uuid = characteristic.getUuid();
                if (uuidMatches(uuid, OTA_DATA_UUIDS)) {
                    otaChar = characteristic;
                    return;
                }
                if (otaService && fallback == null) {
                    fallback = characteristic;
                }
            }
        }
        otaChar = fallback;
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
        if (otaActive) {
            Log.w(TAG, "drop host command during OTA");
            listener.onBleStatus("OTA in progress");
            return;
        }
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

    private void ensureCommandReady() {
        if (!debugReady || gatt == null || cmdChar == null) {
            throw new IllegalStateException("Glimmer is not ready for host commands");
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

    private void onNotify(BluetoothGattCharacteristic characteristic, byte[] data) {
        Log.d(TAG, "notify len=" + (data == null ? 0 : data.length));
        if (otaChar != null && characteristic != null
                && characteristic.getUuid().equals(otaChar.getUuid())) {
            onOtaNotify(data);
            return;
        }
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

    private void writeOtaStart() {
        if (!otaActive || otaSession == null || gatt == null || otaChar == null) {
            failOta("OTA connection lost");
            return;
        }
        if (!writeOtaPacket(otaStartExtPacket())) {
            failOta("OTA start failed");
            return;
        }
    }

    private void writeOtaStartWhenIdle() {
        if (!otaActive || otaSession == null || gatt == null || otaChar == null) {
            failOta("OTA connection lost");
            return;
        }
        if (writeInProgress) {
            handler.postDelayed(this::writeOtaStartWhenIdle, 50);
            return;
        }
        writeOtaStart();
    }

    private void writeNextOtaData() {
        if (!otaActive || otaSession == null || gatt == null || otaChar == null) {
            failOta("OTA connection lost");
            return;
        }
        if (otaSession.nextIndex >= otaSession.totalChunks) {
            otaSession.endSent = writeOtaPacket(otaEndPacket(otaSession.totalChunks - 1));
            if (!otaSession.endSent) {
                failOta("OTA end failed");
                return;
            }
            return;
        }

        int index = otaSession.nextIndex;
        int offset = index * OTA_PDU_LEN;
        if (!writeOtaPacket(otaDataPacket(index, otaSession.firmware, offset))) {
            failOta("OTA write failed");
            return;
        }
        otaSession.nextIndex++;
        if (otaSession.listener != null) {
            otaSession.listener.onOtaProgress(otaSession.nextIndex, otaSession.totalChunks,
                    otaSession.scheduleBytes);
        }
    }

    @SuppressLint("MissingPermission")
    private boolean writeOtaPacket(byte[] packet) {
        if (gatt == null || otaChar == null) {
            return false;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return gatt.writeCharacteristic(otaChar, packet,
                    BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothStatusCodes.SUCCESS;
        }
        otaChar.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
        otaChar.setValue(packet);
        return gatt.writeCharacteristic(otaChar);
    }

    private void onOtaNotify(byte[] data) {
        if (data == null || data.length < 2 || otaSession == null) {
            return;
        }
        int cmd = rd16(data, 0);
        if (cmd == OTA_CMD_RESULT && data.length >= 3) {
            int result = data[2] & 0xff;
            Log.i(TAG, "OTA result=" + result);
            OtaListener otaListener = otaSession.listener;
            otaActive = false;
            otaSession = null;
            if (result == 0) {
                listener.onBleStatus("OTA complete");
                if (otaListener != null) {
                    otaListener.onOtaCompleted();
                }
            } else if (otaListener != null) {
                otaListener.onOtaFailed("OTA result " + result);
            }
        } else if (cmd == OTA_CMD_SCHEDULE_PDU_NUM && data.length >= 4) {
            otaSession.schedulePdu = rd16(data, 2);
        } else if (cmd == OTA_CMD_SCHEDULE_FW_SIZE && data.length >= 6) {
            otaSession.scheduleBytes = rd32(data, 2);
        }
    }

    private void failOta(String reason) {
        OtaListener otaListener = otaSession == null ? null : otaSession.listener;
        otaActive = false;
        otaSession = null;
        if (otaListener != null) {
            otaListener.onOtaFailed(reason);
        }
        listener.onBleStatus(reason);
    }

    private String validateFirmware(byte[] firmware) {
        if (firmware == null || firmware.length < 0x1C) {
            return "Firmware too small";
        }
        if (firmware.length > OTA_MAX_FIRMWARE_BYTES) {
            return "Firmware exceeds " + OTA_MAX_FIRMWARE_BYTES + " bytes";
        }
        if ((firmware[0x08] & 0xff) != 0x4B) {
            return "Invalid Telink boot mark";
        }
        int declared = rd32(firmware, 0x18);
        if (declared != firmware.length) {
            return "Firmware size mismatch";
        }
        return null;
    }

    private byte[] otaStartExtPacket() {
        byte[] packet = new byte[20];
        wr16(packet, 0, OTA_CMD_START_EXT);
        packet[2] = (byte) OTA_PDU_LEN;
        packet[3] = 0;
        return packet;
    }

    private byte[] otaDataPacket(int index, byte[] firmware, int offset) {
        byte[] packet = new byte[OTA_PDU_LEN + 4];
        wr16(packet, 0, index);
        int copy = Math.min(OTA_PDU_LEN, firmware.length - offset);
        if (copy > 0) {
            System.arraycopy(firmware, offset, packet, 2, copy);
        }
        for (int i = 2 + copy; i < 2 + OTA_PDU_LEN; i++) {
            packet[i] = (byte) 0xFF;
        }
        int crc = crc16(packet, 0, OTA_PDU_LEN + 2);
        wr16(packet, OTA_PDU_LEN + 2, crc);
        return packet;
    }

    private byte[] otaEndPacket(int maxIndex) {
        byte[] packet = new byte[6];
        wr16(packet, 0, OTA_CMD_END);
        wr16(packet, 2, maxIndex);
        wr16(packet, 4, ~maxIndex);
        return packet;
    }

    private static int crc16(byte[] data, int offset, int len) {
        int crc = 0xFFFF;
        for (int i = 0; i < len; i++) {
            crc ^= data[offset + i] & 0xff;
            for (int j = 0; j < 8; j++) {
                if ((crc & 1) != 0) {
                    crc = (crc >>> 1) ^ 0xA001;
                } else {
                    crc >>>= 1;
                }
                crc &= 0xFFFF;
            }
        }
        return crc & 0xFFFF;
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

    private static void wr16(byte[] data, int offset, int value) {
        data[offset] = (byte) (value & 0xff);
        data[offset + 1] = (byte) ((value >>> 8) & 0xff);
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

    interface OtaListener {
        void onOtaProgress(int sentChunks, int totalChunks, int scheduleBytes);

        void onOtaWaitingForResult();

        void onOtaCompleted();

        void onOtaFailed(String reason);
    }

    private static final class OtaSession {
        final byte[] firmware;
        final OtaListener listener;
        final int totalChunks;
        int nextIndex;
        int schedulePdu;
        int scheduleBytes;
        boolean endSent;

        OtaSession(byte[] firmware, OtaListener listener) {
            this.firmware = firmware;
            this.listener = listener;
            this.totalChunks = (firmware.length + OTA_PDU_LEN - 1) / OTA_PDU_LEN;
        }
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
