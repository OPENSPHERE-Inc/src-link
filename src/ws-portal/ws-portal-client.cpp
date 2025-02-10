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
#include "event-handler.hpp"

#define INTERVAL_INTERVAL_MSECS 30000

//#define API_DEBUG

#define WS_PORTALS_PATH "/v1/ws-portals"

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
      client(nullptr),
      status(WS_PORTAL_STATUS_INACTIVE),
      reconnectCount(0)
{
    intervalTimer = new QTimer(this);

    connect(apiClient, SIGNAL(ready(bool)), this, SLOT(onApiClientReady(bool)));
    connect(
        apiClient, SIGNAL(wsPortalsReady(const WsPortalArray &)), this, SLOT(onWsPortalsReady(const WsPortalArray &))
    );
    connect(apiClient, SIGNAL(logoutSucceeded()), this, SLOT(onLogoutSucceeded()));
    connect(apiClient, SIGNAL(loginFailed()), this, SLOT(onLogoutSucceeded()));

    // Setup interval timer for pinging
    connect(intervalTimer, &QTimer::timeout, [this]() {
        if (status == WS_PORTAL_STATUS_ACTIVE && client && client->isValid()) {
            client->ping();
        }
    });
    intervalTimer->setInterval(INTERVAL_INTERVAL_MSECS);
    intervalTimer->start();

    // obs-websocket doesn't broadcast high-volume events unless having native WebSocket connections
    // so we create dedicated event handler for WsPortal links.
    WsPortalEventHandler::getInstance()->registerEventCallback(onOBSWebSocketEvent, this);

    API_LOG("WsPortalClient created");
}

WsPortalClient::~WsPortalClient()
{
    WsPortalEventHandler::getInstance()->unregisterEventCallback(onOBSWebSocketEvent, this);

    disconnect(this);
    stop();

    API_LOG("WsPortalClient destroyed");
}

void WsPortalClient::createWsSocket()
{
    if (wsPortal.getFacilityView().isEmpty()) {
        ERROR_LOG("Facility is empty: %s", qUtf8Printable(wsPortal.getName()));
        return;
    }

    // Ensure previous client is destroyed
    destroyWsSocket();

    client = new QWebSocket("https://" + wsPortal.getFacilityView().getHost(), QWebSocketProtocol::Version13, this);

    connect(client, SIGNAL(connected()), this, SLOT(onConnected()));
    connect(client, SIGNAL(disconnected()), this, SLOT(onDisconnected()));
    connect(client, SIGNAL(textMessageReceived(const QString &)), this, SLOT(onTextMessageReceived(const QString &)));
    connect(
        client, SIGNAL(binaryMessageReceived(const QByteArray &)), this,
        SLOT(onBinaryMessageReceived(const QByteArray &))
    );
    connect(client, SIGNAL(pong(quint64, const QByteArray &)), this, SLOT(onPong(quint64, const QByteArray &)));

    API_LOG("WebSocket created for the portal: %s", qUtf8Printable(wsPortal.getName()));
}

void WsPortalClient::destroyWsSocket()
{
    if (!client) {
        return;
    }

    client->disconnect(this);
    client->close();
    client->deleteLater();
    client = nullptr;

    API_LOG("WebSocket closed: %s", qUtf8Printable(wsPortal.getFacilityView().getHostAndPort()));
}

void WsPortalClient::open(const QString &portalId)
{
    if (client && client->isValid()) {
        return;
    }

    QUrlQuery parameters;
    parameters.addQueryItem("portalId", portalId);
    parameters.addQueryItem("uuid", apiClient->getUuid());

    createWsSocket();

    QUrl url = QUrl(wsPortal.getFacilityView().getUrl());
    url.setPath(WS_PORTALS_PATH);
    url.setQuery(parameters);
    API_LOG("Opening WebSocket: %s", qUtf8Printable(url.toString()));

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

    status = WS_PORTAL_STATUS_ACTIVE;
    reconnectCount = 0;
    open(portalId);

    WsPortalEventHandler::getInstance()->subscribe(wsPortal.getEventSubscriptions());
}

