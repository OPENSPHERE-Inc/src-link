/*
SRC-Link
Copyright (C) 2024 OPENSPHERE Inc. info@opensphere.co.jp

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <cstring>

#include <QCoreApplication>
#include <QDateTime>
#include <obs-module.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include "ws-client.hpp"
#include "linux-ca-locations.hpp"
#include "../plugin-support.h"

namespace {
// Mirrors CurlHttpClient::MAX_RESPONSE_SIZE to bound peer-controlled buffer growth.
static constexpr qsizetype MAX_WS_MESSAGE_SIZE = 16 * 1024 * 1024;

bool containsCrlfOrNul(const QByteArray &data)
{
    return data.contains('\r') || data.contains('\n') || data.contains('\0');
}
} // namespace

WsClient::WsClient(QObject *parent)
    : QObject(parent),
      easy(nullptr),
      recvNotifier(nullptr),
      tlsFallbackTimer(new QTimer(this)),
      headerList(nullptr),
      connected(false),
      connecting(false),
      abortConnect(false),
      fragmentType(0),
      frameInProgress(false),
      pingTimestamp(0)
{
    obs_log(LOG_DEBUG, "WsClient created");

    // Low-frequency fallback timer to recover from TLS buffer stall.
    // When mbedtls reads a TLS record containing multiple WebSocket messages,
    // the OS socket may not be readable even though curl has buffered data.
    // QSocketNotifier won't fire in that case, so this timer ensures recovery.
    // 250ms keeps stall recovery well within mbedtls' practical stall window
    // while reducing UI wakeups by 2.5x compared to 100ms.
    tlsFallbackTimer->setInterval(250);
    connect(tlsFallbackTimer, &QTimer::timeout, this, &WsClient::pollRecv);
}

WsClient::~WsClient()
{
    obs_log(LOG_DEBUG, "WsClient destroying");
    abortConnect = true;
    close();
    joinConnectThread();
    cleanup(); // Recover resources if close() early-returned during connecting state
    // Drop any queued lambdas targeting this receiver before the QObject vtable goes away.
    QCoreApplication::removePostedEvents(this);
}

void WsClient::setHeaders(const QMap<QByteArray, QByteArray> &headers)
{
    Q_ASSERT(!connecting && !connected);
    requestHeaders = headers;
}

void WsClient::open(const QUrl &url)
{
    // close() may early-return with connecting==false while connectThread is still joinable
    // (abort path, see close()). Reopening before the worker exits would assign over a
    // joinable std::thread and call std::terminate(), so guard on joinable() too.
    if (connected || connecting || connectThread.joinable()) {
        return;
    }

    cleanup(); // Ensure any stale easy handle from a previous aborted connection is freed

    abortConnect = false;
    connecting = true;
    pendingUrl = url;

    // Defensive: ensure no stale slist leaks if cleanup() ever stops being the prerequisite.
    if (headerList) {
        curl_slist_free_all(headerList);
        headerList = nullptr;
    }

    // Build curl_slist from requestHeaders
    struct curl_slist *slist = nullptr;
    for (auto it = requestHeaders.constBegin(); it != requestHeaders.constEnd(); ++it) {
        if (containsCrlfOrNul(it.key()) || containsCrlfOrNul(it.value())) {
            obs_log(
                LOG_ERROR, "WsClient: Refusing to send upgrade with CR/LF/NUL in header (key=%s)", it.key().constData()
            );
            curl_slist_free_all(slist);
            connecting = false;
            emit errorOccurred("Invalid characters in WebSocket request header");
            return;
        }
        QByteArray header = it.key() + ": " + it.value();
        struct curl_slist *next = curl_slist_append(slist, header.constData());
        if (!next) {
            obs_log(LOG_ERROR, "WsClient: curl_slist_append failed for header '%s'", it.key().constData());
            curl_slist_free_all(slist);
            connecting = false;
            emit errorOccurred("Failed to build WebSocket request headers");
            return;
        }
        slist = next;
    }
    // Some servers require an Origin header for the upgrade handshake.
    if (!requestHeaders.contains("Origin")) {
        const QString urlScheme = url.scheme().toLower();
        QString originScheme;
        if (urlScheme == "wss") {
            originScheme = "https";
        } else {
            originScheme = "http";
        }
        // RFC 6454 §6.1: omit the port when it matches the scheme default.
        const int port = url.port();
        QString origin;
        if ((port == -1) || (port == 80 && (urlScheme == "ws" || urlScheme == "http")) ||
            (port == 443 && (urlScheme == "wss" || urlScheme == "https"))) {
            origin = QString("%1://%2").arg(originScheme, url.host());
        } else {
            origin = QString("%1://%2:%3").arg(originScheme, url.host()).arg(port);
        }
        QByteArray originLine = "Origin: " + origin.toUtf8();
        struct curl_slist *next = curl_slist_append(slist, originLine.constData());
        if (!next) {
            obs_log(LOG_ERROR, "WsClient: curl_slist_append failed for Origin header");
            curl_slist_free_all(slist);
            connecting = false;
            emit errorOccurred("Failed to build WebSocket request headers");
            return;
        }
        slist = next;
    }
    headerList = slist;

    performConnect();
}

void WsClient::performConnect()
{
    easy = curl_easy_init();
    if (!easy) {
        connecting = false;
        emit errorOccurred("Failed to init curl");
        return;
    }

    auto urlStr = pendingUrl.toEncoded();
    obs_log(LOG_DEBUG, "WsClient connecting to %s", urlStr.constData());

    curl_easy_setopt(easy, CURLOPT_URL, urlStr.constData());
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(easy, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2 | CURL_SSLVERSION_MAX_TLSv1_3);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    // TCP keepalive for dead peer detection (NAT timeout, cable unplug).
    // 30s/15s is shorter than HTTP's 60s/30s: long-lived WebSocket sessions need quicker
    // idle detection so reconnect logic kicks in before NAT entries expire.
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPINTVL, 15L);
#if defined(_WIN32) || defined(__APPLE__)
    // Use OS native CA store (Windows: Schannel, macOS: Secure Transport)
    curl_easy_setopt(easy, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#elif defined(__linux__)
    // Linux: libcurl is built against mbedtls in obs-deps, which has no compiled-in CA path.
    // Probe well-known distro locations and use the first one present so server cert
    // verification works on Debian, Ubuntu, RHEL, Fedora, Alpine, etc.
    if (auto ca = findLinuxCaLocation()) {
        if (ca->isDirectory) {
            curl_easy_setopt(easy, CURLOPT_CAPATH, ca->path);
        } else {
            curl_easy_setopt(easy, CURLOPT_CAINFO, ca->path);
        }
    } else {
        obs_log(LOG_WARNING, "WsClient: No system CA bundle found; TLS verification will likely fail");
    }
#endif
    // Enable WebSocket upgrade (CONNECT_ONLY=2 performs HTTP upgrade handshake only)
    curl_easy_setopt(easy, CURLOPT_CONNECT_ONLY, 2L);
    // Allow close()/~WsClient() to abort an in-flight curl_easy_perform()
    // via the abortConnect flag (returns CURLE_ABORTED_BY_CALLBACK).
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, xferInfoCallback);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, this);

    // Run TLS handshake + HTTP upgrade on background thread to avoid blocking UI.
    // joinConnectThread() blocks until curl returns; close() sets abortConnect to unwind quickly.
    // `easy` is value-captured so a UI-thread cleanup() nulling the member cannot race
    // with curl_easy_perform(). `QPointer` guards the queued lambda against `this` destruction.
    // connecting / connectGeneration: UI-thread only. abortConnect: UI -> worker bridge (read in xferInfoCallback).
    unsigned int gen = ++connectGeneration;
    CURL *easyLocal = easy;
    QPointer<WsClient> guard(this);
    connectThread = std::thread([guard, easyLocal, gen]() {
        CURLcode res = curl_easy_perform(easyLocal);
        // QPointer contract: guard.data() is the receiver, but its dereference happens
        // lazily inside the queued lambda on the UI thread, after dtor's joinConnectThread()
        // ensures the worker has finished.
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, res, gen]() {
                if (!guard) {
                    return;
                }
                guard->onConnectFinished(res, gen);
            },
            Qt::QueuedConnection
        );
    });
}

void WsClient::onConnectFinished(CURLcode result, unsigned int generation)
{
    joinConnectThread();

    if (generation != connectGeneration) {
        // Stale callback from a superseded connection attempt.
        // The easy handle was already cleaned up by the new open() call.
        return;
    }

    if (!connecting) {
        // close() was called while connecting — cleanup and notify consumers (R5-2)
        cleanup();
        emit closed();
        return;
    }

    connecting = false;

    if (result != CURLE_OK) {
        obs_log(LOG_WARNING, "WsClient connect failed: %s", curl_easy_strerror(result));
        emit errorOccurred(QString("WebSocket connect failed: %1").arg(curl_easy_strerror(result)));
        cleanup();
        return;
    }

    connected = true;
    startRecvNotifier();
    obs_log(LOG_DEBUG, "WsClient connected to %s", qUtf8Printable(pendingUrl.toString()));
    emit opened();
}

void WsClient::startRecvNotifier()
{
    curl_socket_t sockfd = CURL_SOCKET_BAD;
    curl_easy_getinfo(easy, CURLINFO_ACTIVESOCKET, &sockfd);

    if (sockfd == CURL_SOCKET_BAD) {
        obs_log(LOG_ERROR, "WsClient: Failed to get active socket");
        connected = false;
        cleanup();
        emit errorOccurred("Failed to get WebSocket socket");
        return;
    }

    recvNotifier = new QSocketNotifier(static_cast<qintptr>(sockfd), QSocketNotifier::Read, this);
    connect(recvNotifier, &QSocketNotifier::activated, this, &WsClient::pollRecv);
    tlsFallbackTimer->start();
}

void WsClient::joinConnectThread()
{
    if (connectThread.joinable()) {
        connectThread.join();
    }
}

void WsClient::close()
{
    if (connecting) {
        // FIXME: ~WsClient → close() → joinConnectThread() may block the UI for up to
        // CURLOPT_CONNECTTIMEOUT_MS during a TLS handshake (xferInfoCallback is not
        // guaranteed to fire mid-handshake). Add a shutdown-only shortened connect
        // timeout and document the easy-handle reader/writer invariant in a separate PR.
        abortConnect = true;
        connecting = false;
        return;
    }

    if (connected) {
        obs_log(LOG_DEBUG, "WsClient closing connection");
        // RFC 6455: send close frame with status code 1000 (Normal Closure).
        // shuttingDown=true => single-shot send, no select() retry — keeps OBS shutdown snappy.
        uint16_t code = htons(1000);
        qint64 sent = sendRaw(reinterpret_cast<const char *>(&code), 2, CURLWS_CLOSE, /*shuttingDown=*/true);
        if (sent < 0) {
            obs_log(LOG_DEBUG, "WsClient close frame send failed (best-effort)");
        }
        connected = false;
        cleanup();
        emit closed();
    } else {
        cleanup();
    }
}

