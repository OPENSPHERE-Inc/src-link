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

#include <obs-module.h>
#include <obs.hpp>
#include <util/platform.h>

#include "ws-portal.hpp"
#include "../utils.hpp"

//--- WsPortalController class ---//

WsPortalController::WsPortalController(SRCLinkApiClient *_apiClient, QObject *parent)
    : QObject(parent),
      apiClient(_apiClient),
      active(false)
{
    connect(
        apiClient, SIGNAL(wsPortalMessageReceived(const WsPortalMessage &)), this,
        SLOT(onWsPortalMessageReceived(const WsPortalMessage &))
    );
    connect(
        apiClient, &SRCLinkApiClient::webSocketSubscribeSucceeded, this,
        [this](const QString &name, const QJsonObject &payload) {
            if (name == "ws-portal-messages") {
                active = true;
            }
        }
    );
    connect(
        apiClient, &SRCLinkApiClient::webSocketUnsubscribeSucceeded, this,
        [this](const QString &name, const QJsonObject &payload) {
            if (name == "ws-portal-messages") {
                active = false;
            }
        }
    );

    obs_websocket_register_event_callback(onOBSWebSocketEvent, this);

    obs_log(LOG_DEBUG, "WsPortalController created");
}

WsPortalController::~WsPortalController()
{
    disconnect(this);

    obs_websocket_unregister_event_callback(onOBSWebSocketEvent, this);

    obs_log(LOG_DEBUG, "WsPortalController destroyed");
}

void WsPortalController::onWsPortalMessageReceived(const WsPortalMessage &message)
{
    auto connectionId = message.getConnectionId();
    auto payload = message.getPayload();
    auto op = payload["op"].toInt();

    switch (op) {
    case 6: {
        // Request
        auto response = processRequest(payload["d"].toObject());
        sendResponse(connectionId, 7, response);
        break;
    }

    case 8: {
        // Request batch
        auto halfOnFailure = payload["d"].toObject()["halfOnFailure"].toBool();
        auto executionType = payload["d"].toObject()["executionType"].toInt();
        auto requestId = payload["d"].toObject()["requestId"].toString();
        auto requests = payload["d"].toObject()["requests"].toArray();

        QJsonArray responses;
        foreach (const auto &request, requests) {
            auto response = processRequest(request.toObject());
            responses.append(response);
        }

        sendResponse(connectionId, 9, {{"requestId", requestId}, {"results", responses}});
        break;
    }

    default:
        // Ignore unknown opcodes
        break;
    }
}

QJsonObject WsPortalController::processRequest(const QJsonObject &request)
{
    auto requestType = request["requestType"].toString();
    auto requestData = request["requestData"].toObject();

    OBSDataAutoRelease data = obs_data_create_from_json(QJsonDocument(requestData).toJson().constData());
    auto response = obs_websocket_call_request(qUtf8Printable(requestType), data);

    QJsonObject responseJson;
    responseJson["requestType"] = requestType;
    responseJson["requestId"] = requestData["requestId"];
    responseJson["requestStatus"] = QJsonObject(
        {{"code", (int)response->status_code}, {"result", response->status_code == 100}, {"comment", response->comment}}
    );
    responseJson["responseData"] = QJsonDocument::fromJson(response->response_data).object();

    obs_websocket_request_response_free(response);

    return responseJson;
}

void WsPortalController::sendResponse(const QString &connectionId, int opcode, const QJsonObject &response)
{
    if (!active) {
        return;
    }

    QJsonObject body = {{"op", opcode}, {"d", response}};
    //apiClient->postWsPortalMessage(connectionId, body);
    QMetaObject::invokeMethod(apiClient, "postWsPortalMessage", Qt::QueuedConnection, Q_ARG(QString, connectionId), Q_ARG(QJsonObject, body));
}

void WsPortalController::emitEvent(uint64_t requiredIntent, const char *eventType, const char *eventData)
{
    if (!active) {
        return;
    }

    QJsonObject event = {
        {"eventType", eventType},
        {"eventIntent", (int)requiredIntent},
        {"eventData", QJsonDocument::fromJson(eventData).object()}
    };
    QJsonObject body = {{"op", 5}, {"d", event}};
    //apiClient->postWsPortalEvent(body);
    QMetaObject::invokeMethod(apiClient, "postWsPortalEvent", Qt::QueuedConnection, Q_ARG(QJsonObject, body));
}

void WsPortalController::onOBSWebSocketEvent(
    uint64_t requiredIntent, const char *eventType, const char *eventData, void *privData
)
{
    auto controller = static_cast<WsPortalController *>(privData);
    controller->emitEvent(requiredIntent, eventType, eventData);
}
