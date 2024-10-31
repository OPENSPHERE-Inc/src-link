/*
SR Link
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

class SRLinkApiClient;

class SRLinkWebSocketClient : public QObject {
    Q_OBJECT

    QUrl url;
    QWebSocket *client;
    SRLinkApiClient *apiClient;
    bool started;
    int reconnectCount;
    QTimer *intervalTimer;

    void open();

 signals:
    void ready(bool reconect);
    void connected();
    void disconnected();
    void reconnecting();
    void added(const QString &name, const QString &id, const QJsonObject &payload);
    void changed(const QString &name, const QString &id, const QJsonObject &payload);
    void removed(const QString &name, const QString &id, const QJsonObject &payload);

private slots:
    void onConnected();
    void onDisconnected();
    void onPong(quint64 elapsedTime, const QByteArray &payload);
    void onTextMessageReceived(QString message);

public:
    explicit SRLinkWebSocketClient(QUrl wsUrl, SRLinkApiClient *apiClient, QObject *parent = nullptr);
    ~SRLinkWebSocketClient();

public slots:
    void start();
    void stop();
    void subscribe(const QString &name, const QJsonObject &payload = QJsonObject());
    void unsubscribe(const QString &name, const QJsonObject &payload = QJsonObject());
    bool isStarted() const { return started; }
};