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

#include <QUrlQuery>

#include "ws-portal-client.hpp"
#include "../api-client.hpp"

//#define API_DEBUG

// REST Endpoints
#ifndef WS_PORTAL_SERVER
#define WS_PORTAL_SERVER "ws://localhost:14455"
#endif
#define WS_PORTALS_URL (WS_PORTAL_SERVER "/v1/ws-portals")

//--- Macros ---//
#ifdef API_DEBUG
#define API_LOG(...) obs_log(LOG_DEBUG, "ws-portal: " __VA_ARGS__)
#else
#define API_LOG(...)
#endif
#define WARNING_LOG(...) obs_log(LOG_WARNING, "ws-portal: " __VA_ARGS__)
#define ERROR_LOG(...) obs_log(LOG_ERROR, "ws-portal: " __VA_ARGS__)

//--- WsPortalClient class ---//

WsPortalClient::WsPortalClient(SRCLinkApiClient *_apiClient, QObject *parent)
    : QObject(parent),
      apiClient(_apiClient),
      status(WS_PORTAL_STATUS_INACTIVE),
      reconnectCount(0)
{
    QUrl url = QUrl(WS_PORTALS_URL);
    client = new QWebSocket("https://" + url.host(), QWebSocketProtocol::Version13, this);

    connect(client, SIGNAL(connected()), this, SLOT(onConnected()));
    connect(client, SIGNAL(disconnected()), this, SLOT(onDisconnected()));
    connect(client, SIGNAL(onTextMessageReceived(const QString &)), this, SLOT(onTextMessageReceived(const QString &)));
    connect(
        client, SIGNAL(binaryMessageReceived(const QByteArray &)), this,
        SLOT(onBinaryMessageReceived(const QByteArray &))
    );
    connect(client, SIGNAL(pong(quint64, const QByteArray &)), this, SLOT(onPong(quint64, const QByteArray &)));

    connect(apiClient, SIGNAL(ready(bool)), this, SLOT(onApiClientReady(bool)));
    connect(
        apiClient, SIGNAL(wsPortalsReady(const WsPortalArray &)), this, SLOT(onWsPortalsReady(const WsPortalArray &))
    );
    connect(apiClient, SIGNAL(logoutSucceeded()), this, SLOT(onLogoutSucceeded()));
    connect(apiClient, SIGNAL(loginFailed()), this, SLOT(onLogoutSucceeded()));

    obs_websocket_register_event_callback(onOBSWebSocketEvent, this);

    API_LOG("WsPortalClient created");
}

WsPortalClient::~WsPortalClient()
{
    disconnect(this);

    obs_websocket_unregister_event_callback(onOBSWebSocketEvent, this);

    API_LOG("WsPortalClient destroyed");
}

void WsPortalClient::onLogoutSucceeded()
{
    stop();
}

void WsPortalClient::onApiClientReady(bool reconnect)
{
    if (!reconnect) {
        start();
    }
}

void WsPortalClient::onWsPortalsReady(const WsPortalArray &portals)
{
    wsPortal = portals.find([this](const WsPortal &portal) {
        return portal.getId() == apiClient->getSettings()->getWsPortalId();
    });

    if (!wsPortal.isEmpty() && status == WS_PORTAL_STATUS_INACTIVE) {
        // Start when not started
        start();
    }
}

void WsPortalClient::onConnected()
{
    API_LOG("WebSocket connected");
    emit connected();
}

void WsPortalClient::onDisconnected()
{
    auto portalId = apiClient->getSettings()->getWsPortalId();
    if (status != WS_PORTAL_STATUS_INACTIVE && !portalId.isEmpty() && portalId != "none") {
        API_LOG("Reconnecting");
        reconnectCount++;
        open(portalId);
        emit reconnecting();
    } else {
        API_LOG("Disconnected");
        emit disconnected();
    }
}

void WsPortalClient::onPong(quint64 elapsedTime, const QByteArray &)
{
    UNUSED_PARAMETER(elapsedTime);
    API_LOG("Pong received: %llu", elapsedTime);
}

void WsPortalClient::onTextMessageReceived(const QString &message)
{
    if (message == "ready") {
        emit ready(reconnectCount > 0);
    }
}

void WsPortalClient::onBinaryMessageReceived(const QByteArray &message)
{
    std::vector<uint8_t> messageBytes(message.begin(), message.end());
    auto messageObj = json::from_bson(messageBytes, true, false);
    if (messageObj.type() == json::value_t::discarded || messageObj.empty()) {
        API_LOG("Invalid message");
        return;
    }

    auto connectionId = QString::fromStdString(messageObj["connectionId"]);
    auto body = json::parse(messageObj["body"].template get<std::string>()); // The body is stored in plain text

    int op = body["op"];
    auto data = body["d"];

    switch (op) {
    case 6: {
        // Request
        auto response = processRequest(data);
        sendMessage(connectionId, 7, response);
        break;
    }

    case 8: {
        // Request batch
        bool haltOnFailure = data["haltOnFailure"];
        int executionType = data["executionType"];
        auto requestId = QString::fromStdString(data["requestId"]);
        auto requests = data["requests"];

        json responses;
        for (auto request = requests.begin(); request != requests.end(); ++request) {
            auto response = processRequest(*request);
            if (haltOnFailure && response["requestStatus"]["result"] == false) {
                break;
            }
            responses.push_back(response);
        }

        sendMessage(connectionId, 9, {{"requestId", qUtf8Printable(requestId)}, {"results", responses}});
        break;
    }

    default:
        // Ignore unknown opcodes
        break;
    }
}

