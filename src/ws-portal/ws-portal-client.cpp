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

#include <algorithm>

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
      client(nullptr),
      apiClient(_apiClient),
      status(WS_PORTAL_STATUS_INACTIVE),
      reconnectCount(0),
      reconnectPending(false)
{
    intervalTimer = new QTimer(this);
    reconnectTimer = new QTimer(this);
    reconnectTimer->setSingleShot(true);
    connect(reconnectTimer, &QTimer::timeout, this, [this]() {
        reconnectPending = false;
        if (status != WS_PORTAL_STATUS_INACTIVE && !reconnectPortalId.isEmpty()) {
            open(reconnectPortalId);
            emit reconnecting();
        }
    });

    connect(apiClient, &SRCLinkApiClient::ready, this, &WsPortalClient::onApiClientReady);
    connect(apiClient, &SRCLinkApiClient::wsPortalsReady, this, &WsPortalClient::onWsPortalsReady);
    connect(apiClient, &SRCLinkApiClient::logoutSucceeded, this, &WsPortalClient::onLogoutSucceeded);
    connect(apiClient, &SRCLinkApiClient::loginFailed, this, &WsPortalClient::onLogoutSucceeded);

    // Setup interval timer for pinging
    connect(intervalTimer, &QTimer::timeout, this, [this]() {
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

    client = new WsClient(this);

    connect(client, &WsClient::opened, this, &WsPortalClient::onConnected);
    connect(client, &WsClient::closed, this, &WsPortalClient::onDisconnected);
    connect(client, &WsClient::textMessageReceived, this, &WsPortalClient::onTextMessageReceived);
    connect(client, &WsClient::binaryMessageReceived, this, &WsPortalClient::onBinaryMessageReceived);
    connect(client, &WsClient::errorOccurred, this, [this](const QString &reason) {
        ERROR_LOG("Error received: %s", qUtf8Printable(reason));

        // WsClient may emit errorOccurred with or without a subsequent closed signal;
        // route both through scheduleReconnect() to avoid double-scheduling with stale backoff.
        if (status != WS_PORTAL_STATUS_INACTIVE && (!client || !client->isValid())) {
            scheduleReconnect();
        }
    });

    API_LOG("WebSocket created for the portal: %s", qUtf8Printable(wsPortal.getName()));
}

void WsPortalClient::destroyWsSocket()
{
    if (!client) {
        return;
    }

    if (reconnectTimer) {
        reconnectTimer->stop();
        reconnectPending = false;
    }

    // Must precede client = nullptr to break any pending errorOccurred lambda dispatch.
    client->disconnect(this);
    client->close();
    client->deleteLater();
    client = nullptr;

    API_LOG("WebSocket closed: %s", qUtf8Printable(wsPortal.getFacilityView().getApiHostAndPort()));
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

    QUrl url = QUrl(wsPortal.getFacilityView().getApiUrl());
    url.setPath(WS_PORTALS_PATH);
    url.setQuery(parameters);
    API_LOG("Opening WebSocket: %s", qUtf8Printable(url.toString()));

    QMap<QByteArray, QByteArray> headers;
    headers.insert("Authorization", QString("Bearer %1").arg(apiClient->getAccessToken()).toLatin1());
    client->setHeaders(headers);
    client->open(url);
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
    reconnectTimer->stop();
    reconnectPending = false;
    reconnectPortalId = portalId;
    open(portalId);

    WsPortalEventHandler::getInstance()->subscribe(wsPortal.getEventSubscriptions());
}

void WsPortalClient::stop()
{
    if (status == WS_PORTAL_STATUS_INACTIVE) {
        return;
    }

    status = WS_PORTAL_STATUS_INACTIVE;
    reconnectTimer->stop();
    reconnectPending = false;
    reconnectPortalId.clear();
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
    reconnectCount = 0;
    reconnectTimer->stop();
    reconnectPending = false;
    API_LOG("WebSocket connected");
    emit connected();
}

int WsPortalClient::reconnectDelayMs() const
{
    // Exponential backoff: 10s, 20s, 40s, 80s, 160s, 300s (cap)
    // (reconnectCount is incremented before this is called, so the first delay is BASE_DELAY_MS * 2.)
    static constexpr int BASE_DELAY_MS = 5000;
    static constexpr int MAX_DELAY_MS = 300000;
    int delay = BASE_DELAY_MS * (1 << (std::min)(reconnectCount, 6));
    return (std::min)(delay, MAX_DELAY_MS);
}

// Reconnect signal flow:
//
//   WsClient::errorOccurred -> [scheduleReconnect()] -> [reconnectPending] -> QTimer
//   WsClient::closed        -> onDisconnected -> [scheduleReconnect()]
//
// Both paths funnel through scheduleReconnect() which is idempotent
// via the reconnectPending flag.
void WsPortalClient::scheduleReconnect()
{
    if (status == WS_PORTAL_STATUS_INACTIVE || reconnectPending) {
        return;
    }

    auto portalId = apiClient->getSettings()->getWsPortalId();
    if (portalId.isEmpty() || portalId == "none") {
        return;
    }

    reconnectPending = true;
    reconnectCount++;
    reconnectPortalId = portalId;
    int delay = reconnectDelayMs();
    API_LOG("Reconnecting in %d ms (attempt %d)", delay, reconnectCount);
    reconnectTimer->start(delay);
}

void WsPortalClient::onDisconnected()
{
    scheduleReconnect();
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
    if (!messageObj["body"].is_binary()) {
        API_LOG("Invalid message body type");
        return;
    }
    auto body = json::from_msgpack(messageObj["body"].get_binary(), true, false);
    if (body.is_discarded()) {
        API_LOG("Invalid message body data");
        return;
    }

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
    OBSWebSocketRequestResponse response(obs_websocket_call_request(qUtf8Printable(requestType), data));
    if (!response) {
        return responseJson;
    }

    obs_websocket_request_response *raw = response;
    json requestStatus = {{"code", (int)raw->status_code}, {"result", raw->status_code == 100}};
    if (raw->comment) {
        requestStatus["comment"] = raw->comment;
    }

    responseJson["requestType"] = qUtf8Printable(requestType);
    responseJson["requestId"] = qUtf8Printable(requestId);
    responseJson["requestStatus"] = requestStatus;
    if (raw->response_data) {
        auto responseData = json::parse(raw->response_data, nullptr, false);
        if (responseData.is_discarded()) {
            obs_log(LOG_WARNING, "[WsPortalClient] Failed to parse response data JSON from obs-websocket");
            responseJson["responseData"] = nullptr;
        } else {
            responseJson["responseData"] = std::move(responseData);
        }
    } else {
        responseJson["responseData"] = nullptr;
    }

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
    auto eventSubscriptions =
        (wsPortal["event_subscriptions"].isUndefined() || wsPortal["event_subscriptions"].isNull())
            ? (int)0x7FF
            : wsPortal.getEventSubscriptions();
    if (!(requiredIntent & eventSubscriptions)) {
        return;
    }

    json parsedEventData = nullptr;
    if (eventData) {
        parsedEventData = json::parse(eventData, nullptr, false);
        if (parsedEventData.is_discarded()) {
            obs_log(LOG_WARNING, "[WsPortalClient] Failed to parse event data JSON for event: %s", eventType);
            return;
        }
    }

    json data = {
        {"eventType", eventType},
        {"eventIntent", static_cast<int64_t>(requiredIntent)},
        {"eventData", std::move(parsedEventData)},
    };
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
