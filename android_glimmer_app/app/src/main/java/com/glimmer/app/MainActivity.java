package com.glimmer.app;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity {
    private static final int REQUEST_APP_PERMISSIONS = 1201;

    private final List<Button> navButtons = new ArrayList<>();

    private LinearLayout root;
    private LinearLayout content;
    private TextView titleView;
    private TextView statusView;
    private int selectedTab;

    private final Screen[] screens = {
            new Screen("附近", "附近的微光", "正在寻找附近可见的人", "连接 Glimmer 后，这里会显示附近可见的用户、信号强度和最近活跃状态。"),
            new Screen("消息", "消息", "暂无新的对话", "之后这里会展示一对一会话、未读消息，以及发送中、已送达、未送达等状态。"),
            new Screen("我的", "我的", "设置你被看见的样子", "这里会放昵称、一句话、可见性状态、我的 Glimmer 终端和隐藏诊断入口。"),
            new Screen("安全", "安全", "随时保留退出和屏蔽", "这里会放隐身模式、陌生问候开关、屏蔽列表和本地消息保留策略。"),
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
        showTab(0);
    }

    private void buildUi() {
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(color(0xF7F8F5));
        root.setPadding(dp(18), dp(18), dp(18), dp(12));

        titleView = new TextView(this);
        titleView.setText("微光");
        titleView.setTextColor(color(0x1F2420));
        titleView.setTextSize(28);
        titleView.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        root.addView(titleView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        statusView = new TextView(this);
        statusView.setText("正在寻找你的 Glimmer");
        statusView.setTextColor(color(0x667068));
        statusView.setTextSize(14);
        root.addView(statusView, marginParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                0, dp(6), 0, dp(14)));

        content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        ScrollView scrollView = new ScrollView(this);
        scrollView.addView(content);
        root.addView(scrollView, new LinearLayout.LayoutParams(
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

        for (int i = 0; i < screens.length; i++) {
            final int index = i;
            Button button = new Button(this);
            button.setAllCaps(false);
            button.setText(screens[i].navTitle);
            button.setTextSize(14);
            button.setOnClickListener(v -> showTab(index));
            navButtons.add(button);
            nav.addView(button, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
        }

        return nav;
    }

    private void showTab(int index) {
        selectedTab = index;
        Screen screen = screens[index];
        titleView.setText(screen.title);
        statusView.setText(index == 0 ? "我的 Glimmer 未连接" : "正式 App 骨架已就绪");

        for (int i = 0; i < navButtons.size(); i++) {
            Button button = navButtons.get(i);
            boolean selected = i == selectedTab;
            button.setTextColor(selected ? Color.WHITE : color(0x667068));
            button.setBackground(rounded(selected ? 0x2F8F7B : 0xFFFFFF, 8));
        }

        content.removeAllViews();
        if (index == 0) {
            addWelcomeCard();
        }
        addPageCard(screen.heading, screen.body);
        addActionRow(index);
    }

    private void addWelcomeCard() {
        LinearLayout card = card();

        TextView eyebrow = label("Glimmer", 12, color(0x2F8F7B), true);
        card.addView(eyebrow);

        TextView copy = label("附近有人发来一束微光", 22, color(0x1F2420), true);
        card.addView(copy, marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(8), 0, 0));

        TextView body = label("我们不展示精确位置，也不做长期离线消息。先连接你的 Glimmer，再安静地发现附近的人。", 15, color(0x667068), false);
        card.addView(body, marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(10), 0, 0));

        Button permission = primaryButton("允许并连接");
        permission.setOnClickListener(v -> requestAppPermissions());
        card.addView(permission, marginParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(48), 0, dp(18), 0, 0));

        content.addView(card, cardParams());
    }

    private void addPageCard(String heading, String bodyText) {
        LinearLayout card = card();
        card.addView(label(heading, 20, color(0x1F2420), true));
        card.addView(label(bodyText, 15, color(0x667068), false),
                marginParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0, dp(10), 0, 0));
        content.addView(card, cardParams());
    }

    private void addActionRow(int index) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);

        if (index == 0) {
            row.addView(secondaryButton("扫描附近"), new LinearLayout.LayoutParams(0, dp(48), 1));
            row.addView(spacer(dp(10), 1));
            row.addView(secondaryButton("查看资料"), new LinearLayout.LayoutParams(0, dp(48), 1));
        } else if (index == 1) {
            row.addView(secondaryButton("新消息"), new LinearLayout.LayoutParams(0, dp(48), 1));
            row.addView(spacer(dp(10), 1));
            row.addView(secondaryButton("未送达"), new LinearLayout.LayoutParams(0, dp(48), 1));
        } else if (index == 2) {
            row.addView(secondaryButton("编辑资料"), new LinearLayout.LayoutParams(0, dp(48), 1));
            row.addView(spacer(dp(10), 1));
            row.addView(secondaryButton("我的 Glimmer"), new LinearLayout.LayoutParams(0, dp(48), 1));
        } else {
            row.addView(secondaryButton("隐身模式"), new LinearLayout.LayoutParams(0, dp(48), 1));
            row.addView(spacer(dp(10), 1));
            row.addView(secondaryButton("屏蔽列表"), new LinearLayout.LayoutParams(0, dp(48), 1));
        }

        content.addView(row, marginParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(54), 0, dp(2), 0, dp(18)));
    }

    private void requestAppPermissions() {
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

        List<String> missing = new ArrayList<>();
        for (String permission : permissions) {
            if (checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
                missing.add(permission);
            }
        }

        if (missing.isEmpty()) {
            statusView.setText("权限已就绪，下一步接入 Glimmer 连接层");
        } else {
            requestPermissions(missing.toArray(new String[0]), REQUEST_APP_PERMISSIONS);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_APP_PERMISSIONS) {
            statusView.setText("权限请求已返回，下一步接入扫描和连接");
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

    private TextView label(String text, int sp, int color, boolean bold) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(sp);
        view.setTextColor(color);
        view.setLineSpacing(dp(2), 1.0f);
        if (bold) {
            view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        }
        return view;
    }

    private Button primaryButton(String text) {
        Button button = new Button(this);
        button.setAllCaps(false);
        button.setText(text);
        button.setTextColor(Color.WHITE);
        button.setTextSize(15);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setBackground(rounded(0x2F8F7B, 8));
        return button;
    }

    private Button secondaryButton(String text) {
        Button button = new Button(this);
        button.setAllCaps(false);
        button.setText(text);
        button.setTextColor(color(0x1F2420));
        button.setTextSize(14);
        button.setBackground(rounded(0xFFFFFF, 8));
        return button;
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

    private static final class Screen {
        final String navTitle;
        final String title;
        final String heading;
        final String body;

        Screen(String navTitle, String title, String heading, String body) {
            this.navTitle = navTitle;
            this.title = title;
            this.heading = heading;
            this.body = body;
        }
    }
}
