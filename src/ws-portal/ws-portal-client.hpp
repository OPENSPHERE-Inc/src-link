/*
SRC-Link
Copyright (C) 2025 OPENSPHERE Inc. info@opensphere.co.jp

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

#include <obs-websocket-api.h>

#include <QObject>
#include <QWebSocket>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "../schema.hpp"
#include "../utils.hpp"

using OBSWebSocketRequestResponse = OBSPtr<obs_websocket_request_response *, obs_websocket_request_response_free>;

enum WsPortalStatus {
    WS_PORTAL_STATUS_INACTIVE,
    WS_PORTAL_STATUS_ACTIVE,
};

class SRCLinkApiClient;

class WsPortalClient : public QObject {
    Q_OBJECT

    QWebSocket *client;
    SRCLinkApiClient *apiClient;
    WsPortalStatus status;
    int reconnectCount;
    WsPortal wsPortal;
    QTimer *intervalTimer;

    json processRequest(const json &request);
    void sendMessage(const QString &connectionId, int opcode, const json &data);
    void sendEvent(uint64_t requiredIntent, const char *eventType, const char *eventData);

    static void
    onOBSWebSocketEvent(uint64_t requiredIntent, const char *eventType, const char *eventData, void *privData);
    

    void open(const QString &portalId);

signals:
    void connected();
    void ready(bool reconect);
    void disconnected();
    void reconnecting();

private slots:
    void onApiClientReady(bool reconnect);
    void onWsPortalsReady(const WsPortalArray &portals);
    void onLogoutSucceeded();
    void onConnected();
    void onDisconnected();
    void onPong(quint64 elapsedTime, const QByteArray &payload);
    void onTextMessageReceived(const QString &message);
    void onBinaryMessageReceived(const QByteArray &message);
    void send(const QByteArray &message);

public:
    explicit WsPortalClient(SRCLinkApiClient *_apiClient, QObject *parent = nullptr);
    ~WsPortalClient();

public slots:
    inline bool getStatus() const { return status; }
    void start();
    void stop();
    void restart();
};
