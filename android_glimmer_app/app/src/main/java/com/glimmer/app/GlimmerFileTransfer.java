package com.glimmer.app;

import android.content.Intent;
import android.os.Handler;
import android.util.Log;

import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.Locale;
import java.util.Queue;
import java.util.zip.CRC32;

final class GlimmerFileTransfer {
    static final String TAG = "GlimmerFile";
    static final String ACTION_DEBUG_FILE = "com.glimmer.app.DEBUG_FILE";
    static final int KIND_TEST = 0;
    static final int KIND_IMAGE = 1;
    static final int KIND_AVATAR = 2;
    static final int MAX_TRANSFER_BYTES = 16 * 1024;
    static final int MAX_USER_OBJECT_BYTES = 14 * 1024;
    static final int MAX_AVATAR_BYTES = 6 * 1024;

    private static final int MAGIC_G = 0x47;
    private static final int MAGIC_F = 0x46;
    private static final int VERSION = 1;
    private static final int TYPE_START = 1;
    private static final int TYPE_DATA = 2;
    private static final int TYPE_REPORT = 3;
    private static final int TYPE_DONE = 4;
    private static final int TYPE_CANCEL = 5;

    private static final int MIN_CHUNK_BYTES = 16;
    private static final int DEFAULT_CHUNK_BYTES = 48;
    private static final int MAX_CHUNK_BYTES = 48;
    private static final int MAX_CHUNKS = 384;
    private static final int SEND_RETRY_MAX = 40;
    private static final long SEND_RETRY_DELAY_MS = 140;
    private static final long SEND_GAP_MS = 60;
    private static final long REPORT_DELAY_MS = 350;
    private static final long REPORT_TIMEOUT_MS = 8000;
    private static final int WATCHDOG_RESEND_MAX = 3;

    private final GlimmerBleClient bleClient;
    private final Handler handler;
    private final Listener listener;
    private final Queue<OutboundFrame> txQueue = new ArrayDeque<>();
    private final Runnable drainRunnable = this::drainTxQueue;
    private final Runnable reportRunnable = this::sendPendingReport;
    private final Runnable senderWatchdogRunnable = this::onSenderWatchdog;

    private OutboundFrame activeFrame;
    private SendSession sendSession;
    private ReceiveSession receiveSession;
    private TestRequest pendingDebugRequest;
    private int fileSerial = 1;

    GlimmerFileTransfer(GlimmerBleClient bleClient, Handler handler) {
        this(bleClient, handler, null);
    }

    GlimmerFileTransfer(GlimmerBleClient bleClient, Handler handler, Listener listener) {
        this.bleClient = bleClient;
        this.handler = handler;
        this.listener = listener;
    }

    void onDebugReadyChanged(boolean ready) {
        if (ready) {
            TestRequest request = pendingDebugRequest;
            pendingDebugRequest = null;
            if (request != null) {
                startTestTransfer(request);
            }
            drainTxQueue();
            return;
        }
        activeFrame = null;
        txQueue.clear();
        if (sendSession != null) {
            notifyTxFailed(sendSession.clientTag, sendSession.targetShortId,
                    sendSession.fileId, "not_ready");
        }
        sendSession = null;
        handler.removeCallbacks(senderWatchdogRunnable);
    }

    boolean sendImage(long targetShortId, byte[] imageBytes, long clientTag) {
        return sendObject(targetShortId, imageBytes, clientTag, KIND_IMAGE, "image");
    }

    boolean sendAvatar(long targetShortId, byte[] avatarBytes, long clientTag) {
        return sendObject(targetShortId, avatarBytes, clientTag, KIND_AVATAR, "avatar");
    }

    boolean isIdle() {
        return sendSession == null && activeFrame == null && txQueue.isEmpty();
    }

    private boolean sendObject(long targetShortId, byte[] imageBytes, long clientTag,
                               int objectKind, String label) {
        int maxBytes = objectKind == KIND_AVATAR ? MAX_AVATAR_BYTES : MAX_TRANSFER_BYTES;
        if (imageBytes == null || imageBytes.length == 0) {
            notifyTxFailed(clientTag, targetShortId, 0, "empty_image");
            return false;
        }
        if (imageBytes.length > maxBytes) {
            notifyTxFailed(clientTag, targetShortId, 0, "too_large");
            return false;
        }
        if (!bleClient.isDebugReady()) {
            notifyTxFailed(clientTag, targetShortId, 0, "not_ready");
            return false;
        }
        if (sendSession != null || activeFrame != null || !txQueue.isEmpty()) {
            notifyTxFailed(clientTag, targetShortId, 0, "busy");
            return false;
        }
        startTransfer(targetShortId, imageBytes, DEFAULT_CHUNK_BYTES, 0, -1,
                objectKind, clientTag, label);
        return true;
    }

