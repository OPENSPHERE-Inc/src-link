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

#include <QJsonDocument>

#include "plugin-support.h"
#include "api-websocket.hpp"
#include "api-client.hpp"

#define INTERVAL_INTERVAL_MSECS 30000

//#define API_DEBUG

//--- Macros ---//
#ifdef API_DEBUG
#define API_LOG(...) obs_log(LOG_DEBUG, "websocket: " __VA_ARGS__)
#else
#define API_LOG(...)
#endif
#define WARNING_LOG(...) obs_log(LOG_WARNING, "websocket: " __VA_ARGS__)
#define ERROR_LOG(...) obs_log(LOG_ERROR, "websocket: " __VA_ARGS__)

//--- SRCLinkWebSocketClient class ---//

SRCLinkWebSocketClient::SRCLinkWebSocketClient(QUrl _url, SRCLinkApiClient *_apiClient, QObject *parent)
    : QObject(parent),
      apiClient(_apiClient),
      url(_url),
      started(false),
      reconnectCount(0)
{
    intervalTimer = new QTimer(this);
    client = new QWebSocket("https://" + url.host(), QWebSocketProtocol::Version13, this);

    connect(client, SIGNAL(connected()), this, SLOT(onConnected()));
    connect(client, SIGNAL(disconnected()), this, SLOT(onDisconnected()));
    connect(client, SIGNAL(pong(quint64, const QByteArray &)), this, SLOT(onPong(quint64, const QByteArray &)));
    connect(client, SIGNAL(textMessageReceived(QString)), this, SLOT(onTextMessageReceived(QString)));

    /* Required Qt 6.5 (Error on Ubuntu 22.04)
    connect(client, &QWebSocket::errorOccurred, [this](QAbstractSocket::SocketError error) {
        ERROR_LOG("Error received: %d", error);
    });
    */

    // Setup interval timer for pinging
    connect(intervalTimer, &QTimer::timeout, [this]() {
        if (started && client->isValid()) {
            client->ping();
        }
    });
    intervalTimer->setInterval(INTERVAL_INTERVAL_MSECS);
    intervalTimer->start();

    API_LOG("SRCLinkWebSocketClient created");
}

SRCLinkWebSocketClient::~SRCLinkWebSocketClient()
{
    stop();
    API_LOG("SRCLinkWebSocketClient destroyed");
}

void SRCLinkWebSocketClient::onConnected()
{
    API_LOG("WebSocket connected");
    emit connected();
}

void SRCLinkWebSocketClient::onDisconnected()
{
    if (started) {
        API_LOG("Reconnecting");
        reconnectCount++;
        open();
        emit reconnecting();
    } else {
        API_LOG("Disconnected");
        emit disconnected();
    }
}

void SRCLinkWebSocketClient::onPong(quint64 elapsedTime, const QByteArray &)
{
    UNUSED_PARAMETER(elapsedTime);
    API_LOG("Pong received: %llu", elapsedTime);
}

void SRCLinkWebSocketClient::onTextMessageReceived(QString message)
{
    WebSocketMessage messageObj = QJsonDocument::fromJson(message.toUtf8()).object();
    if (messageObj.getEvent() == "ready") {
        emit ready(reconnectCount > 0);
    } else if (messageObj.getEvent() == "aborted") {
        emit aborted(messageObj.getReason());
    } else if (messageObj.getEvent() == "added") {
        emit added(messageObj);
    } else if (messageObj.getEvent() == "changed") {
        emit changed(messageObj);
    } else if (messageObj.getEvent() == "removed") {
        emit removed(messageObj);
    } else if (messageObj.getEvent() == "subscribed") {
        emit subscribed(messageObj.getName(), messageObj.getPayload());
    } else if (messageObj.getEvent() == "unsubscribed") {
        emit unsubscribed(messageObj.getName(), messageObj.getPayload());
    } else if (messageObj.getEvent() == "invoked") {
        emit invoked(messageObj.getName(), messageObj.getPayload());
    } else if (messageObj.getEvent() == "subscribe_failed") {
        emit subscribeFailed(messageObj.getName(), messageObj.getPayload());
    } else if (messageObj.getEvent() == "unsubscribe_failed") {
        emit unsubscribeFailed(messageObj.getName(), messageObj.getPayload());
    } else if (messageObj.getEvent() == "invoke_failed") {
        emit invokeFailed(messageObj.getName(), messageObj.getPayload());
    } else if (messageObj.getEvent() == "error") {
        emit error(messageObj.getReason());
    } else {
        WARNING_LOG("Unknown message: %s", qUtf8Printable(message));
    }
}

void SRCLinkWebSocketClient::open()
{
    if (client->isValid()) {
        return;
    }

    auto req = QNetworkRequest(url);
    req.setRawHeader("Authorization", QString("Bearer %1").arg(apiClient->getAccessToken()).toLatin1());

    client->open(req);
}

void SRCLinkWebSocketClient::start()
{
    if (started) {
        return;
    }

    API_LOG("Connecting: %s", qUtf8Printable(url.toString()));
    started = true;
    reconnectCount = 0;
    open();
}

void SRCLinkWebSocketClient::stop()
{
    if (!started) {
        return;
    }

    API_LOG("Disconnecting");
    started = false;
    client->close();
}

void SRCLinkWebSocketClient::subscribe(const QString &name, const QJsonObject &payload)
{
    if (!started || !client->isValid()) {
        return;
    }

    API_LOG("Subscribe: %s", qUtf8Printable(name));

    QJsonObject message;
    message["event"] = "subscribe";
    message["name"] = name;
    message["payload"] = payload;

    client->sendTextMessage(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

void SRCLinkWebSocketClient::unsubscribe(const QString &name, const QJsonObject &payload)
{
    if (!started || !client->isValid()) {
        return;
    }

    API_LOG("Unsubscribe: %s", qUtf8Printable(name));

    QJsonObject message;
    message["event"] = "unsubscribe";
    message["name"] = name;
    message["payload"] = payload;

    client->sendTextMessage(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

void SRCLinkWebSocketClient::invokeBin(const QString &name, const json &payload)
{
    if (!started || !client->isValid()) {
        return;
    }

    API_LOG("Invoke(bin): %s", qUtf8Printable(name));

    json message;

    message["event"] = "invoke";
    message["name"] = qUtf8Printable(name);
    message["payload"] = payload;

    auto bson = json::to_bson(message);
    auto sent = client->sendBinaryMessage(QByteArray(reinterpret_cast<const char *>(bson.data()), bson.size()));

    UNUSED_PARAMETER(sent);
    API_LOG("Invoke(bin): %lld bytes sent", sent);
}

void SRCLinkWebSocketClient::invokeText(const QString &name, const QJsonObject &payload)
{
    if (!started || !client->isValid()) {
        return;
    }

    API_LOG("Invoke(text): %s", qUtf8Printable(name));

    QJsonObject message;
    message["event"] = "invoke";
    message["name"] = name;
    message["payload"] = payload;

    auto sent = client->sendTextMessage(QJsonDocument(message).toJson(QJsonDocument::Compact));

    UNUSED_PARAMETER(sent);
    API_LOG("Invoke(text): %lld bytes sent", sent);
}