void WsClient::cleanup()
{
    tlsFallbackTimer->stop();
    if (recvNotifier) {
        // FIXME: recvNotifier->deleteLater() races with the immediate fd close in
        // curl_easy_cleanup() below; a recycled fd may attach to the dangling notifier.
        // Introduce an is-reentrant flag to distinguish pollRecv re-entry from external
        // callers, and use synchronous delete for the latter. Defer to a separate PR.
        recvNotifier->setEnabled(false);
        recvNotifier->disconnect();
        recvNotifier->deleteLater();
        recvNotifier = nullptr;
    }
    if (easy) {
        curl_easy_cleanup(easy);
        easy = nullptr;
    }
    if (headerList) {
        curl_slist_free_all(headerList);
        headerList = nullptr;
    }
    fragmentBuffer.clear();
    fragmentType = 0;
    frameInProgress = false;
    pingPayloadBuffer.clear();
}

bool WsClient::isValid() const
{
    return connected && easy != nullptr;
}

qint64 WsClient::sendTextMessage(const QString &message)
{
    if (!isValid()) {
        return -1;
    }

    auto utf8 = message.toUtf8();
    qint64 result = sendRaw(utf8.constData(), utf8.size(), CURLWS_TEXT);
    if (result == -1 && connected) {
        // Defer notification + close to the next event-loop iteration so we do not invoke
        // consumer slots (which may inspect this WsClient's state) while still inside this
        // method's stack frame.
        QPointer<WsClient> guard(this);
        QMetaObject::invokeMethod(
            this,
            [guard]() {
                if (!guard) {
                    return;
                }
                emit guard->errorOccurred("WebSocket text send failed");
                guard->close();
            },
            Qt::QueuedConnection
        );
    }
    return result;
}

