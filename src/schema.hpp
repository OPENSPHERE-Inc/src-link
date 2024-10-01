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

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QMap>

class AccountInfo : public QJsonObject {
public:
    AccountInfo() = default;
    AccountInfo(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getId() const { return value("_id").toString(); }
    inline void setId(const QString &value) { insert("_id", value); }
    inline QString getDisplayName() const { return value("display_name").toString(); };
    inline void setDisplayName(const QString &value) { insert("display_name", value); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }
};

class StageSource : public QJsonObject {
public:
    StageSource() = default;
    StageSource(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getDisplayName() const { return value("display_name").toString(); }
    inline void setDisplayName(const QString &value) { insert("display_name", value); }
    inline QString getDescription() const { return value("description").toString(); }
    inline void setDescription(const QString &value) { insert("description", value); }
};

class StageSeat : public QJsonObject {
public:
    StageSeat() = default;
    StageSeat(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getDisplayName() const { return value("display_name").toString(); }
    inline void setDisplayName(const QString &value) { insert("display_name", value); }
};

class Stage : public QJsonObject {
public:
    Stage() = default;
    Stage(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getId() const { return value("_id").toString(); }
    inline void setId(const QString &value) { insert("_id", value); }
    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getDescription() const { return value("description").toString(); }
    inline void setDescription(const QString &value) { insert("description", value); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }
    inline QList<StageSource> getSources() const
    {
        QList<StageSource> sources;
        foreach (const QJsonValue sourceItem, value("sources").toArray()) {
            sources.append(sourceItem.toObject());
        }
        return sources;
    }
    inline void setSources(const QList<StageSource> &value)
    {
        QJsonArray sourcesArray;
        foreach (const StageSource &source, value) {
            sourcesArray.append(source);
        }
        insert("sources", sourcesArray);
    }
    inline QList<StageSeat> getSeats() const
    {
        QList<StageSeat> seats;
        foreach (const QJsonValue seatItem, value("seats").toArray()) {
            seats.append(seatItem.toObject());
        }
        return seats;
    }
    inline void setSeats(const QList<StageSeat> &value)
    {
        QJsonArray seatsArray;
        foreach (const StageSeat &seat, value) {
            seatsArray.append(seat);
        }
        insert("seats", seatsArray);
    }
};

class StageArray : public QJsonArray {
public:
    StageArray() = default;
    StageArray(const QJsonArray &json) : QJsonArray(json) {}

    inline QList<Stage> values() const
    {
        QList<Stage> stages;
        foreach (const QJsonValue stageItem, *this) {
            stages.append(stageItem.toObject());
        }
        return stages;
    }

    Stage operator[](qsizetype i) const { return at(i).toObject(); }
};

class Party : public QJsonObject {
public:
    Party() = default;
    Party(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getId() const { return value("_id").toString(); }
    inline void setId(const QString &value) { insert("_id", value); }
    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getDescription() const { return value("description").toString(); }
    inline void setDescription(const QString &value) { insert("description", value); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }
    inline int getCapacity() const { return value("capacity").toInt(); }
    inline void setCapacity(int value) { insert("capacity", value); }
};

class PartyArray : public QJsonArray {
public:
    PartyArray() = default;
    PartyArray(const QJsonArray &json) : QJsonArray(json) {}

    inline QList<Party> values() const
    {
        QList<Party> parties;
        foreach (const QJsonValue partyItem, *this) {
            parties.append(partyItem.toObject());
        }
        return parties;
    }

    Party operator[](qsizetype i) const { return at(i).toObject(); }
};

class PartyEvent : public QJsonObject {
public:
    PartyEvent() = default;
    PartyEvent(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getId() const { return value("_id").toString(); }
    inline void setId(const QString &value) { insert("_id", value); }
    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getDescription() const { return value("description").toString(); }
    inline void setDescription(const QString &value) { insert("description", value); }
    inline QDateTime getStartTime() const { return QDateTime::fromString(value("start_time").toString(), Qt::ISODate); }
    inline void setStartTime(const QDateTime &value) { insert("start_time", value.toString(Qt::ISODate)); }
    inline QDateTime getEndTime() const { return QDateTime::fromString(value("end_time").toString(), Qt::ISODate); }
    inline void setEndTime(const QDateTime &value) { insert("end_time", value.toString(Qt::ISODate)); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }
    inline QString getStatus() const { return value("status").toString(); }
    inline void setStatus(const QString &value) { insert("status", value); }
    inline QDateTime getStatusChangedAt() const
    {
        return QDateTime::fromString(value("status_changed_at").toString(), Qt::ISODate);
    }
    inline void setStatusChangedAt(const QDateTime &value) { insert("status_changed_at", value.toString(Qt::ISODate)); }
    inline Party getParty() const { return value("party").toObject(); }
    inline void setParty(const Party &value) { insert("party", value); }
    inline Stage getStage() const { return value("stage").toObject(); }
    inline void setStage(const Stage &value) { insert("stage", value); }
};

class PartyEventArray : public QJsonArray {
public:
    PartyEventArray() = default;
    PartyEventArray(const QJsonArray &json) : QJsonArray(json) {}

