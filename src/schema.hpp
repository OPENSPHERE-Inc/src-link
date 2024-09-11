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

class AccountInfo : public QObject {
    Q_OBJECT

    QString id;
    QString displayName;
    QString pictureId;

public:
    explicit inline AccountInfo(QObject *parent = nullptr) : QObject(parent) {}

    inline QString getId() const { return id; }
    inline void setId(const QString &value) { id = value; }
    inline QString getDisplayName() const { return displayName; };
    inline void setDisplayName(const QString &value) { displayName = value; }
    inline QString getPictureId() const { return pictureId; }
    inline void setPictureId(const QString &value) { pictureId = value; }

    static inline AccountInfo *fromJsonObject(const QJsonObject &json, QObject *parent = nullptr)
    {
        AccountInfo *accountInfo = new AccountInfo(parent);
        accountInfo->setId(json["_id"].toString());
        accountInfo->setDisplayName(json["display_name"].toString());
        accountInfo->setPictureId(json["picture_id"].toString());
        return accountInfo;
    }
};

class Stage : public QObject {
    Q_OBJECT

    QString id;
    QString name;
    QString description;
    QString pictureId;

    struct StageSource_t {
        QString name;
        QString displayName;
        QString description;
    };

    struct StageSeat_t {
        QString name;
        QString displayName;
    };

    QList<StageSource_t> sources;
    QList<StageSeat_t> seats;

public:
    explicit inline Stage(QObject *parent = nullptr) : QObject(parent) {}

    inline QString getId() const { return id; }
    inline void setId(const QString &value) { id = value; }
    inline QString getName() const { return name; }
    inline void setName(const QString &value) { name = value; }
    inline QString getDescription() const { return description; }
    inline void setDescription(const QString &value) { description = value; }
    inline QString getPictureId() const { return pictureId; }
    inline void setPictureId(const QString &value) { pictureId = value; }
    inline QList<StageSource_t> getSources() const { return sources; }
    inline void setSources(const QList<StageSource_t> &value) { sources = value; }
    inline QList<StageSeat_t> getSeats() const { return seats; }
    inline void setSeats(const QList<StageSeat_t> &value) { seats = value; }

    static inline Stage *fromJsonObject(const QJsonObject &json, QObject *parent = nullptr)
    {
        Stage *stage = new Stage(parent);
        stage->setId(json["_id"].toString());
        stage->setName(json["name"].toString());
        stage->setDescription(json["description"].toString());
        stage->setPictureId(json["picture_id"].toString());

        foreach(const QJsonValue sourceItem, json["sources"].toArray())
        {
            StageSource_t source;
            const auto sourceJson = sourceItem.toObject();
            source.name = sourceJson["name"].toString();
            source.displayName = sourceJson["display_name"].toString();
            source.description = sourceJson["description"].toString();
            stage->sources.append(source);
        }

        foreach(const QJsonValue seatItem, json["seats"].toArray())
        {
            StageSeat_t seat;
            const auto seatJson = seatItem.toObject();
            seat.name = seatJson["name"].toString();
            seat.displayName = seatJson["display_name"].toString();
            stage->seats.append(seat);
        }

        return stage;
    }
};

class Party : public QObject {
    Q_OBJECT

    QString id;
    QString name;
    QString description;
    QString pictureId;
    int capacity;

public:
    explicit inline Party(QObject *parent = nullptr) : QObject(parent) {}

    inline QString getId() const { return id; }
    inline void setId(const QString &value) { id = value; }
    inline QString getName() const { return name; }
    inline void setName(const QString &value) { name = value; }
    inline QString getDescription() const { return description; }
    inline void setDescription(const QString &value) { description = value; }
    inline QString getPictureId() const { return pictureId; }
    inline void setPictureId(const QString &value) { pictureId = value; }
    inline int getCapacity() const { return capacity; }
    inline void setCapacity(int value) { capacity = value; }

    static inline Party *fromJsonObject(const QJsonObject &json, QObject *parent = nullptr)
    {
        Party *party = new Party(parent);
        party->setId(json["_id"].toString());
        party->setName(json["name"].toString());
        party->setDescription(json["description"].toString());
        party->setPictureId(json["picture_id"].toString());
        party->setCapacity(json["capacity"].toInt());
        return party;
    }
};