qint64 WsClient::sendBinaryMessage(const QByteArray &data)
{
    if (!isValid()) {
        return -1;
    }

    qint64 result = sendRaw(data.constData(), data.size(), CURLWS_BINARY);
    if (result == -1 && connected) {
        QPointer<WsClient> guard(this);
        QMetaObject::invokeMethod(
            this,
            [guard]() {
                if (!guard) {
                    return;
                }
                emit guard->errorOccurred("WebSocket binary send failed");
                guard->close();
            },
            Qt::QueuedConnection
        );
    }
    return result;
}

qint64 WsClient::sendRaw(const char *data, size_t len, unsigned int flags, bool shuttingDown)
{
    // shuttingDown == true: best-effort single send; do not block the UI thread on select().
    // Otherwise: cap total UI block to ~150 ms across retries.
    const int maxSelectRetries = shuttingDown ? 0 : 3;

    size_t totalSent = 0;
    int selectRetries = 0;
    bool sentOnce = false;
    // UI thread only — no `easy` reset can race the loop iteration.
    // sentOnce ensures zero-length frames (e.g. empty PING) still complete one CURLE_OK send,
    // since the `totalSent < len` byte-progress check is trivially false for len == 0.
    while (totalSent < len || !sentOnce) {
        size_t sent = 0;
        const char *chunk = data ? data + totalSent : nullptr;
        CURLcode res = curl_ws_send(easy, chunk, len - totalSent, &sent, 0, flags);
        if (res == CURLE_AGAIN) {
            if (++selectRetries > maxSelectRetries) {
                if (!shuttingDown) {
                    obs_log(LOG_WARNING, "WsClient send: exceeded max retries waiting for writable");
                }
                return -1;
            }
            // Socket buffer full — wait for writable with brief select() then retry
            curl_socket_t sockfd = CURL_SOCKET_BAD;
            curl_easy_getinfo(easy, CURLINFO_ACTIVESOCKET, &sockfd);
            if (sockfd == CURL_SOCKET_BAD) {
                obs_log(LOG_WARNING, "WsClient send: CURLE_AGAIN but no active socket");
                return -1;
            }
            fd_set writefds;
            FD_ZERO(&writefds);
#ifndef _WIN32
            // POSIX fd_set is bit-indexed by fd value; FD_SET past FD_SETSIZE is UB.
            // Runtime check (not assert): release builds strip asserts and would invoke UB.
            if (static_cast<int>(sockfd) >= FD_SETSIZE) {
                obs_log(
                    LOG_WARNING, "WsClient: socket fd %d exceeds FD_SETSIZE (%d)", static_cast<int>(sockfd),
                    static_cast<int>(FD_SETSIZE)
                );
                return -1;
            }
#endif
            FD_SET(sockfd, &writefds);
            struct timeval tv = {0, 50000}; // 50ms
#ifdef _WIN32
            int ready = select(0, nullptr, &writefds, nullptr, &tv);
#else
            int ready = select(static_cast<int>(sockfd) + 1, nullptr, &writefds, nullptr, &tv);
#endif
            if (ready <= 0) {
                obs_log(LOG_WARNING, "WsClient send: select() timeout or error waiting for writable");
                return -1;
            }
            continue;
        }
        if (res != CURLE_OK) {
            obs_log(LOG_WARNING, "WsClient send failed: %s", curl_easy_strerror(res));
            return -1;
        }
        // Guard against an infinite loop when curl reports success with zero progress
        // on a non-empty buffer. CURLE_AGAIN already returned above, so this is the
        // pathological "OK but no bytes consumed" case.
        if (sent == 0 && len > 0) {
            obs_log(LOG_WARNING, "WsClient send: CURLE_OK with zero bytes sent on non-empty payload");
            return -1;
        }
        selectRetries = 0;
        totalSent += sent;
        sentOnce = true;
    }
    return static_cast<qint64>(totalSent);
}