json WsPortalClient::processRequest(const json &request)
{
    auto requestId = QString::fromStdString(request["requestId"]);
    auto requestType = QString::fromStdString(request["requestType"]);
    auto requestData = request["requestData"];

    OBSDataAutoRelease data = requestData.size() ? obs_data_create_from_json(requestData.dump().c_str())
                                                 : obs_data_create();
    auto response = obs_websocket_call_request(qUtf8Printable(requestType), data);

    json requestStatus = {{"code", (int)response->status_code}, {"result", response->status_code == 100}};
    if (response->comment) {
        requestStatus["comment"] = response->comment;
    }

    json responseJson;
    responseJson["requestType"] = qUtf8Printable(requestType);
    responseJson["requestId"] = qUtf8Printable(requestId);
    responseJson["requestStatus"] = requestStatus;
    responseJson["responseData"] = response->response_data ? json::parse(response->response_data) : nullptr;

    obs_websocket_request_response_free(response);

    return responseJson;
}

void WsPortalClient::send(const QByteArray &message)
{
    client->sendBinaryMessage(message);
}

void WsPortalClient::sendMessage(const QString &connectionId, int opcode, const json &data)
{
    if (status != WS_PORTAL_STATUS_ACTIVE) {
        return;
    }

    json body = {{"op", opcode}, {"d", data}};
    json message = {
        {"connectionId", qUtf8Printable(connectionId)},
        // The body is stored in plain text
        {"body", body.dump()}
    };

    // Called in proper thread
    auto bson = json::to_bson(message);
    QMetaObject::invokeMethod(
        this, "send", Qt::QueuedConnection,
        Q_ARG(QByteArray, QByteArray(reinterpret_cast<const char *>(bson.data()), bson.size()))
    );
}

void WsPortalClient::sendEvent(uint64_t requiredIntent, const char *eventType, const char *eventData)
{
    if (status != WS_PORTAL_STATUS_ACTIVE) {
        return;
    }

    // Filter out events with portal's event subscriptions
    // Default subscriptions is "All" (But high volume events are excluded)
    auto eventSubscriptions = (wsPortal["event_subscription"].isUndefined() || wsPortal["event_subscriptions"].isNull())
                                  ? (int)0x7FF
                                  : wsPortal.getEventSubscriptions();
    if (!(requiredIntent & eventSubscriptions)) {
        return;
    }

    json data = {{"eventType", eventType}, {"eventIntent", (int)requiredIntent}, {"eventData", json::parse(eventData)}};
    json body = {{"op", 5}, {"d", data}};
    // The body is stored in plain text
    json message = {{"body", body.dump()}};

    // Called in proper thread
    auto bson = json::to_bson(message);
    QMetaObject::invokeMethod(
        this, "send", Qt::QueuedConnection,
        Q_ARG(QByteArray, QByteArray(reinterpret_cast<const char *>(bson.data()), bson.size()))
    );
}

void WsPortalClient::onOBSWebSocketEvent(
    uint64_t requiredIntent, const char *eventType, const char *eventData, void *privData
)
{
    auto controller = static_cast<WsPortalClient *>(privData);
    controller->sendEvent(requiredIntent, eventType, eventData);
}

void WsPortalClient::open(const QString &portalId)
{
    if (client->isValid()) {
        return;
    }

    QUrlQuery parameters;
    parameters.addQueryItem("portalId", portalId);
    parameters.addQueryItem("uuid", apiClient->getUuid());

    QUrl url = QUrl(WS_PORTALS_URL);
    url.setQuery(parameters);

    auto req = QNetworkRequest(url);
    req.setRawHeader("Authorization", QString("Bearer %1").arg(apiClient->getAccessToken()).toLatin1());

    client->open(req);
}

void WsPortalClient::start()
{
    if (status == WS_PORTAL_STATUS_ACTIVE) {
        return;
    }

    auto portalId = apiClient->getSettings()->getWsPortalId();
    if (portalId.isEmpty() || portalId == "none") {
        return;
    }

    wsPortal =
        apiClient->getWsPortals().find([portalId](const WsPortal &_portal) { return _portal.getId() == portalId; });
    if (wsPortal.isEmpty()) {
        return;
    }

    API_LOG("Connecting: %s", WS_PORTALS_URL);
    status = WS_PORTAL_STATUS_ACTIVE;
    reconnectCount = 0;
    open(portalId);
}

void WsPortalClient::stop()
{
    if (status == WS_PORTAL_STATUS_INACTIVE) {
        return;
    }

    API_LOG("Disconnecting");
    status = WS_PORTAL_STATUS_INACTIVE;
    client->close();
}

void WsPortalClient::restart()
{
    stop();
    start();
}
