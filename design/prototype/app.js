const people = [
  {
    id: "lan",
    name: "岚",
    avatar: "岚",
    line: "喜欢在深夜散步，也喜欢安静的人。",
    signal: "S3",
    rssi: "-13",
    tags: ["刚刚擦肩", "夜行", "慢热"],
    last: "刚刚"
  },
  {
    id: "chen",
    name: "晨",
    avatar: "晨",
    line: "在城市里收集偶然的瞬间。",
    signal: "S2",
    rssi: "-46",
    tags: ["咖啡", "摄影", "附近"],
    last: "2 分钟前"
  },
  {
    id: "moss",
    name: "苔",
    avatar: "苔",
    line: "今天只想听歌，不想解释太多。",
    signal: "S1",
    rssi: "-71",
    tags: ["音乐", "低电量", "安静"],
    last: "5 分钟前"
  }
];

const threads = [
  {
    id: "lan",
    name: "岚",
    avatar: "岚",
    last: "可以，从一句很短的话开始。",
    state: "已送达",
    unread: true
  },
  {
    id: "chen",
    name: "晨",
    avatar: "晨",
    last: "对方暂时离开了微光范围。",
    state: "未送达",
    unread: false
  }
];

let selectedPerson = people[0];
let visible = true;
let stealth = false;

const onboarding = document.querySelector("#onboarding");
const app = document.querySelector("#app");
const peopleList = document.querySelector("#people-list");
const threadList = document.querySelector("#thread-list");
const sheet = document.querySelector("#person-sheet");
const toast = document.querySelector("#toast");
const chatLog = document.querySelector("#chat-log");
const composer = document.querySelector("#composer");
const messageInput = document.querySelector("#message-input");

function renderPeople() {
  peopleList.innerHTML = people.map((person) => `
    <button class="person-card" data-person="${person.id}">
      <span class="avatar">${person.avatar}</span>
      <span>
        <strong>${person.name}</strong>
        <p>${person.line}</p>
      </span>
      <span class="signal">${person.signal}</span>
    </button>
  `).join("");
}

function renderThreads() {
  threadList.innerHTML = threads.map((thread) => `
    <button class="thread-card" data-thread="${thread.id}">
      <span class="avatar">${thread.avatar}</span>
      <span>
        <strong>${thread.name}</strong>
        <p>${thread.last}</p>
      </span>
      ${thread.unread ? '<span class="unread-dot" aria-label="未读"></span>' : `<span class="signal">${thread.state}</span>`}
    </button>
  `).join("");
}

function initialChat() {
  chatLog.innerHTML = "";
  addSystemLine("你们刚刚在附近被彼此发现。");
  addBubble("对方", "如果你也在等车，那我们可能在同一个方向。", false, "刚刚");
  addBubble("我", "我在，先打个很轻的招呼。", true, "已送达");
}

function addBubble(sender, text, mine, state) {
  const bubble = document.createElement("div");
  bubble.className = mine ? "bubble mine" : "bubble";
  bubble.innerHTML = `${escapeHtml(text)}<small>${sender} · ${state}</small>`;
  chatLog.appendChild(bubble);
  chatLog.scrollTop = chatLog.scrollHeight;
  return bubble;
}

function addSystemLine(text) {
  const line = document.createElement("div");
  line.className = "system-line";
  line.textContent = text;
  chatLog.appendChild(line);
}

function escapeHtml(text) {
  return text.replace(/[&<>"']/g, (char) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    "\"": "&quot;",
    "'": "&#039;"
  }[char]));
}

function enterApp() {
  onboarding.classList.add("hidden");
  app.classList.remove("hidden");
  renderPeople();
  renderThreads();
  initialChat();
}

function switchTab(tab) {
  document.querySelectorAll(".view").forEach((view) => view.classList.remove("active"));
  document.querySelectorAll(".nav-item").forEach((item) => item.classList.remove("active"));

  const view = document.querySelector(`#view-${tab}`);
  if (!view) {
    return;
  }

  view.classList.add("active");
  const nav = document.querySelector(`.nav-item[data-tab="${tab}"]`);
  if (nav) {
    nav.classList.add("active");
  }

  document.querySelector("#header-title").textContent = view.dataset.title || "";
  document.querySelector("#header-eyebrow").textContent = view.dataset.eyebrow || "";
}

function openPerson(personId) {
  selectedPerson = people.find((person) => person.id === personId) || people[0];
  document.querySelector("#sheet-avatar").textContent = selectedPerson.avatar;
  document.querySelector("#sheet-name").textContent = selectedPerson.name;
  document.querySelector("#sheet-line").textContent = selectedPerson.line;
  document.querySelector("#sheet-signal").textContent = `信号 ${selectedPerson.signal} · RSSI ${selectedPerson.rssi}`;
  document.querySelector("#sheet-tags").innerHTML = selectedPerson.tags.map((tag) => `<span class="tag">${tag}</span>`).join("");
  sheet.classList.remove("hidden");
}

function closeSheet() {
  sheet.classList.add("hidden");
}

