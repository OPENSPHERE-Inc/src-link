/*
Source Link
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

//--- SourceLinkWebSocketClient class ---//

SourceLinkWebSocketClient::SourceLinkWebSocketClient(QUrl _url, SourceLinkApiClient *_apiClient, QObject *parent)
    : QObject(parent),
      apiClient(_apiClient),
      url(_url),
      started(false)
{
    intervalTimer = new QTimer(this);
    client = new QWebSocket("https://" + url.host(), QWebSocketProtocol::Version13, this);

    connect(client, SIGNAL(connected()), this, SLOT(onConnected()));
    connect(client, SIGNAL(disconnected()), this, SLOT(onDisconnected()));
    connect(client, SIGNAL(pong(quint64, const QByteArray &)), this, SLOT(onPong(quint64, const QByteArray &)));
    connect(client, SIGNAL(textMessageReceived(QString)), this, SLOT(onTextMessageReceived(QString)));

    connect(client, &QWebSocket::errorOccurred, [this](QAbstractSocket::SocketError error) {
        obs_log(LOG_ERROR, "SourceLinkWebSocketClient error: %d", error);
    });

    // Setup interval timer for pinging
    connect(intervalTimer, &QTimer::timeout, [this]() {
        if (started && client->isValid()) {
            client->ping();
        }
    });
    intervalTimer->setInterval(INTERVAL_INTERVAL_MSECS);
    intervalTimer->start();

    obs_log(LOG_DEBUG, "SourceLinkWebSocketClient created");
}

SourceLinkWebSocketClient::~SourceLinkWebSocketClient() 
{
    stop();
    obs_log(LOG_DEBUG, "SourceLinkWebSocketClient destroyed");
}

void SourceLinkWebSocketClient::onConnected() 
{
    obs_log(LOG_DEBUG, "WebSocket connected");
}

void SourceLinkWebSocketClient::onDisconnected() 
{
    if (started) {
        // Reconnect
        obs_log(LOG_DEBUG, "WebSocket reconnecting");
        open();
    } else {
        obs_log(LOG_DEBUG, "WebSocket disconnected");
    }
}

void SourceLinkWebSocketClient::onPong(quint64 elapsedTime, const QByteArray &payload) 
{
    obs_log(LOG_DEBUG, "WebSocket pong received: %llu", elapsedTime);
}

void SourceLinkWebSocketClient::onTextMessageReceived(QString message) 
{
    WebSocketMessage messageObj = QJsonDocument::fromJson(message.toUtf8()).object();
    if (messageObj.getEvent() == "ready") {
        emit ready();
    } else if (messageObj.getEvent() == "added") {
        emit added(messageObj.getSubId(), messageObj.getName(), messageObj.getId(), messageObj.getPayload());
    } else if (messageObj.getEvent() == "changed") {
        emit changed(messageObj.getSubId(), messageObj.getName(), messageObj.getId(), messageObj.getPayload());
    } else if (messageObj.getEvent() == "removed") {
        emit removed(messageObj.getSubId(), messageObj.getName(), messageObj.getId());
    } else {
        obs_log(LOG_WARNING, "WebSocket unknown message: %s", qPrintable(message));
    }
}

void SourceLinkWebSocketClient::open() 
{
    if (client->isValid()) {
        return;
    }

    auto req = QNetworkRequest(url);
    req.setRawHeader("Authorization", QString("Bearer %1").arg(apiClient->client->token()).toLatin1());

    client->open(req);
}

void SourceLinkWebSocketClient::start() 
{
    if (started) {
        return;
    }

    obs_log(LOG_DEBUG, "WebSocket connecting: %s", qPrintable(url.toString()));
    open();
    started = true;
}

void SourceLinkWebSocketClient::stop() 
{
    if (!started) {
        return;
    }

    obs_log(LOG_DEBUG, "WebSocket disconnecting");
    started = false;
    client->close();
}

void SourceLinkWebSocketClient::subscribe(const QString &name, const QString &uuid) 
{
    if (!started || !client->isValid()) {
        return;
    }
    
    obs_log(LOG_DEBUG, "WebSocket subscribe: %s", qPrintable(name));

    QJsonObject payload;
    payload["uuid"] = uuid;

    QJsonObject message;
    message["event"] = "subscribe";
    message["name"] = name;
    message["payload"] = payload;

    client->sendTextMessage(QJsonDocument(message).toJson());
}

void SourceLinkWebSocketClient::unsubscribe(const QString &name, const QString &uuid)
{
    if (!started || !client->isValid()) {
        return;
    }

    obs_log(LOG_DEBUG, "WebSocket unsubscribe: %s", qPrintable(name));

    QJsonObject payload;
    payload["uuid"] = uuid;

    QJsonObject message;
    message["event"] = "unsubscribe";
    message["name"] = name;
    message["payload"] = payload;

    client->sendTextMessage(QJsonDocument(message).toJson());
}