    void onDebugFileIntent(Intent intent) {
        if (intent == null || !ACTION_DEBUG_FILE.equals(intent.getAction())) {
            return;
        }
        long targetShortId = intent.getLongExtra("short_id", 0);
        int size = intent.getIntExtra("size", 1024);
        int chunkBytes = intent.getIntExtra("chunk", DEFAULT_CHUNK_BYTES);
        int dropEvery = intent.getIntExtra("drop_every", 0);
        int dropOnce = intent.getIntExtra("drop_once", -1);
        TestRequest request = new TestRequest(targetShortId, size, chunkBytes, dropEvery, dropOnce);
        if (!bleClient.isDebugReady()) {
            pendingDebugRequest = request;
            Log.i(TAG, "start_pending reason=not_ready target=" + shortId(targetShortId));
            return;
        }
        startTestTransfer(request);
    }

    void onHostMessage(HostProtocol.HostMessage message) {
        if (message == null) {
            return;
        }
        if (message.frameType == HostProtocol.TYPE_RSP
                && message.cmd == HostProtocol.CMD_P2P_FILE_SEND) {
            handleFileSendResponse(message);
        } else if (message.frameType == HostProtocol.TYPE_EVENT
                && message.cmd == HostProtocol.EVENT_P2P_FILE) {
            handleFileEvent(message.payload);
        }
    }

    private void startTestTransfer(TestRequest request) {
        long targetShortId = request.targetShortId;
        int size = request.size;
        int chunkBytes = request.chunkBytes;
        int dropEvery = request.dropEvery;
        int dropOnce = request.dropOnce;

        if (targetShortId == 0) {
            Log.w(TAG, "start_failed reason=missing_target");
            return;
        }
        if (!bleClient.isDebugReady()) {
            Log.w(TAG, "start_failed reason=not_ready target=" + shortId(targetShortId));
            return;
        }

        int safeSize = clamp(size, 1, MAX_TRANSFER_BYTES);
        int safeChunk = clamp(chunkBytes, MIN_CHUNK_BYTES, MAX_CHUNK_BYTES);
        int totalChunks = (safeSize + safeChunk - 1) / safeChunk;
        if (totalChunks > MAX_CHUNKS) {
            safeChunk = (safeSize + MAX_CHUNKS - 1) / MAX_CHUNKS;
            safeChunk = clamp(safeChunk, MIN_CHUNK_BYTES, MAX_CHUNK_BYTES);
            totalChunks = (safeSize + safeChunk - 1) / safeChunk;
        }
        if (totalChunks > MAX_CHUNKS) {
            Log.w(TAG, "start_failed reason=too_many_chunks size=" + safeSize + " chunk=" + safeChunk);
            return;
        }

        byte[] data = makeTestData(fileSerial, safeSize);
        startTransfer(targetShortId, data, safeChunk, Math.max(0, dropEvery), dropOnce,
                KIND_TEST, 0, "test");
    }

    private void startTransfer(long targetShortId, byte[] data, int chunkBytes, int dropEvery,
                               int dropOnce, int objectKind, long clientTag, String label) {
        int safeSize = clamp(data.length, 1, MAX_TRANSFER_BYTES);
        int safeChunk = clamp(chunkBytes, MIN_CHUNK_BYTES, MAX_CHUNK_BYTES);
        int totalChunks = (safeSize + safeChunk - 1) / safeChunk;
        if (totalChunks > MAX_CHUNKS) {
            safeChunk = (safeSize + MAX_CHUNKS - 1) / MAX_CHUNKS;
            safeChunk = clamp(safeChunk, MIN_CHUNK_BYTES, MAX_CHUNK_BYTES);
            totalChunks = (safeSize + safeChunk - 1) / safeChunk;
        }
        if (totalChunks > MAX_CHUNKS) {
            Log.w(TAG, "start_failed reason=too_many_chunks size=" + safeSize + " chunk=" + safeChunk);
            notifyTxFailed(clientTag, targetShortId, 0, "too_many_chunks");
            return;
        }

        int fileId = nextFileId(targetShortId);
        int crc = crc32(data);
        activeFrame = null;
        txQueue.clear();
        sendSession = new SendSession(targetShortId, fileId, data, safeChunk, totalChunks, crc,
                dropEvery, dropOnce, objectKind, clientTag);
        handler.removeCallbacks(senderWatchdogRunnable);

        Log.i(TAG, String.format(Locale.US,
                "start_tx file=%08X target=%s kind=%d label=%s size=%d chunk=%d chunks=%d crc=%08X drop_every=%d drop_once=%d",
                fileId, shortId(targetShortId), objectKind, label, safeSize, safeChunk, totalChunks, crc,
                sendSession.dropEvery, sendSession.dropOnce));
        notifyTxStarted(clientTag, targetShortId, fileId, objectKind, data.length, totalChunks);

        enqueueFrame(targetShortId, fileId, TYPE_START, buildStartFrame(sendSession), "START");
        sendSession.pendingInitialFrames++;
        for (int i = 0; i < totalChunks; i++) {
            if (sendSession.shouldSkipInitial(i)) {
                Log.i(TAG, String.format(Locale.US,
                        "tx_skip_initial file=%08X idx=%d", fileId, i));
                continue;
            }
            enqueueDataFrame(sendSession, i, false);
        }
        scheduleSenderWatchdog();
        drainTxQueue();
    }