function openChat() {
  closeSheet();
  document.querySelector("#chat-peer-name").textContent = selectedPerson.name;
  document.querySelector("#chat-peer-state").textContent = `附近，信号 ${selectedPerson.signal}`;
  switchTab("chat");
}

function showToast(text) {
  toast.textContent = text;
  toast.classList.remove("hidden");
  window.clearTimeout(showToast.timer);
  showToast.timer = window.setTimeout(() => toast.classList.add("hidden"), 1800);
}

function scanNearby() {
  const title = document.querySelector("#scan-title");
  const copy = document.querySelector("#scan-copy");
  title.textContent = "正在寻找附近的微光";
  copy.textContent = "保持 Glimmer 在身边，几秒后会更新列表。";
  showToast("正在扫描附近");

  window.setTimeout(() => {
    title.textContent = stealth ? "你正在隐身" : "3 束微光在附近";
    copy.textContent = stealth ? "附近的人暂时看不到你。" : "不显示精确距离，只显示可交流的强弱。";
    showToast(stealth ? "隐身中，列表仅供预览" : "附近列表已更新");
  }, 900);
}

function toggleStealth() {
  stealth = !stealth;
  document.querySelector("#stealth-toggle").checked = stealth;
  document.querySelector("#scan-title").textContent = stealth ? "你正在隐身" : "3 束微光在附近";
  document.querySelector("#scan-copy").textContent = stealth ? "附近的人暂时看不到你。" : "不显示精确距离，只显示可交流的强弱。";
  showToast(stealth ? "已进入隐身模式" : "已恢复可见");
}

function toggleVisible() {
  visible = !visible;
  document.querySelector("#visible-copy").textContent = visible ? "正常可见" : "只接收不展示";
  showToast(visible ? "已恢复正常可见" : "已切换为只接收不展示");
}

function sayHi() {
  closeSheet();
  switchTab("chat");
  addBubble("我", `你好，${selectedPerson.name}。我看到你的微光了。`, true, "发送中");
  window.setTimeout(() => {
    const bubbles = chatLog.querySelectorAll(".bubble.mine small");
    bubbles[bubbles.length - 1].textContent = "我 · 已送达";
    showToast("问候已送达");
  }, 700);
}

function sendMessage(text) {
  const bubble = addBubble("我", text, true, "发送中");
  messageInput.value = "";
  window.setTimeout(() => {
    bubble.querySelector("small").textContent = "我 · 已送达";
    showToast("消息已送达");
  }, 700);

  if (text.includes("你好") || text.includes("在吗")) {
    window.setTimeout(() => {
      addBubble(selectedPerson.name, "我在。刚好这束微光还没有走远。", false, "刚刚");
    }, 1300);
  }
}

function simulateFailure() {
  const bubble = addBubble("我", "如果你还在附近，我们可以继续聊。", true, "发送中");
  window.setTimeout(() => {
    bubble.classList.add("failed");
    bubble.querySelector("small").textContent = "我 · 未送达，可以重试";
    addSystemLine("对方暂时离开了微光范围，消息未送达。");
    showToast("消息未送达");
  }, 700);
}

function hidePerson() {
  closeSheet();
  showToast("已暂时隐藏这个微光");
}

document.addEventListener("click", (event) => {
  const action = event.target.closest("[data-action]")?.dataset.action;
  const tab = event.target.closest("[data-tab]")?.dataset.tab;
  const person = event.target.closest("[data-person]")?.dataset.person;
  const thread = event.target.closest("[data-thread]")?.dataset.thread;

  if (tab) {
    switchTab(tab);
  }

  if (person) {
    openPerson(person);
  }

  if (thread) {
    const found = people.find((item) => item.id === thread);
    if (found) {
      selectedPerson = found;
    }
    openChat();
  }

  switch (action) {
    case "enter-app":
    case "enter-app-muted":
      enterApp();
      break;
    case "close-sheet":
      closeSheet();
      break;
    case "open-chat":
      openChat();
      break;
    case "say-hi":
      sayHi();
      break;
    case "scan":
      scanNearby();
      break;
    case "toggle-stealth":
      toggleStealth();
      break;
    case "toggle-visible":
      toggleVisible();
      break;
    case "open-diagnostics":
      switchTab("diagnostics");
      break;
    case "simulate-fail":
      simulateFailure();
      break;
    case "hide-person":
      hidePerson();
      break;
    case "edit-profile":
      showToast("这里会进入资料编辑页");
      break;
    case "show-blocked":
      showToast("当前没有屏蔽的人");
      break;
    default:
      break;
  }
});

document.querySelector("#stealth-toggle").addEventListener("change", (event) => {
  stealth = event.target.checked;
  document.querySelector("#scan-title").textContent = stealth ? "你正在隐身" : "3 束微光在附近";
  document.querySelector("#scan-copy").textContent = stealth ? "附近的人暂时看不到你。" : "不显示精确距离，只显示可交流的强弱。";
});

composer.addEventListener("submit", (event) => {
  event.preventDefault();
  const text = messageInput.value.trim();
  if (!text) {
    showToast("先写一句话");
    return;
  }
  sendMessage(text);
});

renderPeople();
renderThreads();
initialChat();
