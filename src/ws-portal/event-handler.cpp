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

#include "event-handler.hpp"


//--- WsPortalEventHandler class ---//

WsPortalEventHandler* WsPortalEventHandler::instance = nullptr;

WsPortalEventHandler::WsPortalEventHandler(QObject *parent)
    : QObject(parent),
      ready(false)
{
    eventHandler = std::make_shared<EventHandler>();
    eventHandler->SetEventCallback(onOBSEvent);
    eventHandler->SetObsReadyCallback(onOBSReady);
}

WsPortalEventHandler::~WsPortalEventHandler()
{
    eventHandler->SetEventCallback(nullptr);
    eventHandler->SetObsReadyCallback(nullptr);
    eventHandler = nullptr;
}

WsPortalEventHandler* WsPortalEventHandler::getInstance()
{
    if (!instance) {
        instance = new WsPortalEventHandler();
    }
    return instance;
}

void WsPortalEventHandler::destroyInstance()
{
    if (instance) {
        delete instance;
        instance = nullptr;
    }
}

void WsPortalEventHandler::onOBSEvent(uint64_t requiredIntent, std::string eventType, json eventData, uint8_t rpcVersion)
{
    auto instance = getInstance();
    instance->broadcastEvent(requiredIntent, eventType, eventData, rpcVersion);
}

void WsPortalEventHandler::onOBSReady(bool ready)
{
    auto instance = getInstance();
    instance->ready = ready;
}

void WsPortalEventHandler::broadcastEvent(uint64_t requiredIntent, const std::string &eventType, const json &eventData, uint8_t rpcVersion)
{
    if (!ready) {
        return;
    }

    QMutexLocker locker(&outputMutex);
    {
        for (auto &cb : eventCallbacks) {
            cb.callback(requiredIntent, eventType.c_str(), eventData.dump().c_str(), cb.priv_data);
        }
    }
    locker.unlock();
}

void WsPortalEventHandler::registerEventCallback(obs_websocket_event_callback_function eventCallback, void *privData)
{
    QMutexLocker locker(&outputMutex);
    {
        obs_websocket_event_callback cb = { eventCallback, privData };
        if (!eventCallbacks.contains(cb)) {
            eventCallbacks.append(cb);        
        }
    }
    locker.unlock();
}

void WsPortalEventHandler::unregisterEventCallback(obs_websocket_event_callback_function eventCallback, void *privData)
{
    QMutexLocker locker(&outputMutex);
    {
        obs_websocket_event_callback cb = { eventCallback, privData };
        eventCallbacks.removeAll(cb);
    }
    locker.unlock();
}

void WsPortalEventHandler::subscribe(uint64_t eventSubscriptions)
{
    eventHandler->ProcessSubscriptionChange(true, eventSubscriptions);
}

void WsPortalEventHandler::unsubscribe(uint64_t eventSubscriptions)
{
    eventHandler->ProcessSubscriptionChange(false, eventSubscriptions);
}