    private void handleFileSendResponse(HostProtocol.HostMessage message) {
        OutboundFrame frame = activeFrame;
        if (frame == null) {
            return;
        }
        if (message.seq != frame.seq) {
            return;
        }

        if (message.status == 0) {
            markFrameDelivered(frame);
            Log.i(TAG, String.format(Locale.US,
                    "tx_frame_ok file=%08X type=%s seq=%d remaining=%d",
                    frame.fileId, frame.typeName, frame.seq, txQueue.size()));
            activeFrame = null;
            handler.postDelayed(drainRunnable, SEND_GAP_MS);
            return;
        }

        if (message.status == 3 && frame.retries < SEND_RETRY_MAX) {
            frame.retries++;
            Log.i(TAG, String.format(Locale.US,
                    "tx_frame_busy file=%08X type=%s seq=%d retry=%d",
                    frame.fileId, frame.typeName, frame.seq, frame.retries));
            handler.postDelayed(this::sendActiveFrame, SEND_RETRY_DELAY_MS);
            return;
        }

        Log.w(TAG, String.format(Locale.US,
                "tx_frame_fail file=%08X type=%s seq=%d status=%d retry=%d",
                frame.fileId, frame.typeName, frame.seq, message.status, frame.retries));
        activeFrame = null;
        if (sendSession != null && frame.fileId == sendSession.fileId) {
            Log.w(TAG, String.format(Locale.US, "tx_abort file=%08X reason=host_status_%d",
                    sendSession.fileId, message.status));
            notifyTxFailed(sendSession.clientTag, sendSession.targetShortId,
                    sendSession.fileId, "host_status_" + message.status);
            sendSession = null;
            txQueue.clear();
            handler.removeCallbacks(senderWatchdogRunnable);
        }
        drainTxQueue();
    }

    private void handleFileEvent(byte[] payload) {
        HostProtocol.FileEvent event;
        FileFrame frame;
        try {
            event = HostProtocol.parseFileEvent(payload);
            if (event.isTruncated()) {
                Log.w(TAG, "rx_drop reason=truncated src=" + shortId(event.shortId)
                        + " declared=" + event.declaredFrameLen + " got=" + event.frame.length);
                return;
            }
            frame = parseFrame(event.frame);
        } catch (RuntimeException e) {
            Log.w(TAG, "rx_drop reason=parse_error " + e.getMessage());
            return;
        }

        switch (frame.type) {
            case TYPE_START:
                onStartFrame(event.shortId, event.rssi, frame);
                break;
            case TYPE_DATA:
                onDataFrame(event.shortId, frame);
                break;
            case TYPE_REPORT:
                onReportFrame(event.shortId, frame);
                break;
            case TYPE_DONE:
                Log.i(TAG, String.format(Locale.US,
                        "peer_done file=%08X src=%s status=%d size=%d crc=%08X",
                        frame.fileId, shortId(event.shortId), frame.status,
                        frame.totalLen, frame.crc32));
                if (sendSession != null && sendSession.fileId == frame.fileId && frame.status == 0) {
                    SendSession completed = sendSession;
                    clearQueuedFramesForFile(frame.fileId);
                    sendSession = null;
                    handler.removeCallbacks(senderWatchdogRunnable);
                    Log.i(TAG, String.format(Locale.US,
                            "tx_complete_by_done file=%08X size=%d crc=%08X",
                            frame.fileId, frame.totalLen, frame.crc32));
                    notifyTxCompleted(completed.clientTag, completed.targetShortId,
                            completed.fileId, completed.objectKind, completed.data.length);
                }
                break;
            case TYPE_CANCEL:
                Log.i(TAG, String.format(Locale.US,
                        "peer_cancel file=%08X src=%s status=%d",
                        frame.fileId, shortId(event.shortId), frame.status));
                break;
            default:
                Log.w(TAG, String.format(Locale.US,
                        "rx_drop reason=unknown_type type=%d file=%08X", frame.type, frame.fileId));
                break;
        }
    }

