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

#pragma once

#include <atomic>
#include <thread>

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QMap>
#include <QPointer>
#include <QSocketNotifier>
#include <QTimer>

#include <curl/curl.h>
#include <curl/websockets.h>

class WsClient : public QObject {
    Q_OBJECT

public:
    explicit WsClient(QObject *parent = nullptr);
    ~WsClient();

    /// Set custom headers (call before open)
    void setHeaders(const QMap<QByteArray, QByteArray> &headers);

    /// Open WebSocket connection
    void open(const QUrl &url);

    /// Close WebSocket connection
    void close();

    /// Check if connection is valid
    bool isValid() const;

    /// Send a text message. Returns bytes sent or -1 on error.
    qint64 sendTextMessage(const QString &message);

    /// Send a binary message. Returns bytes sent or -1 on error.
    qint64 sendBinaryMessage(const QByteArray &data);

    /// Send a ping frame
    void ping();

signals:
    void opened();
    void closed();
    void textMessageReceived(const QString &message);
    void binaryMessageReceived(const QByteArray &data);
    void pongReceived(quint64 elapsedTime);
    void errorOccurred(const QString &reason);

private:
    CURL *easy;
    QSocketNotifier *recvNotifier;
    QTimer *tlsFallbackTimer; // Low-frequency fallback for TLS buffer stall recovery
    std::thread connectThread;
    QMap<QByteArray, QByteArray> requestHeaders;
    struct curl_slist *headerList;
    bool connected;
    std::atomic<bool> connecting;
    std::atomic<bool> abortConnect;                 // Set by close()/dtor to make curl_easy_perform() return early
    std::atomic<unsigned int> connectGeneration{0}; // Incremented per connection attempt to detect stale callbacks
    QUrl pendingUrl;
    QByteArray fragmentBuffer;
    int fragmentType;     // CURLWS_TEXT or CURLWS_BINARY
    bool frameInProgress; // True when reading a frame that spans multiple recv calls
    // Only the most recent ping is timed. An unmatched older pong would skew
    // the measurement, but this is acceptable in practice since pings are
    // typically issued at intervals far longer than RTT.
    qint64 pingTimestamp;

    void performConnect();
    void onConnectFinished(CURLcode result, unsigned int generation);
    void joinConnectThread();
    void pollRecv();
    void cleanup();
    void startRecvNotifier();
    qint64 sendRaw(const char *data, size_t len, unsigned int flags, bool shuttingDown = false);

    static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static int
    xferInfoCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
};