class PartyEvent : public QObject {
    Q_OBJECT

    QString id;
    QString name;
    QString description;
    QDateTime startTime;
    QDateTime endTime;
    QString pictureId;
    QString status;
    QDateTime statusChangedAt;

    Party *party;
    Stage *stage;

public:
    explicit inline PartyEvent(QObject *parent = nullptr) : QObject(parent) {}

    inline QString getId() const { return id; }
    inline void setId(const QString &value) { id = value; }
    inline QString getName() const { return name; }
    inline void setName(const QString &value) { name = value; }
    inline QString getDescription() const { return description; }
    inline void setDescription(const QString &value) { description = value; }
    inline QDateTime getStartTime() const { return startTime; }
    inline void setStartTime(const QDateTime &value) { startTime = value; }
    inline QDateTime getEndTime() const { return endTime; }
    inline void setEndTime(const QDateTime &value) { endTime = value; }
    inline QString getPictureId() const { return pictureId; }
    inline void setPictureId(const QString &value) { pictureId = value; }
    inline QString getStatus() const { return status; }
    inline void setStatus(const QString &value) { status = value; }
    inline QDateTime getStatusChangedAt() const { return statusChangedAt; }
    inline void setStatusChangedAt(const QDateTime &value) { statusChangedAt = value; }
    inline Party *getParty() const { return party; }
    inline void setParty(Party *value) { party = value; }

    static inline PartyEvent *fromJsonObject(const QJsonObject &json, QObject *parent = nullptr)
    {
        PartyEvent *partyEvent = new PartyEvent(parent);
        partyEvent->setId(json["_id"].toString());
        partyEvent->setName(json["name"].toString());
        partyEvent->setDescription(json["description"].toString());
        partyEvent->setStartTime(QDateTime::fromString(json["start_time"].toString(), Qt::ISODate));
        partyEvent->setEndTime(QDateTime::fromString(json["end_time"].toString(), Qt::ISODate));
        partyEvent->setPictureId(json["picture_id"].toString());
        partyEvent->setStatus(json["status"].toString());
        partyEvent->setStatusChangedAt(QDateTime::fromString(json["status_changed_at"].toString(), Qt::ISODate));

        partyEvent->party = Party::fromJsonObject(json["party"].toObject(), partyEvent);
        partyEvent->stage = Stage::fromJsonObject(json["stage"].toObject(), partyEvent);

        return partyEvent;
    }
};

class StageConnection : public QObject {
    Q_OBJECT

    QString id;
    QString stageId;
    QString seatName;
    QString sourceName;
    QString protocol;
    QString server;
    int port;
    QString parameters;
    int maxBitrate;
    int minBitrate;
    int width;
    int height;

public:
    explicit inline StageConnection(QObject *parent = nullptr) : QObject(parent) {}

    inline QString getId() const { return id; }
    inline void setId(const QString &value) { id = value; }
    inline QString getStageId() const { return stageId; }
    inline void setStageId(const QString &value) { stageId = value; }
    inline QString getSeatName() const { return seatName; }
    inline void setSeatName(const QString &value) { seatName = value; }
    inline QString getSourceName() const { return sourceName; }
    inline void setSourceName(const QString &value) { sourceName = value; }
    inline QString getProtocol() const { return protocol; }
    inline void setProtocol(const QString &value) { protocol = value; }
    inline QString getServer() const { return server; }
    inline void setServer(const QString &value) { server = value; }
    inline int getPort() const { return port; }
    inline void setPort(int value) { port = value; }
    inline QString getParameters() const { return parameters; }
    inline void setParameters(const QString &value) { parameters = value; }
    inline int getMaxBitrate() const { return maxBitrate; }
    inline void setMaxBitrate(int value) { maxBitrate = value; }
    inline int getMinBitrate() const { return minBitrate; }
    inline void setMinBitrate(int value) { minBitrate = value; }
    inline int getWidth() const { return width; }
    inline void setWidth(int value) { width = value; }
    inline int getHeight() const { return height; }
    inline void setHeight(int value) { height = value; }