    private void onStartFrame(long srcShortId, int rssi, FileFrame frame) {
        if (frame.totalLen <= 0 || frame.totalLen > MAX_TRANSFER_BYTES ||
                frame.chunkSize <= 0 || frame.chunkSize > MAX_CHUNK_BYTES ||
                frame.totalChunks <= 0 || frame.totalChunks > MAX_CHUNKS) {
            Log.w(TAG, String.format(Locale.US,
                    "rx_start_reject file=%08X src=%s size=%d chunk=%d chunks=%d",
                    frame.fileId, shortId(srcShortId), frame.totalLen, frame.chunkSize, frame.totalChunks));
            return;
        }
        receiveSession = new ReceiveSession(srcShortId, frame.fileId, frame.totalLen,
                frame.chunkSize, frame.totalChunks, frame.crc32, frame.objectKind);
        Log.i(TAG, String.format(Locale.US,
                "rx_start file=%08X src=%s rssi=%d kind=%d size=%d chunk=%d chunks=%d crc=%08X",
                frame.fileId, shortId(srcShortId), rssi, frame.objectKind, frame.totalLen, frame.chunkSize,
                frame.totalChunks, frame.crc32));
        notifyRxStarted(srcShortId, frame.fileId, frame.objectKind, frame.totalLen, frame.totalChunks);
        scheduleReport(REPORT_DELAY_MS);
    }

    private void onDataFrame(long srcShortId, FileFrame frame) {
        ReceiveSession rx = receiveSession;
        if (rx == null || rx.fileId != frame.fileId || rx.srcShortId != srcShortId) {
            Log.w(TAG, String.format(Locale.US,
                    "rx_data_orphan file=%08X src=%s idx=%d",
                    frame.fileId, shortId(srcShortId), frame.chunkIndex));
            return;
        }
        if (frame.chunkIndex < 0 || frame.chunkIndex >= rx.totalChunks || frame.data == null) {
            Log.w(TAG, String.format(Locale.US,
                    "rx_data_reject file=%08X idx=%d", frame.fileId, frame.chunkIndex));
            return;
        }
        if (rx.completed) {
            return;
        }
        int offset = frame.chunkIndex * rx.chunkSize;
        int copyLen = Math.min(frame.data.length, rx.totalLen - offset);
        if (copyLen <= 0) {
            return;
        }
        if (!rx.received[frame.chunkIndex]) {
            System.arraycopy(frame.data, 0, rx.data, offset, copyLen);
            rx.received[frame.chunkIndex] = true;
            rx.receivedCount++;
            rx.receivedBytes += copyLen;
        }
        Log.i(TAG, String.format(Locale.US,
                "rx_data file=%08X idx=%d len=%d received=%d/%d",
                frame.fileId, frame.chunkIndex, copyLen, rx.receivedCount, rx.totalChunks));
        if (rx.receivedCount >= rx.totalChunks) {
            completeReceive(rx);
        } else if ((rx.receivedCount % 8) == 0) {
            scheduleReport(REPORT_DELAY_MS);
        }
    }

