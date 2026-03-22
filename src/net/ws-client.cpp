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

#include <QDateTime>
#include <obs-module.h>

#ifdef _WIN32
#include <winsock2.h>
#endif

#include "ws-client.hpp"
#include "../plugin-support.h"

WsClient::WsClient(QObject *parent)
    : QObject(parent),
      easy(nullptr),
      recvNotifier(nullptr),
      tlsFallbackTimer(new QTimer(this)),
      headerList(nullptr),
      connected(false),
      connecting(false),
      connectGeneration(0),
      fragmentType(0),
      frameInProgress(false),
      pingTimestamp(0)
{
    obs_log(LOG_DEBUG, "WsClient created");

    // Low-frequency fallback timer to recover from TLS buffer stall.
    // When mbedtls reads a TLS record containing multiple WebSocket messages,
    // the OS socket may not be readable even though curl has buffered data.
    // QSocketNotifier won't fire in that case, so this timer ensures recovery.
    tlsFallbackTimer->setInterval(100);
    connect(tlsFallbackTimer, &QTimer::timeout, this, &WsClient::pollRecv);
}

WsClient::~WsClient()
{
    obs_log(LOG_DEBUG, "WsClient destroying");
    close();
    joinConnectThread();
    cleanup(); // Recover resources if close() early-returned during connecting state
}

void WsClient::setHeaders(const QMap<QByteArray, QByteArray> &headers)
{
    requestHeaders = headers;
}

