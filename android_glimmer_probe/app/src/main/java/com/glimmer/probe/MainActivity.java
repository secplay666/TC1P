package com.glimmer.probe;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
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
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.TextView;

import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Queue;
import java.util.UUID;

public class MainActivity extends Activity {
    private static final int REQUEST_BLE_PERMISSIONS = 1001;

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

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final SimpleDateFormat timeFormat = new SimpleDateFormat("HH:mm:ss", Locale.US);
    private final HostProtocol.MessageAssembler assembler = new HostProtocol.MessageAssembler();
    private final List<ScanItem> scanItems = new ArrayList<>();
    private final Map<String, ScanItem> scanMap = new HashMap<>();
    private final Queue<byte[]> txQueue = new ArrayDeque<>();
    private final Queue<BluetoothGattCharacteristic> notifyQueue = new ArrayDeque<>();

    private TextView statusView;
    private TextView logView;
    private ScrollView logScroll;
    private ListView deviceList;
    private ArrayAdapter<String> deviceAdapter;
    private EditText chatInput;
    private EditText shellInput;

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothLeScanner scanner;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic cmdChar;
    private BluetoothGattCharacteristic rspChar;
    private BluetoothGattCharacteristic logChar;
    private BluetoothGattCharacteristic evtChar;

    private boolean scanning;
    private boolean debugReady;
    private boolean writeInProgress;
    private int seq = 1;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        BluetoothManager manager = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        bluetoothAdapter = manager == null ? null : manager.getAdapter();
        buildUi();
        appendLog("微光 Glimmer 验证工具启动");
        appendLog("Host frame: max packet=20, chunk=9, message max=192");
        if (bluetoothAdapter == null) {
            setStatus("本机不支持蓝牙");
        } else if (!bluetoothAdapter.isEnabled()) {
            setStatus("蓝牙未开启");
        } else {
            setStatus("就绪，先授权再扫描");
        }
    }

    @Override
    protected void onDestroy() {
        stopScan();
        disconnectGatt();
        super.onDestroy();
    }

    private void buildUi() {
        int pad = dp(12);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("微光 Glimmer 验证工具");
        title.setTextSize(22);
        title.setGravity(Gravity.CENTER_VERTICAL);
        title.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(36)));
        root.addView(title);

        statusView = new TextView(this);
        statusView.setTextSize(14);
        statusView.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(28)));
        root.addView(statusView);

        LinearLayout scanRow = row();
        scanRow.addView(button("授权", v -> ensurePermissions(true)));
        scanRow.addView(button("扫描", v -> startScan()));
        scanRow.addView(button("断开", v -> disconnectGatt()));
        root.addView(scanRow);

        deviceAdapter = new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, new ArrayList<>());
        deviceList = new ListView(this);
        deviceList.setAdapter(deviceAdapter);
        deviceList.setOnItemClickListener(this::onDeviceClicked);
        root.addView(deviceList, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(168)));

        LinearLayout cmdRow = row();
        cmdRow.addView(button("设备信息", v -> sendEmptyCommand(HostProtocol.CMD_GET_DEVICE_INFO)));
        cmdRow.addView(button("邻近表", v -> sendEmptyCommand(HostProtocol.CMD_GET_PEER_TABLE)));
        cmdRow.addView(button("关闭日志", v -> sendPackets(HostProtocol.makeLogEnable(nextSeq(), false))));
        root.addView(cmdRow);

        LinearLayout chatRow = new LinearLayout(this);
        chatRow.setOrientation(LinearLayout.HORIZONTAL);
        chatInput = new EditText(this);
        chatInput.setSingleLine(true);
        chatInput.setHint("输入 P2P 聊天内容，最多 192 bytes");
        chatInput.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
        chatRow.addView(chatInput, new LinearLayout.LayoutParams(0, dp(48), 1));
        chatRow.addView(button("发送", v -> sendChat()));
        root.addView(chatRow);

        LinearLayout shellRow = new LinearLayout(this);
        shellRow.setOrientation(LinearLayout.HORIZONTAL);
        shellInput = new EditText(this);
        shellInput.setSingleLine(true);
        shellInput.setHint("Shell 命令，例如 peers / hoststat");
        shellInput.setText("peers");
        shellRow.addView(shellInput, new LinearLayout.LayoutParams(0, dp(48), 1));
        shellRow.addView(button("执行", v -> sendShell()));
        root.addView(shellRow);

        logView = new TextView(this);
        logView.setTextSize(12);
        logView.setTextIsSelectable(true);
        logScroll = new ScrollView(this);
        logScroll.addView(logView, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(logScroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0,
                1));

        setContentView(root);
    }

    private LinearLayout row() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.HORIZONTAL);
        layout.setGravity(Gravity.CENTER_VERTICAL);
        layout.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(44)));
        return layout;
    }

    private Button button(String text, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(text);
        button.setAllCaps(false);
        button.setOnClickListener(listener);
        button.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
        return button;
    }

    private void onDeviceClicked(AdapterView<?> parent, View view, int position, long id) {
        if (position < 0 || position >= scanItems.size()) {
            return;
        }
        connect(scanItems.get(position).address);
    }

    private boolean ensurePermissions(boolean requestIfMissing) {
        List<String> missing = new ArrayList<>();
        for (String permission : requiredPermissions()) {
            if (checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
                missing.add(permission);
            }
        }
        if (!missing.isEmpty()) {
            if (requestIfMissing) {
                requestPermissions(missing.toArray(new String[0]), REQUEST_BLE_PERMISSIONS);
            }
            setStatus("需要蓝牙权限");
            return false;
        }
        return true;
    }

    private List<String> requiredPermissions() {
        List<String> permissions = new ArrayList<>();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions.add(Manifest.permission.BLUETOOTH_SCAN);
            permissions.add(Manifest.permission.BLUETOOTH_CONNECT);
            permissions.add(Manifest.permission.ACCESS_FINE_LOCATION);
        } else {
            permissions.add(Manifest.permission.ACCESS_FINE_LOCATION);
        }
        return permissions;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQUEST_BLE_PERMISSIONS) {
            return;
        }
        if (ensurePermissions(false)) {
            setStatus("权限已就绪，可以扫描");
            appendLog("权限已授予");
        } else {
            appendLog("权限不足，无法扫描/连接 BLE");
        }
    }

    @SuppressLint("MissingPermission")
    private void startScan() {
        if (!ensurePermissions(true)) {
            return;
        }
        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled()) {
            setStatus("蓝牙未开启");
            return;
        }
        stopScan();
        scanner = bluetoothAdapter.getBluetoothLeScanner();
        if (scanner == null) {
            setStatus("无法获取 BLE Scanner");
            return;
        }

        scanItems.clear();
        scanMap.clear();
        deviceAdapter.clear();
        scanning = true;
        setStatus("扫描微光设备...");
        appendLog("SCAN start");

        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build();
        scanner.startScan(null, settings, scanCallback);
        handler.postDelayed(this::stopScan, 7000);
    }

    @SuppressLint("MissingPermission")
    private void stopScan() {
        if (!scanning) {
            return;
        }
        scanning = false;
        if (scanner != null && ensurePermissions(false)) {
            try {
                scanner.stopScan(scanCallback);
            } catch (RuntimeException ignored) {
            }
        }
        setStatus("扫描结束，发现 " + scanItems.size() + " 个候选设备");
        appendLog("SCAN stop, candidates=" + scanItems.size());
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
                setStatus("扫描失败: " + errorCode);
                appendLog("SCAN failed code=" + errorCode);
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
        if ((name == null || name.isEmpty()) && ensurePermissions(false)) {
            name = device.getName();
        }
        if (name == null) {
            name = "";
        }

        boolean serviceMatch = hasDebugService(record);
        boolean nameMatch = name.toUpperCase(Locale.US).contains("PENDANT")
                || name.toUpperCase(Locale.US).contains("GLIMMER")
                || name.contains("微光");

        ScanItem item = scanMap.get(address);
        if (item == null) {
            item = new ScanItem(address);
            scanMap.put(address, item);
            scanItems.add(item);
        }
        item.name = name.isEmpty() ? "(unknown)" : name;
        item.rssi = result.getRssi();
        item.serviceMatch = serviceMatch;
        item.nameMatch = nameMatch;
        refreshDeviceList();
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

    private void refreshDeviceList() {
        deviceAdapter.clear();
        for (ScanItem item : scanItems) {
            String mark = "";
            if (item.serviceMatch || item.nameMatch) {
                mark = "  ★";
            }
            deviceAdapter.add(item.name + "  RSSI " + item.rssi + "  " + item.address + mark);
        }
        deviceAdapter.notifyDataSetChanged();
    }

    @SuppressLint("MissingPermission")
    private void connect(String address) {
        if (!ensurePermissions(true)) {
            return;
        }
        if (bluetoothAdapter == null) {
            setStatus("蓝牙不可用");
            return;
        }
        stopScan();
        disconnectGatt();
        try {
            BluetoothDevice device = bluetoothAdapter.getRemoteDevice(address);
            setStatus("连接 " + address + " ...");
            appendLog("CONNECT " + address);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                gatt = device.connectGatt(this, false, gattCallback, BluetoothDevice.TRANSPORT_LE);
            } else {
                gatt = device.connectGatt(this, false, gattCallback);
            }
        } catch (IllegalArgumentException e) {
            appendLog("CONNECT error: " + e.getMessage());
        }
    }

    @SuppressLint("MissingPermission")
    private void disconnectGatt() {
        debugReady = false;
        writeInProgress = false;
        txQueue.clear();
        notifyQueue.clear();
        assembler.reset();
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
            setStatus("已断开");
            appendLog("DISCONNECT");
        }
    }

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            handler.post(() -> {
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    setStatus("已连接，发现 GATT 服务...");
                    appendLog("GATT connected status=" + status);
                    gatt.discoverServices();
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    appendLog("GATT disconnected status=" + status);
                    setStatus("连接断开");
                    debugReady = false;
                    writeInProgress = false;
                    txQueue.clear();
                    notifyQueue.clear();
                    assembler.reset();
                    if (MainActivity.this.gatt == gatt) {
                        try {
                            gatt.close();
                        } catch (RuntimeException ignored) {
                        }
                        MainActivity.this.gatt = null;
                    }
                }
            });
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            handler.post(() -> {
                appendLog("GATT services discovered status=" + status);
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    setStatus("GATT 服务发现失败");
                    return;
                }
                findDebugCharacteristics(gatt.getServices());
                if (cmdChar == null || rspChar == null || logChar == null || evtChar == null) {
                    setStatus("未找到完整调试 GATT 特征");
                    appendLog("chars: cmd=" + (cmdChar != null) + " rsp=" + (rspChar != null)
                            + " log=" + (logChar != null) + " evt=" + (evtChar != null));
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
            handler.post(() -> {
                appendLog("CCCD write status=" + status + " uuid=" + descriptor.getCharacteristic().getUuid());
                subscribeNext();
            });
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, int status) {
            handler.post(() -> {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    appendLog("TX write status=" + status);
                }
                handler.postDelayed(MainActivity.this::writeNextPacket, 20);
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
            debugReady = true;
            setStatus("调试通道已就绪");
            appendLog("DEBUG ready");
            return;
        }

        gatt.setCharacteristicNotification(characteristic, true);
        BluetoothGattDescriptor descriptor = characteristic.getDescriptor(CCCD_UUID);
        if (descriptor == null) {
            appendLog("Missing CCCD for " + characteristic.getUuid());
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
            appendLog("CCCD write start failed: " + characteristic.getUuid());
            subscribeNext();
        }
    }

    private void sendEmptyCommand(int cmd) {
        sendPackets(HostProtocol.makeEmptyCommand(nextSeq(), cmd));
    }

    private void sendChat() {
        String text = chatInput.getText().toString();
        if (text.trim().isEmpty()) {
            appendLog("CHAT empty");
            return;
        }
        try {
            sendPackets(HostProtocol.makeP2pChatSend(nextSeq(), text));
            appendLog("TX CHAT " + text.getBytes(StandardCharsets.UTF_8).length + " bytes: " + text);
        } catch (IllegalArgumentException e) {
            appendLog("CHAT error: " + e.getMessage());
        }
    }

    private void sendShell() {
        String line = shellInput.getText().toString().trim();
        if (line.isEmpty()) {
            appendLog("SHELL empty");
            return;
        }
        sendPackets(HostProtocol.makeShellExec(nextSeq(), line));
        appendLog("TX SHELL " + line);
    }

    private void sendPackets(List<byte[]> packets) {
        if (!debugReady || gatt == null || cmdChar == null) {
            appendLog("TX blocked: debug channel is not ready");
            return;
        }
        txQueue.addAll(packets);
        appendLog("TX enqueue packets=" + packets.size());
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
            writeInProgress = false;
            return;
        }
        writeInProgress = true;

        boolean ok;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            ok = gatt.writeCharacteristic(cmdChar, packet, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
                    == BluetoothStatusCodes.SUCCESS;
        } else {
            cmdChar.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
            cmdChar.setValue(packet);
            ok = gatt.writeCharacteristic(cmdChar);
        }

        if (!ok) {
            appendLog("TX write start failed: " + HostProtocol.hex(packet));
            handler.postDelayed(this::writeNextPacket, 50);
        }
    }

    private void onNotify(byte[] data) {
        try {
            HostProtocol.HostFrame frame = HostProtocol.decodePacket(data);
            HostProtocol.HostMessage message = assembler.push(frame);
            if (message != null) {
                appendLog("RX " + HostProtocol.formatMessage(message));
            }
        } catch (Exception e) {
            appendLog("RX decode error: " + e.getMessage() + " raw=" + HostProtocol.hex(data));
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

    private void setStatus(String status) {
        statusView.setText(status);
    }

    private void appendLog(String line) {
        String prefix = timeFormat.format(new Date());
        logView.append(prefix + "  " + line + "\n");
        logScroll.post(() -> logScroll.fullScroll(View.FOCUS_DOWN));
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private static final class ScanItem {
        final String address;
        String name = "";
        int rssi = -127;
        boolean serviceMatch;
        boolean nameMatch;

        ScanItem(String address) {
            this.address = address;
        }
    }
}