    private void onReportFrame(long srcShortId, FileFrame frame) {
        SendSession session = sendSession;
        if (session == null || session.fileId != frame.fileId || session.targetShortId != srcShortId) {
            Log.i(TAG, String.format(Locale.US,
                    "report_ignore file=%08X src=%s", frame.fileId, shortId(srcShortId)));
            return;
        }

        int missing = 0;
        int limit = Math.min(session.totalChunks, frame.missingBitmap == null ? 0 : frame.missingBitmap.length * 8);
        for (int i = 0; i < limit; i++) {
            if (bitmapBit(frame.missingBitmap, i)) {
                missing++;
            }
        }
        Log.i(TAG, String.format(Locale.US,
                "report_rx file=%08X src=%s received=%d/%d missing=%d",
                frame.fileId, shortId(srcShortId), frame.receivedCount, frame.totalChunks, missing));

        if (missing == 0 && frame.receivedCount >= session.totalChunks) {
            SendSession completed = session;
            clearQueuedFramesForFile(session.fileId);
            enqueueFrame(srcShortId, session.fileId, TYPE_DONE, buildDoneFrame(session, 0), "DONE");
            Log.i(TAG, String.format(Locale.US,
                    "tx_complete file=%08X size=%d crc=%08X",
                    session.fileId, session.data.length, session.crc32));
            sendSession = null;
            handler.removeCallbacks(senderWatchdogRunnable);
            notifyTxCompleted(completed.clientTag, completed.targetShortId,
                    completed.fileId, completed.objectKind, completed.data.length);
            drainTxQueue();
            return;
        }

        if (session.pendingInitialFrames > 0 || session.pendingRepairFrames > 0) {
            Log.i(TAG, String.format(Locale.US,
                    "report_defer file=%08X initial=%d repair=%d missing=%d",
                    session.fileId, session.pendingInitialFrames, session.pendingRepairFrames, missing));
            scheduleSenderWatchdog();
            return;
        }

        if (session.repairRound >= 8) {
            Log.w(TAG, String.format(Locale.US,
                    "tx_abort file=%08X reason=too_many_repairs missing=%d",
                    session.fileId, missing));
            notifyTxFailed(session.clientTag, session.targetShortId,
                    session.fileId, "too_many_repairs");
            sendSession = null;
            clearQueuedFramesForFile(frame.fileId);
            handler.removeCallbacks(senderWatchdogRunnable);
            return;
        }
        session.repairRound++;
        for (int i = 0; i < limit; i++) {
            if (bitmapBit(frame.missingBitmap, i)) {
                enqueueDataFrame(session, i, true);
            }
        }
        Log.i(TAG, String.format(Locale.US,
                "repair file=%08X round=%d missing=%d",
                session.fileId, session.repairRound, missing));
        scheduleSenderWatchdog();
        drainTxQueue();
    }

    private void completeReceive(ReceiveSession rx) {
        if (rx.completed) {
            return;
        }
        rx.completed = true;
        int gotCrc = crc32(rx.data);
        boolean ok = gotCrc == rx.crc32;
        Log.i(TAG, String.format(Locale.US,
                "rx_complete file=%08X size=%d crc=%08X expected=%08X ok=%d",
                rx.fileId, rx.totalLen, gotCrc, rx.crc32, ok ? 1 : 0));
        notifyRxCompleted(rx.srcShortId, rx.fileId, rx.objectKind, rx.data, ok);
        sendReport(rx);
        enqueueFrame(rx.srcShortId, rx.fileId, TYPE_DONE,
                buildDoneFrame(rx.fileId, rx.totalLen, gotCrc, ok ? 0 : 1), "DONE");
        drainTxQueue();
    }

    private void enqueueDataFrame(SendSession session, int chunkIndex, boolean repair) {
        if (repair) {
            session.pendingRepairFrames++;
        } else {
            session.pendingInitialFrames++;
        }
        enqueueFrame(session.targetShortId, session.fileId, TYPE_DATA,
                buildDataFrame(session, chunkIndex), repair ? "DATA_REPAIR" : "DATA");
    }

    private void markFrameDelivered(OutboundFrame frame) {
        SendSession session = sendSession;
        if (session == null || frame == null || session.fileId != frame.fileId) {
            return;
        }
        if ("START".equals(frame.typeName) || "DATA".equals(frame.typeName)) {
            session.pendingInitialFrames = Math.max(0, session.pendingInitialFrames - 1);
        } else if ("DATA_REPAIR".equals(frame.typeName)) {
            session.pendingRepairFrames = Math.max(0, session.pendingRepairFrames - 1);
        }
    }

    private void enqueueFrame(long targetShortId, int fileId, int type, byte[] frame, String typeName) {
        txQueue.add(new OutboundFrame(targetShortId, fileId, type, frame, typeName));
    }

    private void clearQueuedFramesForFile(int fileId) {
        txQueue.removeIf(frame -> frame.fileId == fileId);
    }

    private void drainTxQueue() {
        if (activeFrame != null || txQueue.isEmpty()) {
            return;
        }
        activeFrame = txQueue.poll();
        sendActiveFrame();
    }

    private void sendActiveFrame() {
        OutboundFrame frame = activeFrame;
        if (frame == null) {
            return;
        }
        if (!bleClient.isDebugReady()) {
            Log.w(TAG, String.format(Locale.US,
                    "tx_frame_drop file=%08X type=%s reason=not_ready",
                    frame.fileId, frame.typeName));
            activeFrame = null;
            return;
        }
        try {
            frame.seq = bleClient.sendP2pFileFrame(frame.targetShortId, frame.frame);
            Log.i(TAG, String.format(Locale.US,
                    "tx_frame file=%08X type=%s seq=%d len=%d target=%s queued=%d",
                    frame.fileId, frame.typeName, frame.seq, frame.frame.length,
                    shortId(frame.targetShortId), txQueue.size()));
        } catch (RuntimeException e) {
            Log.w(TAG, String.format(Locale.US,
                    "tx_frame_error file=%08X type=%s error=%s",
                    frame.fileId, frame.typeName, e.getMessage()));
            activeFrame = null;
        }
    }

