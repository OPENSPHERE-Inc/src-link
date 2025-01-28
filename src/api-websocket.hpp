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

#include <QObject>
#include <QWebSocket>
#include <QJsonObject>
#include <QTimer>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "schema.hpp"

class SRCLinkApiClient;

class SRCLinkWebSocketClient : public QObject {
    Q_OBJECT

    QUrl url;
    QWebSocket *client;
    SRCLinkApiClient *apiClient;
    bool started;
    int reconnectCount;
    QTimer *intervalTimer;

    void open();

signals:
    void ready(bool reconect);
    void aborted(const QString &reason);
    void connected();
    void disconnected();
    void reconnecting();
    void added(const WebSocketMessage &message);
    void changed(const WebSocketMessage &message);
    void removed(const WebSocketMessage &message);
    void subscribed(const QString &name, const QJsonObject &payload);
    void unsubscribed(const QString &name, const QJsonObject &payload);
    void subscribeFailed(const QString &name, const QJsonObject &payload);
    void unsubscribeFailed(const QString &name, const QJsonObject &payload);
    void invoked(const QString &name, const QJsonObject &payload);
    void invokeFailed(const QString &name, const QJsonObject &payload);
    void error(const QString &reason);

private slots:
    void onConnected();
    void onDisconnected();
    void onPong(quint64 elapsedTime, const QByteArray &payload);
    void onTextMessageReceived(QString message);

public:
    explicit SRCLinkWebSocketClient(QUrl wsUrl, SRCLinkApiClient *apiClient, QObject *parent = nullptr);
    ~SRCLinkWebSocketClient();

    // Do not place slots
    void invokeBin(const QString &name, const json &payload = json());

public slots:
    void start();
    void stop();
    void subscribe(const QString &name, const QJsonObject &payload = QJsonObject());
    void unsubscribe(const QString &name, const QJsonObject &payload = QJsonObject());
    bool isStarted() const { return started; }
    void invokeText(const QString &name, const QJsonObject &payload = QJsonObject());
};
