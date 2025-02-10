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
#include <eventhandler/EventHandler.h>

#include <QObject>
#include <QList>
#include <QMutex>

class WsPortalEventHandler : public QObject {
    Q_OBJECT

    // Singleton instance
    static WsPortalEventHandler *instance;

    static void onOBSEvent(uint64_t requiredIntent, std::string eventType, json eventData, uint8_t rpcVersion);
    static void onOBSReady(bool ready);

    bool ready;
    EventHandlerPtr eventHandler;
    QList<obs_websocket_event_callback> eventCallbacks;
    QMutex outputMutex;

    void broadcastEvent(
        uint64_t requiredIntent, const std::string &eventType, const json &eventData = nullptr, uint8_t rpcVersion = 0
    );

protected:
    explicit WsPortalEventHandler(QObject *parent = nullptr);
    ~WsPortalEventHandler();

public:
    static WsPortalEventHandler *getInstance();
    static void destroyInstance();

    void registerEventCallback(obs_websocket_event_callback_function eventCallback, void *privData);
    void unregisterEventCallback(obs_websocket_event_callback_function eventCallback, void *privData);
    void subscribe(uint64_t eventSubscriptions);
    void unsubscribe(uint64_t eventSubscriptions);
};

// Equation operator for QList
inline bool operator==(const obs_websocket_event_callback &lhs, const obs_websocket_event_callback &rhs)
{
    return lhs.callback == rhs.callback && lhs.priv_data == rhs.priv_data;
}