    inline QList<PartyEvent> values() const
    {
        QList<PartyEvent> partyEvents;
        foreach (const QJsonValue partyEventItem, *this) {
            partyEvents.append(partyEventItem.toObject());
        }
        return partyEvents;
    }

    PartyEvent operator[](qsizetype i) const { return at(i).toObject(); }
};

class StageConnection : public QJsonObject {
public:
    StageConnection() = default;
    StageConnection(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getId() const { return value("_id").toString(); }
    inline void setId(const QString &value) { insert("_id", value); }
    inline QString getStageId() const { return value("stage_id").toString(); }
    inline void setStageId(const QString &value) { insert("stage_id", value); }
    inline QString getSeatName() const { return value("seat_name").toString(); }
    inline void setSeatName(const QString &value) { insert("seat_name", value); }
    inline QString getSourceName() const { return value("source_name").toString(); }
    inline void setSourceName(const QString &value) { insert("source_name", value); }
    inline QString getProtocol() const { return value("protocol").toString(); }
    inline void setProtocol(const QString &value) { insert("protocol", value); }
    inline QString getServer() const { return value("server").toString(); }
    inline void setServer(const QString &value) { insert("server", value); }
    inline int getPort() const { return value("port").toInt(); }
    inline void setPort(int value) { insert("port", value); }
    inline QString getParameters() const { return value("parameters").toString(); }
    inline void setParameters(const QString &value) { insert("parameters", value); }
    inline int getMaxBitrate() const { return value("max_bitrate").toInt(); }
    inline void setMaxBitrate(int value) { insert("max_bitrate", value); }
    inline int getMinBitrate() const { return value("min_bitrate").toInt(); }
    inline void setMinBitrate(int value) { insert("min_bitrate", value); }
    inline int getWidth() const { return value("width").toInt(); }
    inline void setWidth(int value) { insert("width", value); }
    inline int getHeight() const { return value("height").toInt(); }
    inline void setHeight(int value) { insert("height", value); }
    inline int getRevision() const { return value("revision").toInt(); }
    inline void setRevision(int value) { insert("revision", value); }
};

class StageSeatAllocation : public QJsonObject {
public:
    StageSeatAllocation() = default;
    StageSeatAllocation(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getId() const { return value("_id").toString(); }
    inline void setId(const QString &value) { insert("_id", value); }
    inline QString getPartyId() const { return value("party_id").toString(); }
    inline void setPartyId(const QString &value) { insert("party_id", value); }
    inline QString getPartyEventId() const { return value("party_event_id").toString(); }
    inline void setPartyEventId(const QString &value) { insert("party_event_id", value); }
    inline QString getStageId() const { return value("stage_id").toString(); }
    inline void setStageId(const QString &value) { insert("stage_id", value); }
    inline QString getSeatName() const { return value("seat_name").toString(); }
    inline void setSeatName(const QString &value) { insert("seat_name", value); }
    inline QString getMemberId() const { return value("member_id").toString(); }
    inline void setMemberId(const QString &value) { insert("member_id", value); }
    inline QString getParticipantId() const { return value("participant_id").toString(); }
    inline void setParticipantId(const QString &value) { insert("participant_id", value); }
    inline QString getAccountId() const { return value("account_id").toString(); }
    inline void setAccountId(const QString &value) { insert("account_id", value); }
    inline QString getUuid() const { return value("uuid").toString(); }
    inline void setUuid(const QString &value) { insert("uuid", value); }
};

class StageSeatInfo : public QJsonObject {
public:
    StageSeatInfo() = default;
    StageSeatInfo(const QJsonObject &json) : QJsonObject(json) {}

    inline StageSeatAllocation getAllocation() const { return value("allocation").toObject(); }
    inline void setAllocation(const StageSeatAllocation &value) { insert("allocation", value); }
    inline Stage getStage() const { return value("stage").toObject(); }
    inline void setStage(const Stage &value) { insert("stage", value); }
    inline QList<StageConnection> getConnections() const
    {
        QList<StageConnection> connections;
        foreach (const QJsonValue connectionItem, value("connections").toArray()) {
            connections.append(connectionItem.toObject());
        }
        return connections;
    }
    inline void setConnections(const QList<StageConnection> &value)
    {
        QJsonArray connectionsArray;
        foreach (const StageConnection &connection, value) {
            connectionsArray.append(connection);
        }
        insert("connections", connectionsArray);
    }
};

class StageConnectionInfo : public QJsonObject {
public:
    StageConnectionInfo() = default;
    StageConnectionInfo(const QJsonObject &json) : QJsonObject(json) {}

    inline StageConnection getConnection() const { return value("connection").toObject(); }
    inline void setConnection(const StageConnection &value) { insert("connection", value); }
    inline StageSeatAllocation getAllocation() const { return value("allocation").toObject(); }
    inline void setAllocation(const StageSeatAllocation &value) { insert("allocation", value); }
};