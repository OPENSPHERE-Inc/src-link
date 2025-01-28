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

#include "../api-client.hpp"

using OBSWebSocketRequestResponse = OBSPtr<obs_websocket_request_response *, obs_websocket_request_response_free>;

class WsPortalController : public QObject {
    Q_OBJECT

    SRCLinkApiClient *apiClient;
    bool active;

    QJsonObject processRequest(const QJsonObject &request);
    void sendResponse(const QString &connectionId, int opcode, const QJsonObject &response);
    void emitEvent(uint64_t requiredIntent, const char *eventType, const char *eventData);

    static void
    onOBSWebSocketEvent(uint64_t requiredIntent, const char *eventType, const char *eventData, void *privData);

private slots:
    void onWsPortalMessageReceived(const WsPortalMessage &message);

public:
    explicit WsPortalController(SRCLinkApiClient *_apiClient, QObject *parent = nullptr);
    ~WsPortalController();

public slots:
    inline bool isActive() const { return active; }
    
};