    static inline StageConnection *fromJsonObject(const QJsonObject &json, QObject *parent = nullptr)
    {
        StageConnection *connection = new StageConnection(parent);
        connection->setId(json["_id"].toString());
        connection->setStageId(json["stage_id"].toString());
        connection->setSeatName(json["seat_name"].toString());
        connection->setSourceName(json["source_name"].toString());
        connection->setProtocol(json["protocol"].toString());
        connection->setServer(json["server"].toString());
        connection->setPort(json["port"].toInt());
        connection->setParameters(json["parameters"].toString());
        connection->setMaxBitrate(json["max_bitrate"].toInt());
        connection->setMinBitrate(json["min_bitrate"].toInt());
        connection->setWidth(json["width"].toInt());
        connection->setHeight(json["height"].toInt());
        return connection;
    }

    static inline QList<StageConnection *> fromJsonArray(const QJsonArray &jsonArray, QObject *parent = nullptr)
    {
        QList<StageConnection *> connections;
        foreach(const QJsonValue connectionItem, jsonArray)
        {
            connections.append(fromJsonObject(connectionItem.toObject(), parent));
        }
        return connections;
    }
};

class StageSeatAllocation : public QObject {
    Q_OBJECT

    QString id;
    QString partyId;
    QString partyEventId;
    QString stageId;
    QString seatName;
    QString memberId;
    QString participantId;
    QString accountId;
    QString uuid;

public:
    explicit inline StageSeatAllocation(QObject *parent = nullptr) : QObject(parent) {}

    inline QString getId() const { return id; }
    inline void setId(const QString &value) { id = value; }
    inline QString getPartyId() const { return partyId; }
    inline void setPartyId(const QString &value) { partyId = value; }
    inline QString getPartyEventId() const { return partyEventId; }
    inline void setPartyEventId(const QString &value) { partyEventId = value; }
    inline QString getStageId() const { return stageId; }
    inline void setStageId(const QString &value) { stageId = value; }
    inline QString getSeatName() const { return seatName; }
    inline void setSeatName(const QString &value) { seatName = value; }
    inline QString getMemberId() const { return memberId; }
    inline void setMemberId(const QString &value) { memberId = value; }
    inline QString getParticipantId() const { return participantId; }
    inline void setParticipantId(const QString &value) { participantId = value; }
    inline QString getAccountId() const { return accountId; }
    inline void setAccountId(const QString &value) { accountId = value; }
    inline QString getUuid() const { return uuid; }
    inline void setUuid(const QString &value) { uuid = value; }

    static inline StageSeatAllocation *fromJsonObject(const QJsonObject &json, QObject *parent = nullptr)
    {
        StageSeatAllocation *allocation = new StageSeatAllocation(parent);
        allocation->setId(json["_id"].toString());
        allocation->setPartyId(json["party_id"].toString());
        allocation->setPartyEventId(json["party_event_id"].toString());
        allocation->setStageId(json["stage_id"].toString());
        allocation->setSeatName(json["seat_name"].toString());
        allocation->setMemberId(json["member_id"].toString());
        allocation->setParticipantId(json["participant_id"].toString());
        allocation->setAccountId(json["account_id"].toString());
        allocation->setUuid(json["uuid"].toString());
        return allocation;
    }
};

class StageSeatInfo : public QObject {
    Q_OBJECT

    StageSeatAllocation *allocation;
    Stage *stage;
    QList<StageConnection *> connections;

public:
    explicit inline StageSeatInfo(QObject *parent = nullptr) : QObject(parent) {}

    inline StageSeatAllocation *getAllocation() const { return allocation; }
    inline void setAllocation(StageSeatAllocation *value) { allocation = value; }
    inline Stage *getStage() const { return stage; }
    inline void setStage(Stage *value) { stage = value; }
    inline QList<StageConnection *> getConnections() const { return connections; }
    inline void setConnections(const QList<StageConnection *> &value) { connections = value; }

    static inline StageSeatInfo *fromJsonObject(const QJsonObject &json, QObject *parent = nullptr)
    {
        StageSeatInfo *info = new StageSeatInfo(parent);
        info->setAllocation(
            json["allocation"].isObject() ? StageSeatAllocation::fromJsonObject(json["allocation"].toObject(), info)
                                          : nullptr
        );
        info->setStage(json["stage"].isObject() ? Stage::fromJsonObject(json["stage"].toObject(), info) : nullptr);
        info->setConnections(StageConnection::fromJsonArray(json["connections"].toArray(), info));
        return info;
    }
};