void WsPortalClient::stop()
{
    if (status == WS_PORTAL_STATUS_INACTIVE) {
        return;
    }

    status = WS_PORTAL_STATUS_INACTIVE;
    destroyWsSocket();

    if (!wsPortal.isEmpty()) {
        WsPortalEventHandler::getInstance()->unsubscribe(wsPortal.getEventSubscriptions());
        wsPortal = WsPortal();
    }

    emit disconnected();
}

void WsPortalClient::restart()
{
    stop();
    start();
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
    auto messageObj = json::from_msgpack(messageBytes, true, false);
    if (messageObj.type() == json::value_t::discarded || messageObj.empty()) {
        API_LOG("Invalid message");
        return;
    }

    auto connectionId = QString::fromStdString(messageObj["connectionId"]);
    // The body is stored in msgpack encoded binary
    auto body = json::from_msgpack(messageObj["body"].get_binary(), true, false);

    int op = body["op"];
    auto data = body["d"];

    switch (op) {
    case 6: {
        // Request
        auto response = processRequest(data);
        // response possibly empty (e.g. obs-websocket had been terminated)
        if (response.empty()) {
            return;
        }

        sendMessage(connectionId, 7, response);
        break;
    }

    case 8: {
        // Request batch
        bool haltOnFailure = data["haltOnFailure"];
        //int executionType = data["executionType"];
        auto requestId = QString::fromStdString(data["requestId"]);
        auto requests = data["requests"];

        json responses;
        for (auto &request : requests) {
            auto response = processRequest(request);
            if (haltOnFailure && (response.empty() || response["requestStatus"]["result"] == false)) {
                break;
            }
            // response possibly empty (e.g. obs-websocket had been terminated)
            if (!response.empty()) {
                responses.push_back(response);
            }
        }

        if (!responses.empty()) {
            sendMessage(connectionId, 9, {{"requestId", qUtf8Printable(requestId)}, {"results", responses}});
        }
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
    json responseJson;

    OBSDataAutoRelease data = requestData.size() ? obs_data_create_from_json(requestData.dump().c_str())
                                                 : obs_data_create();
    auto response = obs_websocket_call_request(qUtf8Printable(requestType), data);
    if (!response) {
        return responseJson;
    }

    json requestStatus = {{"code", (int)response->status_code}, {"result", response->status_code == 100}};
    if (response->comment) {
        requestStatus["comment"] = response->comment;
    }

    responseJson["requestType"] = qUtf8Printable(requestType);
    responseJson["requestId"] = qUtf8Printable(requestId);
    responseJson["requestStatus"] = requestStatus;
    responseJson["responseData"] = response->response_data ? json::parse(response->response_data) : nullptr;

    obs_websocket_request_response_free(response);

    return responseJson;
}

void WsPortalClient::send(const QByteArray &message)
{
    if (!client) {
        ERROR_LOG("WebSocket client is empty");
        return;
    }
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
        // The body is stored in msgpack encoded binary
        {"body", json::to_msgpack(body)}
    };

    auto raw = json::to_msgpack(message);
    // Called in proper thread
    QMetaObject::invokeMethod(
        this, "send", Qt::QueuedConnection,
        Q_ARG(QByteArray, QByteArray(reinterpret_cast<const char *>(raw.data()), raw.size()))
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
    // The body is stored in msgpack encoded binary
    json message = {{"body", json::to_msgpack(body)}};

    auto raw = json::to_msgpack(message);
    // Called in proper thread
    QMetaObject::invokeMethod(
        this, "send", Qt::QueuedConnection,
        Q_ARG(QByteArray, QByteArray(reinterpret_cast<const char *>(raw.data()), raw.size()))
    );
}

void WsPortalClient::onOBSWebSocketEvent(
    uint64_t requiredIntent, const char *eventType, const char *eventData, void *privData
)
{
    auto controller = static_cast<WsPortalClient *>(privData);
    controller->sendEvent(requiredIntent, eventType, eventData);
}
