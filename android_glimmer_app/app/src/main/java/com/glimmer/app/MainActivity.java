package com.glimmer.app;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;

import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity implements GlimmerBleClient.Listener {
    private static final int REQUEST_APP_PERMISSIONS = 1201;

    private final List<Button> navButtons = new ArrayList<>();
    private final List<GlimmerBleClient.ScanDevice> scanDevices = new ArrayList<>();
    private final List<HostProtocol.PeerInfo> peers = new ArrayList<>();
    private final List<HostProtocol.PeerProfileInfo> peerProfiles = new ArrayList<>();
    private final List<String> eventLines = new ArrayList<>();

    private GlimmerBleClient bleClient;
    private LinearLayout root;
    private LinearLayout content;
    private SwipeRefreshLayout refreshLayout;
    private TextView titleView;
    private TextView statusView;
    private int selectedTab;

    private String connectionStatus = "我的 Glimmer 未连接";
    private HostProtocol.DeviceInfo deviceInfo;
    private HostProtocol.ProfileSummary myProfile;
    private EditText nicknameEdit;
    private EditText signatureEdit;
    private boolean debugReady;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        bleClient = new GlimmerBleClient(this, this);
        buildUi();
        showTab(0);
    }

    @Override
    protected void onDestroy() {
        if (bleClient != null) {
            bleClient.stopScan();
            bleClient.disconnect();
        }
        super.onDestroy();
    }

    private void buildUi() {
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(color(0xF7F8F5));
        root.setPadding(dp(18), dp(18), dp(18), dp(12));

        titleView = label("附近的微光", 28, color(0x1F2420), true);
        root.addView(titleView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        statusView = label(connectionStatus, 14, color(0x667068), false);
        root.addView(statusView, marginParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                0, dp(6), 0, dp(14)));

        content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);

        ScrollView scrollView = new ScrollView(this);
        scrollView.setFillViewport(false);
        scrollView.addView(content, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        refreshLayout = new SwipeRefreshLayout(this);
        refreshLayout.setColorSchemeColors(color(0x2F8F7B), color(0xE5B84B), color(0xF06D5E));
        refreshLayout.setOnRefreshListener(this::handlePullRefresh);
        refreshLayout.addView(scrollView, new SwipeRefreshLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        root.addView(refreshLayout, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0,
                1));

        root.addView(buildNav(), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(58)));

        setContentView(root);
    }

    private LinearLayout buildNav() {
        LinearLayout nav = new LinearLayout(this);
        nav.setOrientation(LinearLayout.HORIZONTAL);
        nav.setGravity(Gravity.CENTER);
        nav.setBackground(rounded(0xFFFFFF, 8));
        nav.setPadding(dp(4), dp(4), dp(4), dp(4));

        String[] labels = {"附近", "消息", "我的", "安全"};
        for (int i = 0; i < labels.length; i++) {
            final int index = i;
            Button button = new Button(this);
            button.setAllCaps(false);
            button.setText(labels[i]);
            button.setTextSize(14);
            button.setOnClickListener(v -> showTab(index));
            navButtons.add(button);
            nav.addView(button, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
        }

        return nav;
    }

    private void showTab(int index) {
        selectedTab = index;
        String[] titles = {"附近的微光", "消息", "我的", "安全"};
        titleView.setText(titles[index]);
        statusView.setText(topStatusText(index));

        for (int i = 0; i < navButtons.size(); i++) {
            Button button = navButtons.get(i);
            boolean selected = i == selectedTab;
            button.setTextColor(selected ? Color.WHITE : color(0x667068));
            button.setBackground(rounded(selected ? 0x2F8F7B : 0xFFFFFF, 8));
        }

        content.removeAllViews();
        if (index == 0) {
            renderNearbyPage();
        } else if (index == 1) {
            renderMessagesPage();
        } else if (index == 2) {
            renderMinePage();
        } else {
            renderSafetyPage();
        }
    }

    private void renderNearbyPage() {
        LinearLayout intro = card();
        intro.addView(label("Glimmer", 12, color(0x1F6F60), true));
        intro.addView(label("附近有人发来一束微光", 24, color(0x1F2420), true),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(8), 0, 0));
        intro.addView(label("微光通过你的 Glimmer 终端发现附近的人。我们不展示精确位置，也不保存长期离线消息。", 15, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(10), 0, 0));
        intro.addView(label("下拉页面即可重新寻找附近终端。", 13, color(0x2F8F7B), true),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(14), 0, 0));
        content.addView(intro, cardParams());

        content.addView(scanPanel(), cardParams());
        content.addView(sectionTitle("刚刚擦肩"), marginParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                0, dp(4), 0, dp(10)));
        if (!debugReady) {
            content.addView(emptyStateCard("先连接你的 Glimmer", "连接自己的终端后，这里会显示附近可交流的微光。"), cardParams());
        } else if (peers.isEmpty() && peerProfiles.isEmpty()) {
            content.addView(emptyStateCard("附近暂时没有微光", "可以下拉刷新，或者稍后再看看。"), cardParams());
        } else if (!peerProfiles.isEmpty()) {
            for (HostProtocol.PeerProfileInfo profile : peerProfiles) {
                content.addView(peerProfileCard(profile), cardParams());
            }
        } else {
            for (HostProtocol.PeerInfo peer : peers) {
                content.addView(peerCard(peer), cardParams());
            }
        }

        content.addView(sectionTitle("我的 Glimmer 终端"), marginParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                0, dp(6), 0, dp(10)));
        content.addView(terminalPanel(), cardParams());
    }

    private LinearLayout scanPanel() {
        LinearLayout panel = card();
        panel.setPadding(dp(16), dp(16), dp(16), dp(16));

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);

        GlimmerFieldView fieldView = new GlimmerFieldView(this);
        fieldView.setPeerCount(peers.size());
        fieldView.setConnected(debugReady);
        row.addView(fieldView, new LinearLayout.LayoutParams(dp(126), dp(126)));

        LinearLayout copy = new LinearLayout(this);
        copy.setOrientation(LinearLayout.VERTICAL);
        copy.setPadding(dp(14), 0, 0, 0);
        copy.addView(label(nearbyTitle(), 19, color(0x1F2420), true));
        copy.addView(label(nearbyCopy(), 13, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(6), 0, dp(10)));
        copy.addView(secondaryButton(bleClient.isScanning() ? "正在寻找" : "重新寻找", v -> startScanFlow()),
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(42)));
        row.addView(copy, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        panel.addView(row, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        return panel;
    }

    private LinearLayout terminalPanel() {
        LinearLayout panel = card();
        panel.addView(label(terminalTitle(), 18, color(0x1F2420), true));
        panel.addView(label(terminalCopy(), 14, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(8), 0, dp(12)));

        if (!debugReady) {
            if (scanDevices.isEmpty()) {
                panel.addView(label(scanSummary(), 13, color(0x9AA39C), false));
            } else {
                for (GlimmerBleClient.ScanDevice device : scanDevices) {
                    panel.addView(terminalDeviceRow(device), marginParams(
                            ViewGroup.LayoutParams.MATCH_PARENT,
                            ViewGroup.LayoutParams.WRAP_CONTENT,
                            0, dp(8), 0, 0));
                }
            }
        } else {
            panel.addView(infoPill(deviceInfo == null ? "正在读取设备信息" : "固件 " + deviceInfo.firmwareVersion() + " · 识别码 " + deviceInfo.shortCode()));
        }

        return panel;
    }

    private void renderMessagesPage() {
        LinearLayout card = card();
        card.addView(label("暂无新的对话", 20, color(0x1F2420), true));
        card.addView(label("收到或发出的微光消息会出现在这里。Glimmer 终端会加密转发，对方暂时不可达时会明确提示。", 15, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(10), 0, 0));
        content.addView(card, cardParams());

        renderRecentEvents("最近状态");
    }

    private void renderMinePage() {
        LinearLayout profile = card();
        profile.addView(label("附近预览卡片", 20, color(0x1F2420), true));
        profile.addView(label(myProfile == null ? "还没有从终端同步资料卡" : myProfile.displayName(), 18, color(0x2F8F7B), true),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(10), 0, 0));
        profile.addView(label(myProfile == null ? "连接 Glimmer 后可以写入昵称和一句话。" : myProfile.signatureText(), 14, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(6), 0, dp(12)));
        nicknameEdit = editText(myProfile == null ? "微光旅人" : myProfile.displayName());
        signatureEdit = editText(myProfile == null ? "今晚想听温柔故事" : myProfile.signatureText());
        profile.addView(nicknameEdit, marginParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(48), 0, 0, 0, dp(8)));
        profile.addView(signatureEdit, marginParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(48), 0, 0, 0, dp(12)));
        profile.addView(primaryButton("保存到 Glimmer", v -> saveProfileCard()),
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(46)));
        content.addView(profile, cardParams());

        LinearLayout device = card();
        device.addView(label("我的 Glimmer", 20, color(0x1F2420), true));
        device.addView(label(connectionStatus, 16, debugReady ? color(0x2F8F7B) : color(0x667068), true),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(10), 0, 0));
        device.addView(label(deviceInfoSummary(), 14, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(10), 0, dp(12)));
        device.addView(primaryButton(debugReady ? "同步设备状态" : "寻找我的 Glimmer", v -> {
            if (debugReady) {
                bleClient.requestFullSync();
                setLocalStatus("正在同步 Glimmer 状态");
            } else {
                startScanFlow();
            }
        }), new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(46)));
        content.addView(device, cardParams());

        LinearLayout nearby = card();
        nearby.addView(label("附近状态", 20, color(0x1F2420), true));
        nearby.addView(label(peerSummary(), 14, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(10), 0, 0));
        content.addView(nearby, cardParams());

        renderRecentEvents("最近事件");
    }

    private void renderSafetyPage() {
        LinearLayout card = card();
        card.addView(label("随时保留退出和屏蔽", 20, color(0x1F2420), true));
        card.addView(label("隐身模式、屏蔽列表和陌生问候开关会在后续接入资料协议后实现。", 15, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(10), 0, 0));
        content.addView(card, cardParams());
        addActionRow("隐身模式", v -> setLocalStatus("隐身设置待接入"), "屏蔽列表", v -> setLocalStatus("屏蔽列表待接入"));
    }

    private void renderRecentEvents(String title) {
        LinearLayout events = card();
        events.addView(label(title, 20, color(0x1F2420), true));
        if (eventLines.isEmpty()) {
            events.addView(label("还没有新的状态。", 13, color(0x9AA39C), false),
                    marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(10), 0, 0));
        } else {
            for (String line : eventLines) {
                events.addView(label(line, 13, color(0x667068), false),
                        marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(8), 0, 0));
            }
        }
        content.addView(events, cardParams());
    }

    private LinearLayout peerCard(HostProtocol.PeerInfo peer) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(14), dp(13), dp(14), dp(13));
        row.setBackground(rounded(0xFFFFFF, 8));

        row.addView(avatar("微", 0x2F8F7B), new LinearLayout.LayoutParams(dp(44), dp(44)));

        LinearLayout copy = new LinearLayout(this);
        copy.setOrientation(LinearLayout.VERTICAL);
        copy.setPadding(dp(12), 0, dp(8), 0);
        copy.addView(label("一束微光", 16, color(0x1F2420), true));
        copy.addView(label(peer.proximityText() + " · " + peer.signalText(), 13, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(4), 0, 0));
        row.addView(copy, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        row.addView(infoPill(peer.proximityText()), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        return row;
    }

    private LinearLayout peerProfileCard(HostProtocol.PeerProfileInfo profile) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(14), dp(13), dp(14), dp(13));
        row.setBackground(rounded(0xFFFFFF, 8));

        row.addView(avatar(profile.displayName().substring(0, 1), 0x2F8F7B),
                new LinearLayout.LayoutParams(dp(44), dp(44)));

        LinearLayout copy = new LinearLayout(this);
        copy.setOrientation(LinearLayout.VERTICAL);
        copy.setPadding(dp(12), 0, dp(8), 0);
        copy.addView(label(profile.displayName(), 16, color(0x1F2420), true));
        copy.addView(label(profile.signatureText(), 13, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(4), 0, 0));
        copy.addView(label(profile.proximityText() + " · " + profile.signalText(), 12, color(0x9AA39C), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(4), 0, 0));
        row.addView(copy, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        row.addView(infoPill(profile.proximityText()), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        return row;
    }

    private LinearLayout terminalDeviceRow(GlimmerBleClient.ScanDevice device) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(12), dp(10), dp(12), dp(10));
        row.setBackground(rounded(0xF1F6F2, 8));
        row.setOnClickListener(v -> bleClient.connect(device));

        row.addView(avatar("G", 0x1F6F60), new LinearLayout.LayoutParams(dp(38), dp(38)));

        LinearLayout copy = new LinearLayout(this);
        copy.setOrientation(LinearLayout.VERTICAL);
        copy.setPadding(dp(12), 0, 0, 0);
        copy.addView(label("Glimmer 终端", 15, color(0x1F2420), true));
        copy.addView(label(signalText(device.rssi) + " · 点击连接", 12, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(3), 0, 0));
        row.addView(copy, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        return row;
    }

    private LinearLayout emptyStateCard(String title, String copyText) {
        LinearLayout state = card();
        state.addView(label(title, 18, color(0x1F2420), true));
        state.addView(label(copyText, 14, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(8), 0, 0));
        return state;
    }

    private TextView sectionTitle(String text) {
        TextView view = label(text, 17, color(0x1F2420), true);
        view.setGravity(Gravity.CENTER_VERTICAL);
        return view;
    }

    private TextView avatar(String text, int backgroundColor) {
        TextView avatar = label(text, 16, Color.WHITE, true);
        avatar.setGravity(Gravity.CENTER);
        avatar.setBackground(rounded(backgroundColor, 8));
        return avatar;
    }

    private TextView infoPill(String text) {
        TextView pill = label(text, 12, color(0x1F6F60), true);
        pill.setGravity(Gravity.CENTER);
        pill.setPadding(dp(10), dp(6), dp(10), dp(6));
        pill.setBackground(rounded(0xE2F1EB, 8));
        return pill;
    }

    private void addActionRow(String leftText, View.OnClickListener leftClick, String rightText, View.OnClickListener rightClick) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.addView(secondaryButton(leftText, leftClick), new LinearLayout.LayoutParams(0, dp(48), 1));
        row.addView(spacer(dp(10), 1));
        row.addView(secondaryButton(rightText, rightClick), new LinearLayout.LayoutParams(0, dp(48), 1));
        content.addView(row, marginParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(54), 0, dp(2), 0, dp(18)));
    }

    private void handlePullRefresh() {
        if (selectedTab == 0 || !debugReady) {
            startScanFlow();
            return;
        }

        setLocalStatus("正在同步 Glimmer 状态");
        bleClient.requestFullSync();
        endRefreshingSoon();
    }

    private void startScanFlow() {
        if (!ensurePermissions(true)) {
            endRefreshing();
            return;
        }
        if (refreshLayout != null) {
            refreshLayout.setRefreshing(true);
        }
        bleClient.startScan();
        rerenderCurrentTab();
    }

    private void saveProfileCard() {
        if (!debugReady) {
            setLocalStatus("请先连接我的 Glimmer");
            return;
        }
        String nickname = nicknameEdit == null ? "" : nicknameEdit.getText().toString();
        String signature = signatureEdit == null ? "" : signatureEdit.getText().toString();
        bleClient.setProfileSummary(nickname, signature);
        setLocalStatus("正在保存附近预览卡片");
    }

    private boolean ensurePermissions(boolean requestIfMissing) {
        List<String> missing = new ArrayList<>();
        for (String permission : GlimmerBleClient.runtimePermissions()) {
            if (checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
                missing.add(permission);
            }
        }

        if (!missing.isEmpty()) {
            if (requestIfMissing) {
                requestPermissions(missing.toArray(new String[0]), REQUEST_APP_PERMISSIONS);
            }
            setLocalStatus("需要允许蓝牙和位置权限");
            return false;
        }
        return true;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_APP_PERMISSIONS) {
            if (ensurePermissions(false)) {
                setLocalStatus("权限已就绪，正在寻找你的 Glimmer");
                startScanFlow();
            } else {
                setLocalStatus("权限不足，无法连接 Glimmer");
                endRefreshing();
            }
            rerenderCurrentTab();
        }
    }

    @Override
    public void onBleStatus(String status) {
        connectionStatus = productBleStatus(status);
        statusView.setText(connectionStatus);
        pushEvent(connectionStatus);
        if (!bleClient.isScanning()) {
            endRefreshing();
        }
        rerenderCurrentTab();
    }

    @Override
    public void onScanDevicesChanged(List<GlimmerBleClient.ScanDevice> devices) {
        scanDevices.clear();
        scanDevices.addAll(devices);
        if (!bleClient.isScanning()) {
            endRefreshing();
        }
        rerenderCurrentTab();
    }

    @Override
    public void onDebugReady(boolean ready) {
        debugReady = ready;
        if (!ready) {
            deviceInfo = null;
            myProfile = null;
            peers.clear();
            peerProfiles.clear();
        }
        rerenderCurrentTab();
    }

    @Override
    public void onHostMessage(HostProtocol.HostMessage message, String formatted) {
        try {
            if (message.frameType == HostProtocol.TYPE_RSP && message.cmd == HostProtocol.CMD_GET_DEVICE_INFO && message.status == 0) {
                deviceInfo = HostProtocol.parseDeviceInfo(message.payload);
                pushEvent("我的 Glimmer 信息已更新");
            } else if (message.frameType == HostProtocol.TYPE_RSP && message.cmd == HostProtocol.CMD_GET_PEER_TABLE && message.status == 0) {
                peers.clear();
                peers.addAll(HostProtocol.parsePeerTable(message.payload));
                pushEvent(peers.isEmpty() ? "附近列表已刷新，暂时没有微光" : "发现 " + peers.size() + " 束附近微光");
            } else if (message.frameType == HostProtocol.TYPE_RSP && message.cmd == HostProtocol.CMD_GET_PROFILE_SUMMARY && message.status == 0) {
                myProfile = HostProtocol.parseProfileSummary(message.payload);
                setLocalStatus("附近预览卡片已同步");
                pushEvent("附近预览卡片已同步");
            } else if (message.frameType == HostProtocol.TYPE_RSP && message.cmd == HostProtocol.CMD_GET_PEER_PROFILES && message.status == 0) {
                peerProfiles.clear();
                peerProfiles.addAll(HostProtocol.parsePeerProfiles(message.payload));
                if (!peerProfiles.isEmpty()) {
                    pushEvent("读到 " + peerProfiles.size() + " 张附近资料卡");
                }
            } else if (message.frameType == HostProtocol.TYPE_EVENT && message.cmd == HostProtocol.EVENT_P2P_CHAT) {
                HostProtocol.ChatEvent event = HostProtocol.parseChatEvent(message.payload);
                pushEvent(event.isDropped() ? "一条微光已过期" : "收到一条来自附近的微光");
            } else if (message.frameType == HostProtocol.TYPE_EVENT && message.cmd == HostProtocol.EVENT_P2P_CHAT_TX_RESULT) {
                HostProtocol.ChatTxResult result = HostProtocol.parseChatTxResult(message.payload);
                pushEvent(result.isSuccess() ? "消息已送达" : "对方暂时不可达，消息未送达");
            } else if (message.frameType == HostProtocol.TYPE_RSP && message.status != 0) {
                pushEvent(productResponseError(message));
            }
        } catch (RuntimeException e) {
            pushEvent("收到一条暂时无法显示的终端信息");
        }
        endRefreshing();
        rerenderCurrentTab();
    }

    private void setLocalStatus(String status) {
        connectionStatus = status;
        statusView.setText(status);
    }

    private void pushEvent(String line) {
        if (line == null || line.trim().isEmpty()) {
            return;
        }
        if (!eventLines.isEmpty() && eventLines.get(0).equals(line)) {
            return;
        }
        eventLines.add(0, line);
        while (eventLines.size() > 6) {
            eventLines.remove(eventLines.size() - 1);
        }
    }

    private void rerenderCurrentTab() {
        if (content != null) {
            showTab(selectedTab);
        }
    }

    private String topStatusText(int index) {
        if (index == 0) {
            return debugReady ? peerSummary() : "下拉寻找附近的 Glimmer 终端";
        }
        if (index == 2) {
            return connectionStatus;
        }
        return "微光范围内的消息状态";
    }

    private String nearbyTitle() {
        if (!debugReady) {
            return "先连接你的 Glimmer";
        }
        if (peers.isEmpty() && peerProfiles.isEmpty()) {
            return "附近暂时没有微光";
        }
        int count = Math.max(peers.size(), peerProfiles.size());
        return count + " 束微光在附近";
    }

    private String nearbyCopy() {
        if (!debugReady) {
            return "找到并连接自己的终端后，微光会开始同步附近状态。";
        }
        if (peers.isEmpty()) {
            return "不显示精确距离，只显示可交流的强弱。";
        }
        return "不显示精确距离，只显示可交流的强弱。";
    }

    private String terminalTitle() {
        if (debugReady) {
            return "我的 Glimmer 已连接";
        }
        if (bleClient.isScanning()) {
            return "正在寻找你的 Glimmer";
        }
        return "连接你的 Glimmer";
    }

    private String terminalCopy() {
        if (debugReady) {
            return "终端已就绪，正在保持附近可见。";
        }
        return "请确认终端已开机。下拉页面或点击按钮都可以重新寻找。";
    }

    private String scanSummary() {
        if (bleClient.isScanning()) {
            return "正在寻找，已发现 " + scanDevices.size() + " 台 Glimmer 终端";
        }
        if (scanDevices.isEmpty()) {
            return "还没有发现可连接的 Glimmer 终端。";
        }
        return "发现 " + scanDevices.size() + " 台 Glimmer 终端，点击即可连接。";
    }

    private String peerSummary() {
        if (!debugReady) {
            return "连接终端后同步附近状态";
        }
        if (peers.isEmpty() && peerProfiles.isEmpty()) {
            return "附近暂时没有微光";
        }
        int count = Math.max(peers.size(), peerProfiles.size());
        return "附近有 " + count + " 束微光";
    }

    private String deviceInfoSummary() {
        if (deviceInfo == null) {
            return debugReady ? "正在读取固件和设备识别信息。" : "连接后会显示固件版本、识别码和终端状态。";
        }

        String privacy = deviceInfo.hasPrivacyKey() ? "隐私密钥已启用" : "隐私密钥未启用";
        return "固件 " + deviceInfo.firmwareVersion()
                + " · 硬件 " + deviceInfo.hardwareRevision
                + "\n识别码 " + deviceInfo.shortCode()
                + " · " + privacy;
    }

    private String productBleStatus(String raw) {
        if (raw == null) {
            return connectionStatus;
        }
        if (raw.startsWith("扫描结束")) {
            return scanDevices.isEmpty() ? "暂未发现 Glimmer 终端" : "发现 " + scanDevices.size() + " 台 Glimmer 终端";
        }
        if (raw.startsWith("扫描失败")) {
            return "扫描失败，请稍后重试";
        }
        if (raw.contains("正在寻找")) {
            return "正在寻找你的 Glimmer";
        }
        if (raw.contains("正在连接")) {
            return "正在连接我的 Glimmer";
        }
        if (raw.contains("已连接") || raw.contains("我的 Glimmer 已连接")) {
            return "我的 Glimmer 已连接";
        }
        if (raw.contains("已断开")) {
            return "我的 Glimmer 已断开";
        }
        if (raw.contains("蓝牙未开启")) {
            return "蓝牙未开启";
        }
        if (raw.contains("权限")) {
            return raw;
        }
        return raw.replace("GATT", "Glimmer");
    }

    private String productResponseError(HostProtocol.HostMessage message) {
        if (message.cmd == HostProtocol.CMD_P2P_CHAT_SEND) {
            if (message.status == 0x08) {
                return "附近没有可送达的微光";
            }
            if (message.status == 0x02) {
                return "附近微光不止一束，暂时需要先选择对象";
            }
            return "消息暂时没有送出";
        }
        return "终端状态同步失败";
    }

    private String signalText(int rssi) {
        if (rssi >= -55) {
            return "信号稳定";
        }
        if (rssi >= -72) {
            return "信号良好";
        }
        return "信号较弱";
    }

    private void endRefreshingSoon() {
        if (refreshLayout != null) {
            refreshLayout.postDelayed(this::endRefreshing, 700);
        }
    }

    private void endRefreshing() {
        if (refreshLayout != null && refreshLayout.isRefreshing()) {
            refreshLayout.setRefreshing(false);
        }
    }

    private LinearLayout card() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(rounded(0xFFFFFF, 8));
        card.setPadding(dp(18), dp(18), dp(18), dp(18));
        return card;
    }

    private LinearLayout.LayoutParams cardParams() {
        return marginParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                0, 0, 0, dp(14));
    }

    private TextView label(String text, int sp, int textColor, boolean bold) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(sp);
        view.setTextColor(textColor);
        view.setLineSpacing(dp(2), 1.0f);
        if (bold) {
            view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        }
        return view;
    }

    private Button primaryButton(String text, View.OnClickListener listener) {
        Button button = baseButton(text, listener);
        button.setTextColor(Color.WHITE);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setBackground(rounded(0x2F8F7B, 8));
        return button;
    }

    private Button secondaryButton(String text, View.OnClickListener listener) {
        Button button = baseButton(text, listener);
        button.setTextColor(color(0x1F6F60));
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setBackground(rounded(0xDCEFE9, 8));
        return button;
    }

    private Button baseButton(String text, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setAllCaps(false);
        button.setText(text);
        button.setTextSize(14);
        button.setOnClickListener(listener);
        return button;
    }

    private EditText editText(String text) {
        EditText edit = new EditText(this);
        edit.setSingleLine(true);
        edit.setText(text);
        edit.setTextSize(14);
        edit.setTextColor(color(0x1F2420));
        edit.setPadding(dp(12), 0, dp(12), 0);
        edit.setBackground(rounded(0xF1F6F2, 8));
        return edit;
    }

    private View spacer(int width, int height) {
        View view = new View(this);
        view.setLayoutParams(new LinearLayout.LayoutParams(width, height));
        return view;
    }

    private LinearLayout.LayoutParams marginParams(int width, int height, int left, int top, int right, int bottom) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(width, height);
        params.setMargins(left, top, right, bottom);
        return params;
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private int color(int rgb) {
        return Color.rgb((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
    }

    private GradientDrawable rounded(int rgb, int radiusDp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color(rgb));
        drawable.setCornerRadius(dp(radiusDp));
        return drawable;
    }

    private final class GlimmerFieldView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private int peerCount;
        private boolean connected;

        GlimmerFieldView(Activity activity) {
            super(activity);
            setBackground(rounded(0xF8FBF8, 8));
        }

        void setPeerCount(int peerCount) {
            this.peerCount = peerCount;
        }

        void setConnected(boolean connected) {
            this.connected = connected;
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float w = getWidth();
            float h = getHeight();
            float cx = w / 2f;
            float cy = h / 2f;

            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(dp(1));
            paint.setColor(color(0xB9DCD2));
            canvas.drawCircle(cx, cy, Math.min(w, h) * 0.36f, paint);
            paint.setColor(color(0xF0C8BC));
            canvas.drawCircle(cx, cy, Math.min(w, h) * 0.22f, paint);

            paint.setStyle(Paint.Style.FILL);
            drawDot(canvas, cx, cy, connected ? color(0x2F8F7B) : color(0x9AA39C), 9);
            if (peerCount > 0) {
                drawDot(canvas, w * 0.25f, h * 0.30f, color(0xE5B84B), 6);
            }
            if (peerCount > 1) {
                drawDot(canvas, w * 0.73f, h * 0.38f, color(0xF06D5E), 6);
            }
            if (peerCount > 2) {
                drawDot(canvas, w * 0.58f, h * 0.76f, color(0x5967D8), 6);
            }
        }

        private void drawDot(Canvas canvas, float x, float y, int dotColor, int radiusDp) {
            paint.setColor(Color.argb(36, Color.red(dotColor), Color.green(dotColor), Color.blue(dotColor)));
            canvas.drawCircle(x, y, dp(radiusDp + 8), paint);
            paint.setColor(dotColor);
            canvas.drawCircle(x, y, dp(radiusDp), paint);
        }
    }
}