    private void scheduleReport(long delayMs) {
        handler.removeCallbacks(reportRunnable);
        handler.postDelayed(reportRunnable, delayMs);
    }

    private void sendPendingReport() {
        ReceiveSession rx = receiveSession;
        if (rx == null) {
            return;
        }
        sendReport(rx);
        if (rx.receivedCount < rx.totalChunks) {
            scheduleReport(2000);
        }
        drainTxQueue();
    }

    private void sendReport(ReceiveSession rx) {
        int missing = rx.totalChunks - rx.receivedCount;
        Log.i(TAG, String.format(Locale.US,
                "report_tx file=%08X dst=%s received=%d/%d missing=%d",
                rx.fileId, shortId(rx.srcShortId), rx.receivedCount, rx.totalChunks, missing));
        enqueueFrame(rx.srcShortId, rx.fileId, TYPE_REPORT, buildReportFrame(rx), "REPORT");
    }

    private void scheduleSenderWatchdog() {
        handler.removeCallbacks(senderWatchdogRunnable);
        handler.postDelayed(senderWatchdogRunnable, REPORT_TIMEOUT_MS);
    }

    private void onSenderWatchdog() {
        SendSession session = sendSession;
        if (session == null) {
            return;
        }
        if (activeFrame != null || !txQueue.isEmpty()) {
            scheduleSenderWatchdog();
            return;
        }
        if (session.watchdogResends >= WATCHDOG_RESEND_MAX) {
            Log.w(TAG, String.format(Locale.US,
                    "tx_abort file=%08X reason=report_timeout", session.fileId));
            notifyTxFailed(session.clientTag, session.targetShortId,
                    session.fileId, "report_timeout");
            sendSession = null;
            return;
        }

        session.watchdogResends++;
        Log.w(TAG, String.format(Locale.US,
                "tx_watchdog_resend file=%08X round=%d", session.fileId, session.watchdogResends));
        enqueueFrame(session.targetShortId, session.fileId, TYPE_START, buildStartFrame(session), "START_RETRY");
        for (int i = 0; i < session.totalChunks; i++) {
            enqueueDataFrame(session, i, true);
        }
        scheduleSenderWatchdog();
        drainTxQueue();
    }

    private byte[] buildStartFrame(SendSession session) {
        byte[] frame = new byte[21];
        writeHeader(frame, TYPE_START, session.fileId);
        wr32(frame, 8, session.data.length);
        wr16(frame, 12, session.chunkSize);
        wr16(frame, 14, session.totalChunks);
        wr32(frame, 16, session.crc32);
        frame[20] = (byte) session.objectKind;
        return frame;
    }

    private byte[] buildDataFrame(SendSession session, int chunkIndex) {
        int offset = chunkIndex * session.chunkSize;
        int len = Math.min(session.chunkSize, session.data.length - offset);
        byte[] frame = new byte[10 + len];
        writeHeader(frame, TYPE_DATA, session.fileId);
        wr16(frame, 8, chunkIndex);
        System.arraycopy(session.data, offset, frame, 10, len);
        return frame;
    }

    private byte[] buildReportFrame(ReceiveSession rx) {
        int bitmapLen = (rx.totalChunks + 7) / 8;
        byte[] frame = new byte[15 + bitmapLen];
        writeHeader(frame, TYPE_REPORT, rx.fileId);
        wr16(frame, 8, rx.totalChunks);
        wr16(frame, 10, rx.receivedCount);
        wr16(frame, 12, 0);
        frame[14] = (byte) bitmapLen;
        for (int i = 0; i < rx.totalChunks; i++) {
            if (!rx.received[i]) {
                setBitmapBit(frame, 15, i);
            }
        }
        return frame;
    }

    private byte[] buildDoneFrame(SendSession session, int status) {
        return buildDoneFrame(session.fileId, session.data.length, session.crc32, status);
    }

    private byte[] buildDoneFrame(int fileId, int totalLen, int crc, int status) {
        byte[] frame = new byte[17];
        writeHeader(frame, TYPE_DONE, fileId);
        frame[8] = (byte) status;
        wr32(frame, 9, totalLen);
        wr32(frame, 13, crc);
        return frame;
    }