void WsClient::ping()
{
    if (!isValid()) {
        return;
    }

    // sendRaw retries on CURLE_AGAIN via select(); only true failures return -1.
    // libcurl does not document curl_ws_send accepting a null buffer; pass a
    // 1-byte placeholder with len=0 so the API contract is honored.
    static const char pingPlaceholder = 0;
    qint64 result = sendRaw(&pingPlaceholder, 0, CURLWS_PING);
    if (result >= 0) {
        pingTimestamp = QDateTime::currentMSecsSinceEpoch();
    } else {
        // Mark disconnected synchronously so subsequent ping ticks short-circuit via isValid().
        connected = false;
        QPointer<WsClient> guard(this);
        QMetaObject::invokeMethod(
            this,
            [guard]() {
                if (!guard) {
                    return;
                }
                guard->cleanup();
                emit guard->closed();
            },
            Qt::QueuedConnection
        );
    }
}

void WsClient::pollRecv()
{
    if (!isValid()) {
        return;
    }

    // Disable notifier during processing to prevent re-entrant activation
    if (recvNotifier) {
        recvNotifier->setEnabled(false);
    }

    // Slots invoked via emit may delete this WsClient (e.g. consumer reacts to a final
    // message by tearing down the connection). Guard each post-emit access via QPointer
    // so we don't dereference a destroyed object.
    QPointer<WsClient> guard(this);

    char buffer[65536];
    size_t received = 0;
    const struct curl_ws_frame *meta = nullptr;
    // Tail-flush gate: only true after a final-fragment frame fully drained (bytesleft == 0,
    // no CURLWS_CONT). Prevents emitting a fragment chain that straddles poll cycles.
    bool lastFrameFinalAndComplete = false;

    while (true) {
        CURLcode res = curl_ws_recv(easy, buffer, sizeof(buffer), &received, &meta);
        if (res == CURLE_AGAIN) {
            // No data available right now
            break;
        }
        if (res != CURLE_OK) {
            // Connection error or closed
            obs_log(LOG_DEBUG, "WsClient connection lost: %s", curl_easy_strerror(res));
            connected = false;
            cleanup();
            emit closed();
            return;
        }

        if (meta->flags & CURLWS_CLOSE) {
            obs_log(LOG_DEBUG, "WsClient received close frame");
            // RFC 6455 §5.5.1: respond with a close frame to complete the handshake.
            // Echo the peer's status code when present and valid; otherwise use 1000
            // (Normal Closure). Valid range is 1000-4999 (RFC 6455 §7.4).
            // 1004/1005/1006/1015 are reserved and MUST NOT appear on the wire (§7.4.1):
            // reply with 1002 (Protocol Error) when the peer sends one.
            // A 1-byte payload is a protocol violation (§5.5.1): respond with 1002.
            // The reason string is intentionally not echoed: re-sending would require UTF-8
            // validation per §5.5.1, which adds complexity for no protocol benefit.
            uint16_t echoCode = 1000;
            if (received == 1) {
                echoCode = 1002;
            } else if (received >= 2) {
                uint16_t peerCodeBE;
                std::memcpy(&peerCodeBE, buffer, 2);
                const uint16_t peerCode = ntohs(peerCodeBE);
                const bool reserved = (peerCode == 1004 || peerCode == 1005 || peerCode == 1006 || peerCode == 1015);
                if (peerCode >= 1000 && peerCode <= 4999 && !reserved) {
                    echoCode = peerCode;
                } else {
                    echoCode = 1002;
                }
            }
            const uint16_t codeBE = htons(echoCode);
            const auto *codePayload = reinterpret_cast<const char *>(&codeBE);
            qint64 echoSent = sendRaw(codePayload, 2, CURLWS_CLOSE, /*shuttingDown=*/true);
            if (echoSent < 0) {
                obs_log(LOG_DEBUG, "WsClient close echo failed");
            }
            connected = false;
            if (!fragmentBuffer.isEmpty()) {
                obs_log(
                    LOG_INFO, "Discarding partial fragmented message (%lld bytes, type=%d) on close",
                    static_cast<long long>(fragmentBuffer.size()), static_cast<int>(fragmentType)
                );
            }
            cleanup();
            emit closed();
            return;
        }

        if (meta->flags & CURLWS_PONG) {
            if (pingTimestamp == 0) {
                // Unsolicited PONG (RFC 6455 §5.5.3) — skip RTT report.
                continue;
            }
            const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - pingTimestamp;
            pingTimestamp = 0; // reset after consuming
            emit pongReceived(static_cast<quint64>(elapsed >= 0 ? elapsed : 0));
            continue;
        }

        if (meta->flags & CURLWS_PING) {
            // RFC 6455 §5.5: control frames MUST have a payload length of 125 bytes or less and
            // MUST NOT be fragmented (§5.4). Reject oversized PINGs based on the full frame length
            // (meta->len) rather than the slice returned by this curl_ws_recv call.
            if (static_cast<size_t>(meta->len) > 125) {
                obs_log(
                    LOG_WARNING, "WsClient: PING payload exceeds 125 bytes (%zu); closing 1002",
                    static_cast<size_t>(meta->len)
                );
                const char closePayload[2] = {static_cast<char>(0x03), static_cast<char>(0xEA)}; // 1002 big-endian
                qint64 rc = sendRaw(closePayload, sizeof(closePayload), CURLWS_CLOSE, /*shuttingDown=*/true);
                if (rc < 0) {
                    obs_log(LOG_DEBUG, "WsClient: failed to send close frame (1002)");
                }
                connected = false;
                cleanup();
                emit closed();
                return;
            }
            // Accumulate the PING payload until the full frame has arrived. curl may split
            // a single frame across multiple curl_ws_recv calls; echoing a partial slice
            // would yield a malformed PONG.
            pingPayloadBuffer.append(buffer, static_cast<qsizetype>(received));
            if (meta->bytesleft != 0) {
                continue;
            }
            qint64 pongResult =
                sendRaw(pingPayloadBuffer.constData(), static_cast<size_t>(pingPayloadBuffer.size()), CURLWS_PONG);
            pingPayloadBuffer.clear();
            if (pongResult < 0) {
                connected = false;
                cleanup();
                emit closed();
                return;
            }
            continue;
        }

        // Handle text/binary frames with possible fragmentation.
        //
        // Two kinds of fragmentation exist:
        // (1) Within-frame: A single frame larger than the recv buffer (65536 bytes).
        //     curl returns CURLWS_TEXT/BINARY with meta->offset > 0 on subsequent reads.
        // (2) Multi-frame (WebSocket protocol fragmentation): First frame has TEXT/BINARY
        //     with CURLWS_CONT flag (FIN=0), continuation frames have CURLWS_CONT only.
        //     The accumulator is emitted in-loop on the final fragment (frameComplete &&
        //     !isCont). Tail-flush below is guarded by lastFrameFinalAndComplete so that
        //     a chain straddling poll cycles is preserved instead of emitted prematurely.
        bool isText = (meta->flags & CURLWS_TEXT) != 0;
        bool isBinary = (meta->flags & CURLWS_BINARY) != 0;
        bool isCont = (meta->flags & CURLWS_CONT) != 0;

        // New message starts at offset 0 of a TEXT or BINARY frame
        if ((isText || isBinary) && meta->offset == 0) {
            // Emit any pending multi-frame data before starting new message
            if (fragmentType != 0 && !fragmentBuffer.isEmpty()) {
                if (fragmentType == CURLWS_TEXT) {
                    emit textMessageReceived(QString::fromUtf8(fragmentBuffer));
                    if (!guard || !guard->easy || !guard->connected) {
                        return;
                    }
                } else {
                    emit binaryMessageReceived(fragmentBuffer);
                    if (!guard || !guard->easy || !guard->connected) {
                        return;
                    }
                }
            }
            fragmentBuffer.clear();
            fragmentType = isText ? CURLWS_TEXT : CURLWS_BINARY;
        }
        // CURLWS_CONT without TEXT/BINARY: WebSocket continuation frame.
        // fragmentType carries over from the initial TEXT/BINARY frame.

        // Bound peer-controlled buffer growth: close with status 1009 (Message Too Big).
        // Subtraction form avoids overflow on `size + received` for adversarial inputs.
        if (static_cast<qsizetype>(received) > MAX_WS_MESSAGE_SIZE - fragmentBuffer.size()) {
            obs_log(
                LOG_WARNING,
                "WsClient: message exceeds MAX_WS_MESSAGE_SIZE (have %lld bytes, +%lld incoming); aborting",
                static_cast<long long>(fragmentBuffer.size()), static_cast<long long>(received)
            );
            const char closePayload[2] = {static_cast<char>(0x03), static_cast<char>(0xF1)}; // 1009 big-endian
            qint64 rc = sendRaw(closePayload, sizeof(closePayload), CURLWS_CLOSE, /*shuttingDown=*/true);
            if (rc < 0) {
                obs_log(LOG_DEBUG, "WsClient: failed to send close frame (1009)");
            }
            connected = false;
            cleanup();
            emit closed();
            return;
        }

        fragmentBuffer.append(buffer, static_cast<qsizetype>(received));

        bool frameComplete = (static_cast<size_t>(meta->offset) + received >= static_cast<size_t>(meta->len));
        frameInProgress = !frameComplete;

        lastFrameFinalAndComplete = frameComplete && !isCont && meta->bytesleft == 0;

        if (frameComplete) {
            // For single-frame messages (no CONT flag), emit immediately.
            // For multi-frame messages (CONT flag set on first or continuation frame),
            // keep accumulating — emit after the recv loop below.
            if (!isCont) {
                if (fragmentType == CURLWS_TEXT) {
                    emit textMessageReceived(QString::fromUtf8(fragmentBuffer));
                    if (!guard || !guard->easy || !guard->connected) {
                        return;
                    }
                } else if (fragmentType != 0) {
                    emit binaryMessageReceived(fragmentBuffer);
                    if (!guard || !guard->easy || !guard->connected) {
                        return;
                    }
                }
                fragmentBuffer.clear();
                fragmentType = 0;
                lastFrameFinalAndComplete = false;
            }
        }
    }

    // Emit accumulated multi-frame WebSocket message after draining all available data.
    // Only safe to emit when the most recent frame was final (no CONT) and fully drained;
    // otherwise the chain is still in progress and may resume on the next poll cycle.
    if (lastFrameFinalAndComplete && fragmentType != 0 && !fragmentBuffer.isEmpty()) {
        if (fragmentType == CURLWS_TEXT) {
            emit textMessageReceived(QString::fromUtf8(fragmentBuffer));
            if (!guard || !guard->easy || !guard->connected) {
                return;
            }
        } else {
            emit binaryMessageReceived(fragmentBuffer);
            if (!guard || !guard->easy || !guard->connected) {
                return;
            }
        }
        fragmentBuffer.clear();
        fragmentType = 0;
    }

    // Re-enable notifier for next data arrival
    if (recvNotifier && connected) {
        recvNotifier->setEnabled(true);
    }
}

size_t WsClient::writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    Q_UNUSED(ptr);
    Q_UNUSED(userdata);
    return size * nmemb;
}

// libcurl only invokes this callback during the CONNECT_ONLY handshake phase.
// abortConnect cannot interrupt post-handshake curl_ws_send() / curl_ws_recv().
int WsClient::xferInfoCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    Q_UNUSED(dltotal);
    Q_UNUSED(dlnow);
    Q_UNUSED(ultotal);
    Q_UNUSED(ulnow);
    auto *self = static_cast<WsClient *>(clientp);
    return self && self->abortConnect.load() ? 1 : 0;
}