void WsClient::open(const QUrl &url)
{
    if (connected || connecting) {
        return;
    }

    joinConnectThread();
    cleanup(); // Ensure any stale easy handle from a previous aborted connection is freed

    connecting = true;
    pendingUrl = url;

    // Build curl_slist from requestHeaders
    struct curl_slist *slist = nullptr;
    for (auto it = requestHeaders.constBegin(); it != requestHeaders.constEnd(); ++it) {
        QByteArray header = it.key() + ": " + it.value();
        slist = curl_slist_append(slist, header.constData());
    }
    // Add Origin header if not explicitly set (QWebSocket sets this from the origin parameter)
    if (!requestHeaders.contains("Origin")) {
        QByteArray origin = QString("https://%1").arg(url.host()).toUtf8();
        slist = curl_slist_append(slist, QByteArray("Origin: " + origin).constData());
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
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    // TCP keepalive for dead peer detection (NAT timeout, cable unplug)
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPINTVL, 15L);
#if defined(_WIN32) || defined(__APPLE__)
    // Use OS native CA store (Windows: Schannel, macOS: Secure Transport)
    curl_easy_setopt(easy, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif
    // Enable WebSocket upgrade (CONNECT_ONLY=2 performs HTTP upgrade handshake only)
    curl_easy_setopt(easy, CURLOPT_CONNECT_ONLY, 2L);

    // Run TLS handshake + HTTP upgrade on background thread to avoid blocking UI.
    // The destructor calls joinConnectThread() which blocks for at most CURLOPT_CONNECTTIMEOUT (5s).
    unsigned int gen = ++connectGeneration;
    connectThread = std::thread([this, gen]() {
        CURLcode res = curl_easy_perform(easy);
        QMetaObject::invokeMethod(this, [this, res, gen]() { onConnectFinished(res, gen); }, Qt::QueuedConnection);
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
    tlsFallbackTimer->stop();

    if (recvNotifier) {
        recvNotifier->setEnabled(false);
        delete recvNotifier;
        recvNotifier = nullptr;
    }

    if (connecting) {
        // Signal abort — onConnectFinished will handle cleanup when the thread completes
        connecting = false;
        return;
    }

    if (connected) {
        obs_log(LOG_DEBUG, "WsClient closing connection");
        // L-9: Send close frame with status code 1000 (Normal Closure) per RFC 6455
        size_t sent = 0;
        uint16_t code = htons(1000);
        curl_ws_send(easy, reinterpret_cast<const char *>(&code), 2, &sent, 0, CURLWS_CLOSE);
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
    if (easy) {
        curl_easy_cleanup(easy);
        easy = nullptr;
    }
    if (headerList) {
        curl_slist_free_all(headerList);
        headerList = nullptr;
    }
    if (recvNotifier) {
        delete recvNotifier;
        recvNotifier = nullptr;
    }
    fragmentBuffer.clear();
    fragmentType = 0;
    frameInProgress = false;
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
        // Connection is likely broken — notify consumers to trigger reconnection
        emit errorOccurred("WebSocket text send failed");
        close();
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
        // Connection is likely broken — notify consumers to trigger reconnection
        emit errorOccurred("WebSocket binary send failed");
        close();
    }
    return result;
}

qint64 WsClient::sendRaw(const char *data, size_t len, unsigned int flags)
{
    static constexpr int MAX_SELECT_RETRIES = 3; // Cap total UI block to ~150ms

    size_t totalSent = 0;
    int selectRetries = 0;
    while (totalSent < len) {
        size_t sent = 0;
        CURLcode res = curl_ws_send(easy, data + totalSent, len - totalSent, &sent, 0, flags);
        if (res == CURLE_AGAIN) {
            if (++selectRetries > MAX_SELECT_RETRIES) {
                obs_log(LOG_WARNING, "WsClient send: exceeded max retries waiting for writable");
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
        selectRetries = 0;
        totalSent += sent;
    }
    return static_cast<qint64>(totalSent);
}

void WsClient::ping()
{
    if (!isValid()) {
        return;
    }

    pingTimestamp = QDateTime::currentMSecsSinceEpoch();
    size_t sent = 0;
    curl_ws_send(easy, "", 0, &sent, 0, CURLWS_PING);
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

    char buffer[65536];
    size_t received = 0;
    const struct curl_ws_frame *meta = nullptr;

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
            // RFC 6455 §5.5.1: respond with a close frame to complete the handshake
            size_t closeSent = 0;
            curl_ws_send(easy, buffer, received, &closeSent, 0, CURLWS_CLOSE);
            connected = false;
            cleanup();
            emit closed();
            return;
        }

        if (meta->flags & CURLWS_PONG) {
            qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - pingTimestamp;
            emit pongReceived(static_cast<quint64>(elapsed >= 0 ? elapsed : 0));
            continue;
        }

        if (meta->flags & CURLWS_PING) {
            // Auto-respond with pong
            size_t sent = 0;
            curl_ws_send(easy, buffer, received, &sent, 0, CURLWS_PONG);
            continue;
        }

        // Handle text/binary frames with possible fragmentation.
        //
        // Two kinds of fragmentation exist:
        // (1) Within-frame: A single frame larger than the recv buffer (65536 bytes).
        //     curl returns CURLWS_TEXT/BINARY with meta->offset > 0 on subsequent reads.
        // (2) Multi-frame (WebSocket protocol fragmentation): First frame has TEXT/BINARY
        //     with CURLWS_CONT flag (FIN=0), continuation frames have CURLWS_CONT only.
        //     curl does not expose the FIN bit separately, so we emit accumulated
        //     multi-frame data after the recv loop when no more data is available.
        //     This works correctly when all frames arrive within the same poll cycle.
        bool isText = (meta->flags & CURLWS_TEXT) != 0;
        bool isBinary = (meta->flags & CURLWS_BINARY) != 0;
        bool isCont = (meta->flags & CURLWS_CONT) != 0;

        // New message starts at offset 0 of a TEXT or BINARY frame
        if ((isText || isBinary) && meta->offset == 0) {
            // Emit any pending multi-frame data before starting new message
            if (fragmentType != 0 && !fragmentBuffer.isEmpty()) {
                if (fragmentType == CURLWS_TEXT) {
                    emit textMessageReceived(QString::fromUtf8(fragmentBuffer));
                } else {
                    emit binaryMessageReceived(fragmentBuffer);
                }
            }
            fragmentBuffer.clear();
            fragmentType = isText ? CURLWS_TEXT : CURLWS_BINARY;
        }
        // CURLWS_CONT without TEXT/BINARY: WebSocket continuation frame.
        // fragmentType carries over from the initial TEXT/BINARY frame.

        fragmentBuffer.append(buffer, static_cast<int>(received));

        bool frameComplete =
            (static_cast<size_t>(meta->offset) + received >= static_cast<size_t>(meta->len));
        frameInProgress = !frameComplete;

        if (frameComplete) {
            // For single-frame messages (no CONT flag), emit immediately.
            // For multi-frame messages (CONT flag set on first or continuation frame),
            // keep accumulating — emit after the recv loop below.
            if (!isCont) {
                if (fragmentType == CURLWS_TEXT) {
                    emit textMessageReceived(QString::fromUtf8(fragmentBuffer));
                } else if (fragmentType != 0) {
                    emit binaryMessageReceived(fragmentBuffer);
                }
                fragmentBuffer.clear();
                fragmentType = 0;
            }
        }
    }

    // Emit accumulated multi-frame WebSocket message after draining all available data.
    // Guard with frameInProgress to avoid emitting partial data from a large single frame
    // that spans multiple poll cycles.
    if (!frameInProgress && fragmentType != 0 && !fragmentBuffer.isEmpty()) {
        if (fragmentType == CURLWS_TEXT) {
            emit textMessageReceived(QString::fromUtf8(fragmentBuffer));
        } else {
            emit binaryMessageReceived(fragmentBuffer);
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