    private FileFrame parseFrame(byte[] frame) {
        if (frame == null || frame.length < 8) {
            throw new IllegalArgumentException("short file frame");
        }
        if (u8(frame[0]) != MAGIC_G || u8(frame[1]) != MAGIC_F || u8(frame[2]) != VERSION) {
            throw new IllegalArgumentException("bad file frame header");
        }
        int type = u8(frame[3]);
        int fileId = rd32(frame, 4);
        FileFrame out = new FileFrame(type, fileId);
        switch (type) {
            case TYPE_START:
                if (frame.length < 21) {
                    throw new IllegalArgumentException("short START");
                }
                out.totalLen = rd32(frame, 8);
                out.chunkSize = rd16(frame, 12);
                out.totalChunks = rd16(frame, 14);
                out.crc32 = rd32(frame, 16);
                out.objectKind = u8(frame[20]);
                break;
            case TYPE_DATA:
                if (frame.length < 10) {
                    throw new IllegalArgumentException("short DATA");
                }
                out.chunkIndex = rd16(frame, 8);
                out.data = Arrays.copyOfRange(frame, 10, frame.length);
                break;
            case TYPE_REPORT:
                if (frame.length < 15) {
                    throw new IllegalArgumentException("short REPORT");
                }
                out.totalChunks = rd16(frame, 8);
                out.receivedCount = rd16(frame, 10);
                out.bitmapBase = rd16(frame, 12);
                int bitmapLen = u8(frame[14]);
                if (frame.length < 15 + bitmapLen) {
                    throw new IllegalArgumentException("bad REPORT bitmap");
                }
                out.missingBitmap = Arrays.copyOfRange(frame, 15, 15 + bitmapLen);
                break;
            case TYPE_DONE:
                if (frame.length < 17) {
                    throw new IllegalArgumentException("short DONE");
                }
                out.status = u8(frame[8]);
                out.totalLen = rd32(frame, 9);
                out.crc32 = rd32(frame, 13);
                break;
            case TYPE_CANCEL:
                out.status = frame.length > 8 ? u8(frame[8]) : 0;
                break;
            default:
                break;
        }
        return out;
    }

    private void writeHeader(byte[] frame, int type, int fileId) {
        frame[0] = (byte) MAGIC_G;
        frame[1] = (byte) MAGIC_F;
        frame[2] = (byte) VERSION;
        frame[3] = (byte) type;
        wr32(frame, 4, fileId);
    }

    private int nextFileId(long targetShortId) {
        int serial = fileSerial++;
        if (fileSerial == 0x7fffffff) {
            fileSerial = 1;
        }
        return (int) (System.currentTimeMillis() ^ (targetShortId >>> 1) ^ serial);
    }

    private byte[] makeTestData(int fileId, int size) {
        byte[] data = new byte[size];
        for (int i = 0; i < data.length; i++) {
            data[i] = (byte) (fileId + (i * 31) + (i >>> 3));
        }
        return data;
    }

    private int crc32(byte[] data) {
        CRC32 crc = new CRC32();
        crc.update(data, 0, data.length);
        return (int) crc.getValue();
    }

    private static boolean bitmapBit(byte[] bitmap, int index) {
        if (bitmap == null || index < 0 || index / 8 >= bitmap.length) {
            return false;
        }
        return (bitmap[index / 8] & (1 << (index & 7))) != 0;
    }

    private static void setBitmapBit(byte[] data, int offset, int index) {
        data[offset + (index / 8)] |= (byte) (1 << (index & 7));
    }

    private static int clamp(int value, int min, int max) {
        return Math.max(min, Math.min(max, value));
    }

    private static int u8(byte value) {
        return value & 0xFF;
    }

    private static int rd16(byte[] data, int offset) {
        return u8(data[offset]) | (u8(data[offset + 1]) << 8);
    }

    private static int rd32(byte[] data, int offset) {
        return u8(data[offset])
                | (u8(data[offset + 1]) << 8)
                | (u8(data[offset + 2]) << 16)
                | (u8(data[offset + 3]) << 24);
    }

    private static void wr16(byte[] data, int offset, int value) {
        data[offset] = (byte) value;
        data[offset + 1] = (byte) (value >> 8);
    }

    private static void wr32(byte[] data, int offset, int value) {
        data[offset] = (byte) value;
        data[offset + 1] = (byte) (value >> 8);
        data[offset + 2] = (byte) (value >> 16);
        data[offset + 3] = (byte) (value >> 24);
    }

    private static String shortId(long value) {
        return String.format(Locale.US, "%08X", value & 0xffffffffL);
    }

    private void notifyTxStarted(long clientTag, long targetShortId, int fileId,
                                 int objectKind, int totalLen, int totalChunks) {
        if (listener != null) {
            listener.onFileTxStarted(clientTag, targetShortId, fileId,
                    objectKind, totalLen, totalChunks);
        }
    }

    private void notifyTxCompleted(long clientTag, long targetShortId, int fileId,
                                   int objectKind, int totalLen) {
        if (listener != null) {
            listener.onFileTxCompleted(clientTag, targetShortId, fileId, objectKind, totalLen);
        }
    }

