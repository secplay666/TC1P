package com.glimmer.app;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputFilter;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.Set;

public class MainActivity extends Activity implements GlimmerBleClient.Listener {
    private static final int REQUEST_APP_PERMISSIONS = 1201;
    private static final String PREFS_NAME = "glimmer_app";
    private static final String KEY_BOUND_ADDRESS = "bound_address";
    private static final String KEY_BOUND_NAME = "bound_name";
    private static final String KEY_BOUND_SHORT_ID = "bound_short_id";
    private static final String KEY_CHATTED_PEERS = "chatted_peer_ids";
    private static final long AUTO_SYNC_INTERVAL_MS = 10000;
    private static final long AUTO_RECONNECT_INTERVAL_MS = 15000;
    private static final String ACTION_DEBUG_CHAT = "com.glimmer.app.DEBUG_CHAT";

    private final List<Button> navButtons = new ArrayList<>();
    private final List<GlimmerBleClient.ScanDevice> scanDevices = new ArrayList<>();
    private final List<HostProtocol.PeerInfo> peers = new ArrayList<>();
    private final List<HostProtocol.PeerProfileInfo> peerProfiles = new ArrayList<>();
    private final List<HostProtocol.PeerProfileInfo> pendingPeerProfiles = new ArrayList<>();
    private final Map<Integer, Integer> peerProfilePageStarts = new HashMap<>();
    private final List<Conversation> conversations = new ArrayList<>();
    private final Map<String, String> chatDrafts = new HashMap<>();
    private final List<String> eventLines = new ArrayList<>();
    private final Set<String> chattedPeerIds = new HashSet<>();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable autoSyncRunnable = new Runnable() {
        @Override
        public void run() {
            if (debugReady) {
                bleClient.requestPeerTable();
                handler.postDelayed(() -> requestPeerProfilesPage(0), 300);
                handler.postDelayed(this, AUTO_SYNC_INTERVAL_MS);
            }
        }
    };
    private final Runnable autoReconnectRunnable = new Runnable() {
        @Override
        public void run() {
            if (!debugReady && hasBoundDevice()) {
                startScanFlow(false);
                handler.postDelayed(this, AUTO_RECONNECT_INTERVAL_MS);
            }
        }
    };
    private final BroadcastReceiver debugChatReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            handleDebugChatIntent(intent);
        }
    };

    private GlimmerBleClient bleClient;
    private SharedPreferences prefs;
    private LinearLayout root;
    private LinearLayout content;
    private FrameLayout bodyContainer;
    private SwipeRefreshLayout refreshLayout;
    private LinearLayout navBar;
    private TextView titleView;
    private TextView statusView;
    private int selectedTab;
    private int mineDetailMode;

    private String connectionStatus = "我的 Glimmer 未连接";
    private HostProtocol.DeviceInfo deviceInfo;
    private HostProtocol.ProfileSummary myProfile;
    private EditText nicknameEdit;
    private EditText signatureEdit;
    private EditText chatInput;
    private boolean debugReady;
    private String boundAddress;
    private String boundName;
    private String boundShortId;
    private String selectedConversationId;
    private String lastOutgoingConversationId;

    private static final class Conversation {
        final String id;
        long shortId;
        String title;
        String subtitle;
        String proximity;
        String lastText;
        String lastTime;
        long updatedAt;
        int unread;
        boolean familiar;
        final List<ChatMessage> messages = new ArrayList<>();

        Conversation(String id, long shortId) {
            this.id = id;
            this.shortId = shortId;
            this.title = "一束微光";
            this.subtitle = "";
            this.proximity = "附近";
            this.lastText = "";
            this.lastTime = "";
            this.updatedAt = 0;
        }
    }

    private static final class ChatMessage {
        final boolean incoming;
        final boolean system;
        final String text;
        String status;
        final String time;

        ChatMessage(boolean incoming, boolean system, String text, String status, String time) {
            this.incoming = incoming;
            this.system = system;
            this.text = text;
            this.status = status;
            this.time = time;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        loadLocalState();
        bleClient = new GlimmerBleClient(this, this);
        buildUi();
        registerDebugChatReceiver();
        showTab(0);
        maybeStartBoundReconnect();
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacks(autoSyncRunnable);
        handler.removeCallbacks(autoReconnectRunnable);
        if (bleClient != null) {
            bleClient.stopScan();
            bleClient.disconnect();
        }
        unregisterDebugChatReceiver();
        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        if (selectedTab == 1 && selectedConversationId != null) {
            preserveCurrentChatDraft();
            selectedConversationId = null;
            showTab(1);
            return;
        }
        if (selectedTab == 2 && mineDetailMode != 0) {
            mineDetailMode = 0;
            showTab(2);
            return;
        }
        super.onBackPressed();
    }

    private void loadLocalState() {
        boundAddress = prefs.getString(KEY_BOUND_ADDRESS, null);
        boundName = prefs.getString(KEY_BOUND_NAME, null);
        boundShortId = prefs.getString(KEY_BOUND_SHORT_ID, null);
        Set<String> savedPeers = prefs.getStringSet(KEY_CHATTED_PEERS, null);
        chattedPeerIds.clear();
        if (savedPeers != null) {
            chattedPeerIds.addAll(savedPeers);
        }
        connectionStatus = hasBoundDevice() ? "正在寻找已绑定的 Glimmer" : "我的 Glimmer 未绑定";
    }

    private boolean hasBoundDevice() {
        return boundAddress != null && !boundAddress.isEmpty();
    }

    private void saveBoundDevice(GlimmerBleClient.ScanDevice device) {
        boundAddress = device.address;
        boundName = device.displayName();
        prefs.edit()
                .putString(KEY_BOUND_ADDRESS, boundAddress)
                .putString(KEY_BOUND_NAME, boundName)
                .apply();
    }

    private void saveBoundDeviceInfo() {
        if (deviceInfo == null || !hasBoundDevice()) {
            return;
        }
        boundShortId = deviceInfo.shortCode();
        prefs.edit().putString(KEY_BOUND_SHORT_ID, boundShortId).apply();
    }

    private void unbindDevice() {
        bleClient.stopScan();
        bleClient.disconnect();
        handler.removeCallbacks(autoSyncRunnable);
        handler.removeCallbacks(autoReconnectRunnable);
        boundAddress = null;
        boundName = null;
        boundShortId = null;
        debugReady = false;
        deviceInfo = null;
        myProfile = null;
        peers.clear();
        peerProfiles.clear();
        scanDevices.clear();
        prefs.edit()
                .remove(KEY_BOUND_ADDRESS)
                .remove(KEY_BOUND_NAME)
                .remove(KEY_BOUND_SHORT_ID)
                .apply();
        setLocalStatus("已解除绑定，可以重新绑定新的 Glimmer");
        rerenderCurrentTab();
    }

    private void rememberChattedPeer(long shortId) {
        String value = HostProtocol.shortIdText(shortId);
        if (chattedPeerIds.add(value)) {
            prefs.edit().putStringSet(KEY_CHATTED_PEERS, new HashSet<>(chattedPeerIds)).apply();
        }
    }

    private boolean hasChattedWith(long shortId) {
        return chattedPeerIds.contains(HostProtocol.shortIdText(shortId));
    }

    private Conversation findConversation(String id) {
        if (id == null) {
            return null;
        }
        for (Conversation conversation : conversations) {
            if (conversation.id.equals(id)) {
                return conversation;
            }
        }
        return null;
    }

    private Conversation selectedConversation() {
        return findConversation(selectedConversationId);
    }

    private HostProtocol.PeerInfo findPeer(long shortId) {
        String id = HostProtocol.shortIdText(shortId);
        for (HostProtocol.PeerInfo peer : peers) {
            if (HostProtocol.shortIdText(peer.shortId).equals(id)) {
                return peer;
            }
        }
        return null;
    }

    private HostProtocol.PeerProfileInfo findPeerProfile(long shortId) {
        String id = HostProtocol.shortIdText(shortId);
        for (HostProtocol.PeerProfileInfo profile : peerProfiles) {
            if (HostProtocol.shortIdText(profile.shortId).equals(id)) {
                return profile;
            }
        }
        return null;
    }

    private Conversation ensureConversation(long shortId) {
        String id = HostProtocol.shortIdText(shortId);
        Conversation conversation = findConversation(id);
        if (conversation == null) {
            conversation = new Conversation(id, shortId);
            conversations.add(conversation);
        }
        conversation.shortId = shortId;
        conversation.familiar = hasChattedWith(shortId);
        return conversation;
    }

    private void updateConversationFromPeer(Conversation conversation, HostProtocol.PeerInfo peer) {
        if (conversation == null || peer == null) {
            return;
        }
        conversation.shortId = peer.shortId;
        conversation.proximity = peer.proximityText();
        conversation.subtitle = peer.proximityText() + " · " + peer.signalText();
        conversation.familiar = hasChattedWith(peer.shortId);
        if (conversation.title == null || conversation.title.isEmpty() || "一束微光".equals(conversation.title)) {
            conversation.title = conversation.familiar ? "再遇见的微光" : "一束微光";
        }
    }

    private void updateConversationFromProfile(Conversation conversation, HostProtocol.PeerProfileInfo profile) {
        if (conversation == null || profile == null) {
            return;
        }
        conversation.shortId = profile.shortId;
        conversation.title = profile.displayName();
        conversation.subtitle = profile.signatureText();
        conversation.proximity = profile.proximityText();
        conversation.familiar = hasChattedWith(profile.shortId);
    }

    private void updateConversationFromKnownPeers(Conversation conversation) {
        if (conversation == null) {
            return;
        }
        HostProtocol.PeerProfileInfo profile = findPeerProfile(conversation.shortId);
        if (profile != null) {
            updateConversationFromProfile(conversation, profile);
            return;
        }
        HostProtocol.PeerInfo peer = findPeer(conversation.shortId);
        if (peer != null) {
            updateConversationFromPeer(conversation, peer);
        }
    }

    private void syncConversationProfiles() {
        for (Conversation conversation : conversations) {
            updateConversationFromKnownPeers(conversation);
        }
    }

    private List<Conversation> sortedConversations() {
        List<Conversation> sorted = new ArrayList<>(conversations);
        sorted.sort((left, right) -> Long.compare(right.updatedAt, left.updatedAt));
        return sorted;
    }

    private Conversation openConversation(HostProtocol.PeerInfo peer) {
        Conversation conversation = ensureConversation(peer.shortId);
        updateConversationFromPeer(conversation, peer);
        selectedConversationId = conversation.id;
        conversation.unread = 0;
        if (conversation.updatedAt == 0) {
            conversation.updatedAt = System.currentTimeMillis();
        }
        showTab(1);
        return conversation;
    }

    private Conversation openConversation(HostProtocol.PeerProfileInfo profile) {
        Conversation conversation = ensureConversation(profile.shortId);
        updateConversationFromProfile(conversation, profile);
        selectedConversationId = conversation.id;
        conversation.unread = 0;
        if (conversation.updatedAt == 0) {
            conversation.updatedAt = System.currentTimeMillis();
        }
        showTab(1);
        return conversation;
    }

    private void addChatMessage(Conversation conversation, ChatMessage message) {
        if (conversation == null || message == null) {
            return;
        }
        conversation.messages.add(message);
        while (conversation.messages.size() > 80) {
            conversation.messages.remove(0);
        }
        conversation.lastText = message.text;
        conversation.lastTime = message.time;
        conversation.updatedAt = System.currentTimeMillis();
        if (message.incoming && !conversation.id.equals(selectedConversationId)) {
            conversation.unread++;
        }
    }

    private ChatMessage latestOutgoingMessage(Conversation conversation) {
        if (conversation == null) {
            return null;
        }
        for (int i = conversation.messages.size() - 1; i >= 0; i--) {
            ChatMessage message = conversation.messages.get(i);
            if (!message.incoming && !message.system) {
                return message;
            }
        }
        return null;
    }

    private void markLatestOutgoing(String conversationId, String status) {
        Conversation conversation = findConversation(conversationId);
        ChatMessage message = latestOutgoingMessage(conversation);
        if (message != null) {
            message.status = status;
        }
    }

    private void markSendingMessagesFailed() {
        for (Conversation conversation : conversations) {
            for (ChatMessage message : conversation.messages) {
                if (!message.incoming && !message.system && "发送中".equals(message.status)) {
                    message.status = "未送达";
                }
            }
        }
    }

    private String nowTimeText() {
        return new SimpleDateFormat("HH:mm", Locale.CHINA).format(new Date());
    }

    private void maybeStartBoundReconnect() {
        if (!hasBoundDevice() || debugReady) {
            return;
        }
        if (ensurePermissions(false)) {
            startScanFlow(false);
            handler.removeCallbacks(autoReconnectRunnable);
            handler.postDelayed(autoReconnectRunnable, AUTO_RECONNECT_INTERVAL_MS);
        }
    }

    private boolean isDebugBuild() {
        return (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
    }

    private void registerDebugChatReceiver() {
        if (!isDebugBuild()) {
            return;
        }
        IntentFilter filter = new IntentFilter(ACTION_DEBUG_CHAT);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(debugChatReceiver, filter, Context.RECEIVER_EXPORTED);
        } else {
            registerReceiver(debugChatReceiver, filter);
        }
    }

    private void unregisterDebugChatReceiver() {
        if (!isDebugBuild()) {
            return;
        }
        try {
            unregisterReceiver(debugChatReceiver);
        } catch (IllegalArgumentException ignored) {
            // Receiver may not be registered if activity setup was interrupted.
        }
    }

    private void handleDebugChatIntent(Intent intent) {
        if (intent == null || !ACTION_DEBUG_CHAT.equals(intent.getAction()) || !isDebugBuild()) {
            return;
        }

        long shortId = intent.getLongExtra("short_id", 0);
        Conversation selected = selectedConversation();
        if (shortId == 0 && selected != null) {
            shortId = selected.shortId;
        }
        if (shortId == 0) {
            shortId = 0x44556677L;
        }

        Conversation conversation = ensureConversation(shortId);
        String title = intent.getStringExtra("title");
        if (title != null && !title.trim().isEmpty()) {
            conversation.title = title.trim();
        } else {
            updateConversationFromKnownPeers(conversation);
        }

        String text = intent.getStringExtra("text");
        if (text == null || text.trim().isEmpty()) {
            text = "hello glimmer";
        }
        String dir = intent.getStringExtra("dir");
        if ("real".equalsIgnoreCase(dir)) {
            selectedConversationId = conversation.id;
            selectedTab = 1;
            sendChatMessageText(conversation, text);
            return;
        }
        boolean incoming = dir == null || !"out".equalsIgnoreCase(dir);
        String status = incoming ? "" : intent.getStringExtra("status");
        if (status == null) {
            status = "已送达";
        }

        addChatMessage(conversation, new ChatMessage(incoming, false, text, status, nowTimeText()));
        rememberChattedPeer(conversation.shortId);
        conversation.familiar = true;
        if (intent.getBooleanExtra("open", true)) {
            selectedConversationId = conversation.id;
            selectedTab = 1;
        }
        rerenderCurrentTab();
    }

    private void buildUi() {
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(color(0xF7F8F5));
        root.setPadding(dp(18), dp(24), dp(18), dp(12));

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
        bodyContainer = new FrameLayout(this);
        bodyContainer.addView(refreshLayout, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        root.addView(bodyContainer, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0,
                1));

        navBar = buildNav();
        root.addView(navBar, new LinearLayout.LayoutParams(
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

        String[] labels = {"附近", "消息", "我的"};
        for (int i = 0; i < labels.length; i++) {
            final int index = i;
            Button button = new Button(this);
            button.setAllCaps(false);
            button.setText(labels[i]);
            button.setTextSize(14);
            button.setOnClickListener(v -> {
                if (index == 1 && selectedTab == 1 && selectedConversationId != null) {
                    selectedConversationId = null;
                }
                showTab(index);
            });
            navButtons.add(button);
            nav.addView(button, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
        }

        return nav;
    }

    private void showTab(int index) {
        preserveCurrentChatDraft();
        if (index < 0 || index > 2) {
            index = 2;
        }
        selectedTab = index;
        if (index != 2) {
            mineDetailMode = 0;
        }
        String[] titles = {"附近的微光", "消息", "我的"};
        Conversation selectedConversation = selectedConversation();
        boolean chatMode = index == 1 && selectedConversation != null;
        titleView.setText(chatMode ? selectedConversation.title : titles[index]);
        statusView.setText(topStatusText(index));
        if (navBar != null) {
            navBar.setVisibility(chatMode ? View.GONE : View.VISIBLE);
        }

        for (int i = 0; i < navButtons.size(); i++) {
            Button button = navButtons.get(i);
            boolean selected = i == selectedTab;
            button.setTextColor(selected ? Color.WHITE : color(0x667068));
            button.setBackground(rounded(selected ? 0x2F8F7B : 0xFFFFFF, 8));
        }

        chatInput = null;
        if (bodyContainer != null) {
            bodyContainer.removeAllViews();
        }
        if (chatMode) {
            endRefreshing();
            if (refreshLayout != null) {
                refreshLayout.setEnabled(false);
            }
            if (bodyContainer != null) {
                bodyContainer.addView(chatPageView(selectedConversation), new FrameLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT));
            }
            return;
        }

        if (refreshLayout != null) {
            refreshLayout.setEnabled(true);
        }
        if (bodyContainer != null) {
            bodyContainer.addView(refreshLayout, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
        }
        content.removeAllViews();
        if (index == 0) {
            renderNearbyPage();
        } else if (index == 1) {
            renderMessagesPage();
        } else {
            renderMinePage();
        }
    }

    private void preserveCurrentChatDraft() {
        if (chatInput == null || selectedConversationId == null) {
            return;
        }
        String draft = chatInput.getText().toString();
        if (draft.isEmpty()) {
            chatDrafts.remove(selectedConversationId);
        } else {
            chatDrafts.put(selectedConversationId, draft);
        }
    }

    private void renderNearbyPage() {
        content.addView(scanPanel(), cardParams());

        content.addView(nearbySectionHeader(), marginParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                0, dp(2), 0, dp(10)));
        if (!debugReady) {
            if (!hasBoundDevice()) {
                content.addView(emptyStateCard("先绑定你的 Glimmer", "第一次使用时下拉寻找终端，绑定后这里只服务这一台。"), cardParams());
            } else {
                content.addView(emptyStateCard("等待已绑定终端", "它进入范围后会自动连接，连接后附近的人会出现在这里。"), cardParams());
            }
        } else if (peers.isEmpty() && peerProfiles.isEmpty()) {
            content.addView(emptyStateCard("等待进入范围", "Glimmer 会自动把擦肩的人推到这里。"), cardParams());
        } else {
            Set<String> profileIds = currentPeerProfileIds();
            for (HostProtocol.PeerProfileInfo profile : peerProfiles) {
                content.addView(peerProfileCard(profile), cardParams());
            }
            for (HostProtocol.PeerInfo peer : peers) {
                if (!profileIds.contains(HostProtocol.shortIdText(peer.shortId))) {
                    content.addView(peerCard(peer), cardParams());
                }
            }
        }
    }

    private LinearLayout scanPanel() {
        LinearLayout panel = card();
        panel.setPadding(dp(16), dp(16), dp(16), dp(16));

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);

        GlimmerFieldView fieldView = new GlimmerFieldView(this);
        fieldView.setPeerCount(nearbyVisibleCount());
        fieldView.setConnected(debugReady);
        row.addView(fieldView, new LinearLayout.LayoutParams(dp(112), dp(112)));

        LinearLayout copy = new LinearLayout(this);
        copy.setOrientation(LinearLayout.VERTICAL);
        copy.setPadding(dp(14), 0, 0, 0);
        copy.addView(label(nearbyTitle(), 20, color(0x1F2420), true));
        copy.addView(terminalStatusRow(),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(6), 0, dp(8)));
        String hint = scanHintText();
        if (!hint.isEmpty()) {
            copy.addView(label(hint, 12, color(0x667068), false));
        }
        row.addView(copy, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        panel.addView(row, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        if (!debugReady && !hasBoundDevice() && !scanDevices.isEmpty()) {
            int limit = Math.min(scanDevices.size(), 2);
            for (int i = 0; i < limit; i++) {
                panel.addView(terminalDeviceRow(scanDevices.get(i)), marginParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT,
                        0, dp(12), 0, 0));
            }
        }

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
        Conversation selected = selectedConversation();
        if (selected != null) {
            renderChatPage(selected);
            return;
        }

        syncConversationProfiles();
        if (conversations.isEmpty()) {
            content.addView(simpleEmptyRow("还没有对话"), cardParams());
            return;
        }

        for (Conversation conversation : sortedConversations()) {
            content.addView(conversationRow(conversation), cardParams());
        }
    }

    private void renderChatPage(Conversation conversation) {
        content.addView(chatPageView(conversation), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    private View chatPageView(Conversation conversation) {
        conversation.unread = 0;

        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);

        TextView back = label("‹ 消息", 16, color(0x2F8F7B), true);
        back.setPadding(dp(4), dp(2), dp(4), dp(8));
        back.setOnClickListener(v -> {
            selectedConversationId = null;
            showTab(1);
        });
        page.addView(back, marginParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                0, 0, 0, dp(4)));

        LinearLayout messages = new LinearLayout(this);
        messages.setOrientation(LinearLayout.VERTICAL);
        if (conversation.messages.isEmpty()) {
            messages.addView(chatSystemLine("向 " + conversation.title + " 打个招呼"), marginParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    0, dp(12), 0, dp(10)));
        } else {
            for (ChatMessage message : conversation.messages) {
                messages.addView(chatBubble(message), marginParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT,
                        0, dp(6), 0, dp(6)));
            }
        }

        messages.addView(spacer(1, dp(12)), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(12)));

        ScrollView messagesScroll = new ScrollView(this);
        messagesScroll.setFillViewport(true);
        messagesScroll.addView(messages, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        page.addView(messagesScroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0,
                1));

        page.addView(chatInputBar(conversation), marginParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                0, dp(8), 0, 0));

        messagesScroll.post(() -> messagesScroll.fullScroll(View.FOCUS_DOWN));
        return page;
    }

    private LinearLayout conversationRow(Conversation conversation) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(14), dp(12), dp(14), dp(12));
        row.setBackground(rounded(0xFFFFFF, 8));
        row.setOnClickListener(v -> {
            selectedConversationId = conversation.id;
            conversation.unread = 0;
            showTab(1);
        });

        row.addView(avatar(conversationInitial(conversation), conversation.familiar ? 0xC08A24 : 0x2F8F7B),
                new LinearLayout.LayoutParams(dp(46), dp(46)));

        LinearLayout copy = new LinearLayout(this);
        copy.setOrientation(LinearLayout.VERTICAL);
        copy.setPadding(dp(12), 0, dp(8), 0);

        LinearLayout titleRow = new LinearLayout(this);
        titleRow.setOrientation(LinearLayout.HORIZONTAL);
        titleRow.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = label(conversation.title, 16, color(0x1F2420), true);
        title.setSingleLine(true);
        titleRow.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        if (!conversation.lastTime.isEmpty()) {
            titleRow.addView(label(conversation.lastTime, 11, color(0x9AA39C), false));
        }
        copy.addView(titleRow, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        String preview = conversation.lastText.isEmpty() ? conversation.subtitle : conversation.lastText;
        TextView previewView = label(preview.isEmpty() ? conversation.proximity : preview, 13, color(0x667068), false);
        previewView.setSingleLine(true);
        copy.addView(previewView, marginParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                0, dp(4), 0, 0));
        row.addView(copy, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        if (conversation.unread > 0) {
            TextView unread = label(String.valueOf(Math.min(conversation.unread, 99)), 11, Color.WHITE, true);
            unread.setGravity(Gravity.CENTER);
            unread.setBackground(rounded(0xF06D5E, 8));
            row.addView(unread, new LinearLayout.LayoutParams(dp(24), dp(24)));
        } else {
            row.addView(label("›", 22, color(0x9AA39C), true));
        }
        return row;
    }

    private LinearLayout simpleEmptyRow(String text) {
        LinearLayout row = card();
        row.setGravity(Gravity.CENTER);
        row.setPadding(dp(18), dp(28), dp(18), dp(28));
        TextView label = label(text, 15, color(0x9AA39C), false);
        label.setGravity(Gravity.CENTER);
        row.addView(label, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        return row;
    }

    private TextView chatSystemLine(String text) {
        TextView view = label(text, 12, color(0x9AA39C), false);
        view.setGravity(Gravity.CENTER);
        return view;
    }

    private View chatBubble(ChatMessage message) {
        if (message.system) {
            return chatSystemLine(message.text);
        }

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(message.incoming ? Gravity.START : Gravity.END);

        if (message.incoming) {
            row.addView(avatar("微", 0x2F8F7B), marginParams(dp(34), dp(34), 0, 0, dp(8), 0));
        }

        LinearLayout bubbleColumn = new LinearLayout(this);
        bubbleColumn.setOrientation(LinearLayout.VERTICAL);
        bubbleColumn.setGravity(message.incoming ? Gravity.START : Gravity.END);

        TextView bubble = label(message.text, 15, color(0x1F2420), false);
        bubble.setPadding(dp(12), dp(9), dp(12), dp(9));
        bubble.setMaxWidth(Math.max(dp(180), getResources().getDisplayMetrics().widthPixels - dp(126)));
        bubble.setBackground(rounded(message.incoming ? 0xFFFFFF : 0xDCEFE9, 8));
        bubbleColumn.addView(bubble, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        if (message.status != null && !message.status.isEmpty()) {
            TextView status = label(message.status, 11,
                    "未送达".equals(message.status) ? color(0xF06D5E) : color(0x9AA39C),
                    false);
            status.setGravity(Gravity.END);
            bubbleColumn.addView(status, marginParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    0, dp(3), 0, 0));
        }

        row.addView(bubbleColumn, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        if (!message.incoming) {
            row.addView(avatar("我", 0x1F6F60), marginParams(dp(34), dp(34), dp(8), 0, 0, 0));
        }
        return row;
    }

    private LinearLayout chatInputBar(Conversation conversation) {
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(8), dp(8), dp(8), dp(8));
        bar.setBackground(rounded(0xFFFFFF, 8));

        chatInput = new EditText(this);
        chatInput.setHint("消息");
        chatInput.setTextSize(15);
        chatInput.setTextColor(color(0x1F2420));
        chatInput.setHintTextColor(color(0x9AA39C));
        chatInput.setMinLines(1);
        chatInput.setMaxLines(4);
        chatInput.setPadding(dp(12), 0, dp(12), 0);
        chatInput.setBackground(rounded(0xF1F6F2, 8));
        chatInput.setFilters(new InputFilter[]{new InputFilter.LengthFilter(HostProtocol.CHAT_TEXT_MAX_LEN)});
        String draft = chatDrafts.get(conversation.id);
        if (draft != null && !draft.isEmpty()) {
            chatInput.setText(draft);
            chatInput.setSelection(chatInput.getText().length());
        }
        bar.addView(chatInput, new LinearLayout.LayoutParams(0, dp(44), 1));

        Button send = primaryButton("发送", v -> sendChatMessage(conversation));
        send.setEnabled(debugReady);
        send.setTextColor(debugReady ? Color.WHITE : color(0x9AA39C));
        send.setBackground(rounded(debugReady ? 0x2F8F7B : 0xE4E8E3, 8));
        bar.addView(send, marginParams(dp(72), dp(44), dp(8), 0, 0, 0));
        return bar;
    }

    private void sendChatMessage(Conversation conversation) {
        if (conversation == null || chatInput == null) {
            return;
        }
        String text = chatInput.getText().toString().trim();
        if (text.isEmpty()) {
            return;
        }
        sendChatMessageText(conversation, text);
    }

    private void sendChatMessageText(Conversation conversation, String text) {
        if (conversation == null || text == null || text.trim().isEmpty()) {
            return;
        }
        text = text.trim();
        if (!debugReady) {
            addChatMessage(conversation, new ChatMessage(false, true, "我的 Glimmer 未连接", "", nowTimeText()));
            rerenderCurrentTab();
            return;
        }

        lastOutgoingConversationId = conversation.id;
        chatDrafts.remove(conversation.id);
        if (chatInput != null) {
            chatInput.setText("");
        }
        addChatMessage(conversation, new ChatMessage(false, false, text, "发送中", nowTimeText()));
        try {
            bleClient.sendP2pChat(conversation.shortId, text);
        } catch (RuntimeException e) {
            markLatestOutgoing(conversation.id, "未送达");
        }
        rerenderCurrentTab();
    }

    private String conversationInitial(Conversation conversation) {
        if (conversation == null || conversation.title == null || conversation.title.isEmpty()) {
            return "微";
        }
        return conversation.title.substring(0, 1);
    }

    private int chatBottomSpacerHeight(Conversation conversation) {
        int viewport = refreshLayout == null ? 0 : refreshLayout.getHeight();
        if (viewport <= 0) {
            viewport = getResources().getDisplayMetrics().heightPixels - dp(420);
        }
        int messageRows = conversation == null ? 0 : Math.max(1, conversation.messages.size());
        int estimatedUsed = dp(170) + messageRows * dp(66);
        return Math.max(dp(16), viewport - estimatedUsed);
    }

    private void renderMinePage() {
        if (mineDetailMode == 1) {
            renderProfileSettingsPage();
            return;
        }
        if (mineDetailMode == 2) {
            renderDeviceSettingsPage();
            return;
        }
        if (mineDetailMode == 3) {
            renderSafetySettingsPage();
            return;
        }

        LinearLayout list = card();
        list.addView(label("设置", 20, color(0x1F2420), true));
        list.addView(settingsRow("附近预览卡片",
                        myProfile == null ? "设置昵称和一句话签名" : myProfile.displayName() + " · " + myProfile.signatureText(),
                        v -> openMineDetail(1)),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(12), 0, 0));
        list.addView(settingsRow("我的 Glimmer",
                        debugReady ? "已连接 · " + deviceInfoSummary().replace("\n", " · ") : deviceInfoSummary().replace("\n", " · "),
                        v -> openMineDetail(2)),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(8), 0, 0));
        list.addView(settingsRow("隐私与安全",
                        "隐身模式、屏蔽列表、陌生问候",
                        v -> openMineDetail(3)),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(8), 0, 0));
        content.addView(list, cardParams());

        renderRecentEvents("最近事件");
    }

    private void renderProfileSettingsPage() {
        addSettingsBack();

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
    }

    private void renderDeviceSettingsPage() {
        addSettingsBack();

        LinearLayout device = card();
        device.addView(label("我的 Glimmer", 20, color(0x1F2420), true));
        device.addView(label(connectionStatus, 16, debugReady ? color(0x2F8F7B) : color(0x667068), true),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(10), 0, 0));
        device.addView(label(deviceInfoSummary(), 14, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(10), 0, dp(12)));
        device.addView(primaryButton(debugReady ? "同步设备状态" : (hasBoundDevice() ? "寻找已绑定终端" : "寻找并绑定 Glimmer"), v -> {
            if (debugReady) {
                bleClient.requestFullSync();
                setLocalStatus("正在同步 Glimmer 状态");
            } else {
                startScanFlow();
            }
        }), new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(46)));
        if (hasBoundDevice()) {
            device.addView(secondaryButton("解除绑定", v -> unbindDevice()),
                    marginParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44), 0, dp(10), 0, 0));
        }
        content.addView(device, cardParams());

        LinearLayout nearby = card();
        nearby.addView(label("附近状态", 20, color(0x1F2420), true));
        nearby.addView(label(peerSummary(), 14, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(10), 0, 0));
        content.addView(nearby, cardParams());
    }

    private void renderSafetySettingsPage() {
        addSettingsBack();

        LinearLayout card = card();
        card.addView(label("隐私与安全", 20, color(0x1F2420), true));
        card.addView(settingsRow("隐身模式", "暂未开启，后续接入资料协议", v -> setLocalStatus("隐身设置待接入")),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(12), 0, 0));
        card.addView(settingsRow("屏蔽列表", "管理不想再次遇见的人", v -> setLocalStatus("屏蔽列表待接入")),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(8), 0, 0));
        card.addView(settingsRow("陌生问候", "允许附近的人发来第一句微光", v -> setLocalStatus("陌生问候设置待接入")),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(8), 0, 0));
        content.addView(card, cardParams());
    }

    private void openMineDetail(int mode) {
        mineDetailMode = mode;
        showTab(2);
    }

    private void addSettingsBack() {
        TextView back = label("‹ 我的", 16, color(0x2F8F7B), true);
        back.setPadding(dp(4), dp(2), dp(4), dp(8));
        back.setOnClickListener(v -> {
            mineDetailMode = 0;
            showTab(2);
        });
        content.addView(back, marginParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                0, 0, 0, dp(4)));
    }

    private LinearLayout settingsRow(String title, String detail, View.OnClickListener listener) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(12), dp(12), dp(12), dp(12));
        row.setBackground(rounded(0xF7F8F5, 8));
        row.setOnClickListener(listener);

        LinearLayout copy = new LinearLayout(this);
        copy.setOrientation(LinearLayout.VERTICAL);
        copy.addView(label(title, 16, color(0x1F2420), true));
        TextView detailView = label(detail == null ? "" : detail, 13, color(0x667068), false);
        detailView.setSingleLine(true);
        detailView.setEllipsize(TextUtils.TruncateAt.END);
        copy.addView(detailView, marginParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                0, dp(4), 0, 0));
        row.addView(copy, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        row.addView(label("›", 24, color(0x9AA39C), true), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        return row;
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

    private LinearLayout nearbySectionHeader() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.addView(sectionTitle("刚刚擦肩"), new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        row.addView(infoPill(nearbyCountText()), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        return row;
    }

    private LinearLayout terminalStatusRow() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(9), dp(8), dp(9), dp(8));
        row.setBackground(rounded(0xF1F6F2, 8));
        row.setOnClickListener(v -> {
            if (debugReady) {
                bleClient.requestFullSync();
                setLocalStatus("正在同步 Glimmer 状态");
            }
        });

        String icon = debugReady ? "✓" : (bleClient.isScanning() ? "↻" : "○");
        int iconColor = debugReady ? 0x2F8F7B : (bleClient.isScanning() ? 0xE5B84B : 0x9AA39C);
        row.addView(statusIcon(icon, iconColor), new LinearLayout.LayoutParams(dp(28), dp(28)));

        LinearLayout copy = new LinearLayout(this);
        copy.setOrientation(LinearLayout.VERTICAL);
        copy.setPadding(dp(9), 0, dp(6), 0);
        copy.addView(label("我的 Glimmer", 13, color(0x1F2420), true));
        copy.addView(label(terminalCompactText(), 11, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(1), 0, 0));
        row.addView(copy, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        row.addView(label(terminalStatusActionText(), 12, color(0x1F6F60), true));
        return row;
    }

    private LinearLayout peerCard(HostProtocol.PeerInfo peer) {
        boolean familiar = hasChattedWith(peer.shortId);
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(14), dp(13), dp(14), dp(13));
        row.setBackground(rounded(familiar ? 0xFFF7DF : 0xFFFFFF, 8));
        row.setOnClickListener(v -> openConversation(peer));

        row.addView(avatar(familiar ? "↻" : "微", familiar ? 0xC08A24 : 0x2F8F7B),
                new LinearLayout.LayoutParams(dp(44), dp(44)));

        LinearLayout copy = new LinearLayout(this);
        copy.setOrientation(LinearLayout.VERTICAL);
        copy.setPadding(dp(12), 0, dp(8), 0);
        copy.addView(label(familiar ? "再遇见的微光" : "一束微光", 16, color(0x1F2420), true));
        copy.addView(label(peer.proximityText() + " · " + peer.signalText(), 13, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(4), 0, 0));
        row.addView(copy, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        row.addView(infoPill(familiar ? "再遇见" : peer.proximityText()), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        return row;
    }

    private LinearLayout peerProfileCard(HostProtocol.PeerProfileInfo profile) {
        boolean familiar = hasChattedWith(profile.shortId);
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(14), dp(13), dp(14), dp(13));
        row.setBackground(rounded(familiar ? 0xFFF7DF : 0xFFFFFF, 8));
        row.setOnClickListener(v -> openConversation(profile));

        row.addView(avatar(familiar ? "↻" : profile.displayName().substring(0, 1),
                        familiar ? 0xC08A24 : 0x2F8F7B),
                new LinearLayout.LayoutParams(dp(44), dp(44)));

        LinearLayout copy = new LinearLayout(this);
        copy.setOrientation(LinearLayout.VERTICAL);
        copy.setPadding(dp(12), 0, dp(8), 0);
        copy.addView(label(familiar ? profile.displayName() + " · 再遇见" : profile.displayName(),
                16, color(0x1F2420), true));
        copy.addView(label(profile.signatureText(), 13, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(4), 0, 0));
        copy.addView(label(profile.proximityText() + " · " + profile.signalText(), 12, color(0x9AA39C), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(4), 0, 0));
        row.addView(copy, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        row.addView(infoPill(familiar ? "再遇见" : profile.proximityText()), new LinearLayout.LayoutParams(
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
        row.setOnClickListener(v -> bindOrConnectDevice(device));

        row.addView(avatar("G", 0x1F6F60), new LinearLayout.LayoutParams(dp(38), dp(38)));

        LinearLayout copy = new LinearLayout(this);
        copy.setOrientation(LinearLayout.VERTICAL);
        copy.setPadding(dp(12), 0, 0, 0);
        copy.addView(label("Glimmer 终端", 15, color(0x1F2420), true));
        copy.addView(label(signalText(device.rssi), 12, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(3), 0, 0));
        row.addView(copy, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        row.addView(label("›", 24, color(0x2F8F7B), true), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        return row;
    }

    private void bindOrConnectDevice(GlimmerBleClient.ScanDevice device) {
        if (device == null) {
            return;
        }
        if (hasBoundDevice() && !boundAddress.equals(device.address)) {
            setLocalStatus("已绑定一台 Glimmer，请先在“我的”页面解绑");
            return;
        }
        if (!hasBoundDevice()) {
            saveBoundDevice(device);
            setLocalStatus("已绑定 " + device.displayName() + "，正在连接");
        }
        bleClient.connect(device);
        handler.removeCallbacks(autoReconnectRunnable);
        handler.postDelayed(autoReconnectRunnable, AUTO_RECONNECT_INTERVAL_MS);
    }

    private TextView statusIcon(String text, int backgroundColor) {
        TextView icon = label(text, 15, Color.WHITE, true);
        icon.setGravity(Gravity.CENTER);
        icon.setBackground(rounded(backgroundColor, 8));
        return icon;
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
        if (!debugReady) {
            startScanFlow();
            return;
        }

        if (selectedTab == 0) {
            setLocalStatus("正在同步附近的微光");
            bleClient.requestPeerTable();
            handler.postDelayed(() -> requestPeerProfilesPage(0), 300);
            endRefreshingSoon();
            return;
        }

        setLocalStatus("正在同步 Glimmer 状态");
        bleClient.requestFullSync();
        endRefreshingSoon();
    }

    private void startScanFlow() {
        startScanFlow(true);
    }

    private void requestPeerProfilesPage(int startIndex) {
        if (startIndex == 0) {
            pendingPeerProfiles.clear();
            peerProfilePageStarts.clear();
        }
        int seq = bleClient.requestPeerProfilesPage(startIndex);
        peerProfilePageStarts.put(seq, startIndex);
    }

    private void startScanFlow(boolean showRefreshing) {
        if (!ensurePermissions(showRefreshing)) {
            endRefreshing();
            return;
        }
        if (showRefreshing && refreshLayout != null) {
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
        GlimmerBleClient.ScanDevice boundDevice = findBoundScanDevice();
        if (boundDevice != null && !debugReady) {
            bleClient.connect(boundDevice);
        }
        if (!bleClient.isScanning()) {
            endRefreshing();
        }
        rerenderCurrentTab();
    }

    private GlimmerBleClient.ScanDevice findBoundScanDevice() {
        if (!hasBoundDevice()) {
            return null;
        }
        for (GlimmerBleClient.ScanDevice device : scanDevices) {
            if (boundAddress.equals(device.address)) {
                return device;
            }
        }
        return null;
    }

    @Override
    public void onDebugReady(boolean ready) {
        debugReady = ready;
        if (!ready) {
            markSendingMessagesFailed();
            deviceInfo = null;
            myProfile = null;
            peers.clear();
            peerProfiles.clear();
            handler.removeCallbacks(autoSyncRunnable);
            if (hasBoundDevice()) {
                handler.removeCallbacks(autoReconnectRunnable);
                handler.postDelayed(autoReconnectRunnable, AUTO_RECONNECT_INTERVAL_MS);
            }
        } else {
            handler.removeCallbacks(autoReconnectRunnable);
            handler.removeCallbacks(autoSyncRunnable);
            handler.postDelayed(() -> requestPeerProfilesPage(0), 900);
            handler.postDelayed(autoSyncRunnable, AUTO_SYNC_INTERVAL_MS);
        }
        rerenderCurrentTab();
    }

    @Override
    public void onHostMessage(HostProtocol.HostMessage message, String formatted) {
        try {
            if (message.frameType == HostProtocol.TYPE_RSP && message.cmd == HostProtocol.CMD_GET_DEVICE_INFO && message.status == 0) {
                deviceInfo = HostProtocol.parseDeviceInfo(message.payload);
                saveBoundDeviceInfo();
                pushEvent("我的 Glimmer 信息已更新");
            } else if (message.frameType == HostProtocol.TYPE_RSP && message.cmd == HostProtocol.CMD_GET_PEER_TABLE && message.status == 0) {
                int previousCount = peers.size();
                peers.clear();
                for (HostProtocol.PeerInfo peer : HostProtocol.parsePeerTable(message.payload)) {
                    if (peer.level > 0) {
                        peers.add(peer);
                    }
                }
                if (peers.isEmpty()) {
                    pushEvent("附近列表已刷新，暂时没有微光");
                } else if (peers.size() != previousCount) {
                    pushEvent("发现 " + peers.size() + " 束附近微光");
                }
            } else if (message.frameType == HostProtocol.TYPE_RSP && message.cmd == HostProtocol.CMD_GET_PROFILE_SUMMARY && message.status == 0) {
                myProfile = HostProtocol.parseProfileSummary(message.payload);
                setLocalStatus("附近预览卡片已同步");
                pushEvent("附近预览卡片已同步");
            } else if (message.frameType == HostProtocol.TYPE_RSP && message.cmd == HostProtocol.CMD_GET_PEER_PROFILES && message.status == 0) {
                Integer responseStart = peerProfilePageStarts.remove(message.seq);
                Set<String> previousIds = currentPeerProfileIds();
                HostProtocol.PeerProfilePage page = HostProtocol.parsePeerProfilePage(message.payload);
                if (responseStart == null || responseStart == 0) {
                    pendingPeerProfiles.clear();
                }
                for (HostProtocol.PeerProfileInfo profile : page.profiles) {
                    if (profile.level > 0) {
                        upsertPeerProfile(pendingPeerProfiles, profile);
                    }
                }
                if (page.hasMore()) {
                    requestPeerProfilesPage(page.nextIndex);
                } else {
                    peerProfiles.clear();
                    peerProfiles.addAll(pendingPeerProfiles);
                    pendingPeerProfiles.clear();
                    if (hasNewPeerProfile(previousIds)) {
                        pushEvent("有新的微光进入范围");
                    } else if (!peerProfiles.isEmpty()) {
                        pushEvent("读到 " + peerProfiles.size() + " 张附近资料卡");
                    }
                }
            } else if (message.frameType == HostProtocol.TYPE_EVENT && message.cmd == HostProtocol.EVENT_P2P_CHAT) {
                HostProtocol.ChatEvent event = HostProtocol.parseChatEvent(message.payload);
                Conversation conversation = ensureConversation(event.shortId);
                updateConversationFromKnownPeers(conversation);
                if (event.isDropped()) {
                    addChatMessage(conversation, new ChatMessage(true, true, "一条消息已过期", "", nowTimeText()));
                } else {
                    String text = event.text == null || event.text.isEmpty() ? " " : event.text;
                    if (event.isTruncated()) {
                        text = text + "…";
                    }
                    addChatMessage(conversation, new ChatMessage(true, false, text, "", nowTimeText()));
                    rememberChattedPeer(event.shortId);
                    conversation.familiar = true;
                }
                pushEvent(event.isDropped() ? "一条微光已过期" : "收到一条来自附近的微光");
            } else if (message.frameType == HostProtocol.TYPE_EVENT && message.cmd == HostProtocol.EVENT_P2P_CHAT_TX_RESULT) {
                HostProtocol.ChatTxResult result = HostProtocol.parseChatTxResult(message.payload);
                String txStatus = chatTxStatusText(result);
                Conversation conversation = result.shortId == 0 ? null : findConversation(HostProtocol.shortIdText(result.shortId));
                if (conversation == null) {
                    conversation = findConversation(lastOutgoingConversationId);
                }
                if (conversation != null) {
                    markLatestOutgoing(conversation.id, txStatus);
                    if (result.isSuccess()) {
                        rememberChattedPeer(conversation.shortId);
                        conversation.familiar = true;
                    }
                }
                pushEvent(chatTxEventText(result));
            } else if (message.frameType == HostProtocol.TYPE_RSP && message.status != 0) {
                if (message.cmd == HostProtocol.CMD_P2P_CHAT_SEND) {
                    markLatestOutgoing(lastOutgoingConversationId, "未送达");
                }
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

    private Set<String> currentPeerProfileIds() {
        Set<String> ids = new HashSet<>();
        for (HostProtocol.PeerProfileInfo profile : peerProfiles) {
            ids.add(HostProtocol.shortIdText(profile.shortId));
        }
        return ids;
    }

    private void upsertPeerProfile(List<HostProtocol.PeerProfileInfo> target,
                                   HostProtocol.PeerProfileInfo profile) {
        String id = HostProtocol.shortIdText(profile.shortId);
        for (int i = 0; i < target.size(); i++) {
            if (HostProtocol.shortIdText(target.get(i).shortId).equals(id)) {
                target.set(i, profile);
                return;
            }
        }
        target.add(profile);
    }

    private int nearbyVisibleCount() {
        Set<String> ids = new HashSet<>();
        for (HostProtocol.PeerProfileInfo profile : peerProfiles) {
            ids.add(HostProtocol.shortIdText(profile.shortId));
        }
        for (HostProtocol.PeerInfo peer : peers) {
            ids.add(HostProtocol.shortIdText(peer.shortId));
        }
        return ids.size();
    }

    private boolean hasNewPeerProfile(Set<String> previousIds) {
        for (HostProtocol.PeerProfileInfo profile : peerProfiles) {
            if (!previousIds.contains(HostProtocol.shortIdText(profile.shortId))) {
                return true;
            }
        }
        return false;
    }

    private void rerenderCurrentTab() {
        if (content != null) {
            showTab(selectedTab);
        }
    }

    private String topStatusText(int index) {
        if (index == 0) {
            if (debugReady) {
                return "✓ 我的 Glimmer 已连接";
            }
            if (!hasBoundDevice()) {
                return bleClient.isScanning() ? "↻ 正在寻找可绑定终端" : "○ 我的 Glimmer 未绑定";
            }
            return bleClient.isScanning() ? "↻ 正在找回已绑定终端" : "○ 等待已绑定 Glimmer";
        }
        if (index == 1) {
            Conversation conversation = selectedConversation();
            if (conversation != null) {
                if (conversation.subtitle != null && !conversation.subtitle.isEmpty()) {
                    return conversation.subtitle;
                }
                return conversation.proximity == null || conversation.proximity.isEmpty() ? "微光对话" : conversation.proximity;
            }
            int unread = 0;
            for (Conversation item : conversations) {
                unread += item.unread;
            }
            return unread > 0 ? unread + " 条未读" : "最近对话";
        }
        if (index == 2) {
            if (mineDetailMode == 1) {
                return "附近预览卡片";
            }
            if (mineDetailMode == 2) {
                return "终端绑定与状态";
            }
            if (mineDetailMode == 3) {
                return "隐私与安全";
            }
            return connectionStatus;
        }
        return connectionStatus;
    }

    private String nearbyTitle() {
        if (!debugReady) {
            if (!hasBoundDevice()) {
                return bleClient.isScanning() ? "选择你的 Glimmer" : "绑定你的 Glimmer";
            }
            return bleClient.isScanning() ? "正在找回终端" : "等待自动连接";
        }
        if (peers.isEmpty() && peerProfiles.isEmpty()) {
            return "等待进入范围";
        }
        int count = nearbyVisibleCount();
        return count + " 位进入范围";
    }

    private String nearbyCopy() {
        if (!debugReady) {
            return hasBoundDevice() ? "已绑定的终端进入范围后会自动连接。" : "一个 App 只绑定一台微光终端。";
        }
        if (peers.isEmpty()) {
            return "不显示精确距离，只显示可交流的强弱。";
        }
        return "不显示精确距离，只显示可交流的强弱。";
    }

    private String scanHintText() {
        if (!hasBoundDevice()) {
            return bleClient.isScanning() ? "正在寻找可绑定的终端" : "下拉页面绑定你的 Glimmer";
        }
        if (!debugReady) {
            return bleClient.isScanning() ? "正在自动连接已绑定终端" : "下拉页面寻找已绑定终端";
        }
        return "";
    }

    private String nearbyCountText() {
        if (!debugReady) {
            return hasBoundDevice() ? "未连接" : "未绑定";
        }
        int count = nearbyVisibleCount();
        return count == 0 ? "暂无" : count + " 位";
    }

    private String terminalCompactText() {
        if (debugReady) {
            return deviceInfo == null ? "已连接" : deviceInfo.shortCode();
        }
        if (bleClient.isScanning()) {
            if (hasBoundDevice()) {
                return "找回中";
            }
            return scanDevices.isEmpty() ? "寻找中" : scanDevices.size() + " 台可绑定";
        }
        if (hasBoundDevice()) {
            return boundShortId == null ? "已绑定" : boundShortId;
        }
        return scanDevices.isEmpty() ? "未绑定" : scanDevices.size() + " 台可绑定";
    }

    private String terminalStatusActionText() {
        if (debugReady) {
            return "已连";
        }
        if (bleClient.isScanning()) {
            return "寻找中";
        }
        return hasBoundDevice() ? "等待" : "未绑定";
    }

    private String terminalTitle() {
        if (debugReady) {
            return "我的 Glimmer 已连接";
        }
        if (bleClient.isScanning()) {
            return hasBoundDevice() ? "正在找回已绑定终端" : "正在寻找你的 Glimmer";
        }
        return hasBoundDevice() ? "已绑定的 Glimmer" : "绑定你的 Glimmer";
    }

    private String terminalCopy() {
        if (debugReady) {
            return "终端已就绪，正在保持附近可见。";
        }
        if (hasBoundDevice()) {
            return "已绑定 " + (boundName == null ? "Glimmer" : boundName) + "。它进入范围后会自动连接。";
        }
        return "第一次使用需要下拉寻找，并选择一台终端完成绑定。";
    }

    private String scanSummary() {
        if (bleClient.isScanning()) {
            return hasBoundDevice() ? "正在寻找已绑定终端" : "正在寻找，已发现 " + scanDevices.size() + " 台可绑定终端";
        }
        if (scanDevices.isEmpty()) {
            return hasBoundDevice() ? "已绑定终端暂时不在范围内。" : "还没有发现可绑定的 Glimmer 终端。";
        }
        return hasBoundDevice() ? "已绑定终端进入范围后会自动连接。" : "发现 " + scanDevices.size() + " 台 Glimmer 终端，点击即可绑定。";
    }

    private String peerSummary() {
        if (!debugReady) {
            return hasBoundDevice() ? "等待已绑定终端进入范围" : "绑定终端后同步附近状态";
        }
        if (peers.isEmpty() && peerProfiles.isEmpty()) {
            return "附近暂时没有微光";
        }
        int count = nearbyVisibleCount();
        return "附近有 " + count + " 位微光";
    }

    private String deviceInfoSummary() {
        if (deviceInfo == null) {
            if (debugReady) {
                return "正在读取固件和设备识别信息。";
            }
            if (hasBoundDevice()) {
                return "已绑定 " + (boundName == null ? "Glimmer" : boundName)
                        + (boundShortId == null ? "" : "\n识别码 " + boundShortId);
            }
            return "还没有绑定终端。一个 App 只能绑定一台 Glimmer。";
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
        if (raw.contains("status=")) {
            return raw.replace("GATT", "Glimmer");
        }
        if (raw.startsWith("扫描结束")) {
            if (hasBoundDevice()) {
                return findBoundScanDevice() == null ? "已绑定终端暂时不在范围内" : "已找到绑定终端";
            }
            return scanDevices.isEmpty() ? "暂未发现 Glimmer 终端" : "发现 " + scanDevices.size() + " 台 Glimmer 终端";
        }
        if (raw.startsWith("扫描失败")) {
            return "扫描失败，请稍后重试";
        }
        if (raw.contains("正在寻找")) {
            return hasBoundDevice() ? "正在找回已绑定终端" : "正在寻找你的 Glimmer";
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

    private String chatTxStatusText(HostProtocol.ChatTxResult result) {
        if (result.isSuccess()) {
            return "已送达";
        }
        return result.isTimeout() ? "未确认" : "未送达";
    }

    private String chatTxEventText(HostProtocol.ChatTxResult result) {
        if (result.isSuccess()) {
            return "消息已送达";
        }
        if (result.isTimeout()) {
            return "消息已发出，暂时未确认";
        }
        return "对方暂时不可达，消息未送达";
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
