/*
Source Link
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

#include <obs.hpp>

#include <o2.h>

class SourceLinkSettingsStore : public O0AbstractStore {
    Q_OBJECT

    OBSDataAutoRelease settingsData;

public:
    explicit SourceLinkSettingsStore(QObject *parent = nullptr);
    ~SourceLinkSettingsStore();

    QString value(const QString &key, const QString &defaultValue = QString());
    void setValue(const QString &key, const QString &value);

    inline void setPartyId(const QString &partyId) { setValue("partyId", partyId); }
    inline const QString getPartyId() { return value("partyId"); }
    inline void setPartyEventId(const QString &partyEventId) { setValue("partyEventId", partyEventId); }
    inline const QString getPartyEventId() { return value("partyEventId"); }
    inline void setForceConnection(const bool forceConnection)
    {
        setValue("forceConnection", forceConnection ? "true" : "false");
    }
    inline const bool getForceConnection() { return value("forceConnection", "false") == "true"; }
    inline int getIngressPortMax() { return value("ingress.portMax", "10099").toInt(); }
    inline void setIngressPortMax(int value) { setValue("ingress.portMax", QString::number(value)); }
    inline int getIngressPortMin() { return value("ingress.portMin", "10000").toInt(); }
    inline void setIngressPortMin(int value) { setValue("ingress.portMin", QString::number(value)); }
    inline QString getIngressProtocol() { return value("ingress.protocol", "srt"); }
    inline void setIngressProtocol(const QString &value) { setValue("ingress.protocol", value); }
    inline QString getIngressSrtMode() { return value("ingress.srtMode", "listener"); }
    inline void setIngressSrtMode(const QString &value) { setValue("ingress.srtMode", value); }
    inline int getIngressSrtLatency() { return value("ingress.srtLatency", "200").toInt(); }
    inline void setIngressSrtLatency(int value) { setValue("ingress.srtLatency", QString::number(value)); }
    inline int getIngressSrtPbkeylen() { return value("ingress.srtPbkeylen", "16").toInt(); }
    inline void setIngressSrtPbkeylen(int value) { setValue("ingress.srtPbkeylen", QString::number(value)); }
    inline bool getIngressAdvancedSettings() { return value("ingress.advancedSettings", "false") == "true"; }
    inline void setIngressAdvancedSettings(bool value)
    {
        setValue("ingress.advancedSettings", value ? "true" : "false");
    }
    inline int getIngressReconnectDelayTime() { return value("ingress.reconnectDelayTime", "1").toInt(); }
    inline void setIngressReconnectDelayTime(int value) { setValue("ingress.reconnectDelayTime", QString::number(value)); }
    inline int getIngressNetworkBufferSize() { return value("ingress.networkBufferSize", "1").toInt(); }
    inline void setIngressNetworkBufferSize(int value) { setValue("ingress.networkBufferSize", QString::number(value)); }
};