    private void notifyTxFailed(long clientTag, long targetShortId, int fileId, String reason) {
        if (listener != null) {
            listener.onFileTxFailed(clientTag, targetShortId, fileId, reason);
        }
    }

    private void notifyRxStarted(long srcShortId, int fileId, int objectKind,
                                 int totalLen, int totalChunks) {
        if (listener != null) {
            listener.onFileRxStarted(srcShortId, fileId, objectKind, totalLen, totalChunks);
        }
    }

    private void notifyRxCompleted(long srcShortId, int fileId, int objectKind,
                                   byte[] data, boolean ok) {
        if (listener != null) {
            listener.onFileRxCompleted(srcShortId, fileId, objectKind, data, ok);
        }
    }

    interface Listener {
        void onFileTxStarted(long clientTag, long targetShortId, int fileId,
                             int objectKind, int totalLen, int totalChunks);

        void onFileTxCompleted(long clientTag, long targetShortId, int fileId,
                               int objectKind, int totalLen);

        void onFileTxFailed(long clientTag, long targetShortId, int fileId, String reason);

        void onFileRxStarted(long srcShortId, int fileId, int objectKind,
                             int totalLen, int totalChunks);

        void onFileRxCompleted(long srcShortId, int fileId, int objectKind,
                               byte[] data, boolean ok);
    }

    private static final class OutboundFrame {
        final long targetShortId;
        final int fileId;
        final int type;
        final byte[] frame;
        final String typeName;
        int seq;
        int retries;

        OutboundFrame(long targetShortId, int fileId, int type, byte[] frame, String typeName) {
            this.targetShortId = targetShortId;
            this.fileId = fileId;
            this.type = type;
            this.frame = frame;
            this.typeName = typeName;
        }
    }

    private static final class SendSession {
        final long targetShortId;
        final int fileId;
        final byte[] data;
        final int chunkSize;
        final int totalChunks;
        final int crc32;
        final int dropEvery;
        final int dropOnce;
        final int objectKind;
        final long clientTag;
        int pendingInitialFrames;
        int pendingRepairFrames;
        int repairRound;
        int watchdogResends;

        SendSession(long targetShortId, int fileId, byte[] data, int chunkSize,
                    int totalChunks, int crc32, int dropEvery, int dropOnce,
                    int objectKind, long clientTag) {
            this.targetShortId = targetShortId;
            this.fileId = fileId;
            this.data = data;
            this.chunkSize = chunkSize;
            this.totalChunks = totalChunks;
            this.crc32 = crc32;
            this.dropEvery = dropEvery;
            this.dropOnce = dropOnce;
            this.objectKind = objectKind;
            this.clientTag = clientTag;
        }

        boolean shouldSkipInitial(int index) {
            if (index == dropOnce) {
                return true;
            }
            return dropEvery > 0 && index > 0 && (index % dropEvery) == 0;
        }
    }

    private static final class TestRequest {
        final long targetShortId;
        final int size;
        final int chunkBytes;
        final int dropEvery;
        final int dropOnce;

        TestRequest(long targetShortId, int size, int chunkBytes, int dropEvery, int dropOnce) {
            this.targetShortId = targetShortId;
            this.size = size;
            this.chunkBytes = chunkBytes;
            this.dropEvery = dropEvery;
            this.dropOnce = dropOnce;
        }
    }

    private static final class ReceiveSession {
        final long srcShortId;
        final int fileId;
        final int totalLen;
        final int chunkSize;
        final int totalChunks;
        final int crc32;
        final int objectKind;
        final byte[] data;
        final boolean[] received;
        int receivedCount;
        int receivedBytes;
        boolean completed;

        ReceiveSession(long srcShortId, int fileId, int totalLen, int chunkSize,
                       int totalChunks, int crc32, int objectKind) {
            this.srcShortId = srcShortId;
            this.fileId = fileId;
            this.totalLen = totalLen;
            this.chunkSize = chunkSize;
            this.totalChunks = totalChunks;
            this.crc32 = crc32;
            this.objectKind = objectKind;
            this.data = new byte[totalLen];
            this.received = new boolean[totalChunks];
        }
    }

    private static final class FileFrame {
        final int type;
        final int fileId;
        int totalLen;
        int chunkSize;
        int totalChunks;
        int crc32;
        int objectKind;
        int chunkIndex;
        byte[] data;
        int receivedCount;
        int bitmapBase;
        byte[] missingBitmap;
        int status;

        FileFrame(int type, int fileId) {
            this.type = type;
            this.fileId = fileId;
        }
    }
}
