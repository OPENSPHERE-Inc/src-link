/*
SR Link
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
#include <QJsonDocument>

template<typename T> class TypedJsonArray : public QJsonArray {
public:
    TypedJsonArray() = default;
    TypedJsonArray(const QJsonArray &json) : QJsonArray(json) {}

    inline QList<T> values() const
    {
        QList<T> list;
        foreach (const QJsonValue item, *this) {
            list.append(item.toObject());
        }
        return list;
    }

    int findIndex(const std::function<bool(const T &)> &predicate) const
    {
        for (int i = 0; i < size(); i++) {
            if (predicate(at(i).toObject())) {
                return i;
            }
        }
        return -1;
    }

    const T find(const std::function<bool(const T &)> &predicate) const
    {
        int index = findIndex(predicate);
        if (index >= 0) {
            return at(index).toObject();
        }
        return T();
    }

    bool every(const std::function<bool(const T &)> &predicate) const
    {
        for (int i = 0; i < size(); i++) {
            if (!predicate(at(i).toObject())) {
                return false;
            }
        }
        return true;
    }

    T operator[](qsizetype i) const { return at(i).toObject(); }
};

inline bool maybe(const QJsonValue &value, bool result)
{
    return value.isNull() || value.isUndefined() || result;
}

inline const std::string dumpJsonObject(const QJsonObject &json)
{
    return QJsonDocument(json).toJson().toStdString();
}

class SubscriptionFeatures : public QJsonObject {
public:
    SubscriptionFeatures() = default;
    SubscriptionFeatures(const QJsonObject &json) : QJsonObject(json) {}

    inline bool getHostAbility() const { return value("host_ability").toBool(); }
    inline void setHostAbility(bool value) { insert("host_ability", value); }
    inline bool getGuestAbility() const { return value("guest_ability").toBool(); }
    inline void setGuestAbility(bool value) { insert("guest_ability", value); }
    inline int getMaxStages() const { return value("max_stages").toInt(); }
    inline void setMaxStages(int value) { insert("max_stages", value); }
    inline int getMaxParties() const { return value("max_parties").toInt(); }
    inline void setMaxParties(int value) { insert("max_parties", value); }
    inline int getMaxConcurrentPartyEvents() const { return value("max_concurrent_party_events").toInt(); }
    inline void setMaxConcurrentPartyEvents(int value) { insert("max_concurrent_party_events", value); }
    inline int getMaxSourcesPerStageSeat() const { return value("max_sources_per_stage_seat").toInt(); }
    inline void setMaxSourcesPerStageSeat(int value) { insert("max_sources_per_stage_seat", value); }
    inline int getMaxSeatsPerStage() const { return value("max_seats_per_stage").toInt(); }
    inline void setMaxSeatsPerStage(int value) { insert("max_seats_per_stage", value); }
    inline int getMaxMembersPerParty() const { return value("max_members_per_party").toInt(); }
    inline void setMaxMembersPerParty(int value) { insert("max_members_per_party", value); }
    inline int getMaxParticipantsPerPartyEvent() const { return value("max_participants_per_party_event").toInt(); }
    inline void setMaxParticipantsPerPartyEvent(int value) { insert("max_participants_per_party_event", value); }
    inline int getMaxUplinkDuration() const { return value("max_uplink_duration").toInt(); }
    inline void setMaxUplinkDuration(int value) { insert("max_uplink_duration", value); }
    inline QString getUiType() const { return value("ui_type").toString(); }
    inline void setUiType(const QString &value) { insert("ui_type", value); }
    inline bool getByolAbility() const { return value("byol_ability").toBool(); }
    inline void setByolAbility(bool value) { insert("byol_ability", value); }
    inline int getMaxRelayConnections() const { return value("max_relay_connections").toInt(); }
    inline void setMaxRelayConnections(int value) { insert("max_relay_connections", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["host_ability"].isBool() && (*this)["guest_ability"].isBool() &&
                     (*this)["max_stages"].isDouble() && (*this)["max_parties"].isDouble() &&
                     (*this)["max_concurrent_party_events"].isDouble() &&
                     (*this)["max_sources_per_stage_seat"].isDouble() && (*this)["max_seats_per_stage"].isDouble() &&
                     (*this)["max_members_per_party"].isDouble() &&
                     (*this)["max_participants_per_party_event"].isDouble() &&
                     (*this)["max_uplink_duration"].isDouble() && (*this)["ui_type"].isString() &&
                     (*this)["byol_ability"].isBool() && (*this)["max_relay_connections"].isDouble();
        return valid;
    }
};

class SavedSubscriptionPlan : public QJsonObject {
public:
    SavedSubscriptionPlan() = default;
    SavedSubscriptionPlan(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getDescription() const { return value("description").toString(); }
    inline void setDescription(const QString &value) { insert("description", value); }
    inline SubscriptionFeatures getFeatures() const { return value("features").toObject(); }
    inline void setFeatures(const SubscriptionFeatures &value) { insert("features", value); }
    inline int getPeriodMonths() const { return value("period_months").toInt(); }
    inline void setPeriodMonths(int value) { insert("period_months", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["name"].isString() && maybe((*this)["description"], (*this)["description"].isString()) &&
                     getFeatures().isValid() && (*this)["period_months"].isDouble();
        return valid;
    }
};

class SubscriptionLicense : public QJsonObject {
public:
    SubscriptionLicense() = default;
    SubscriptionLicense(const QJsonObject &json) : QJsonObject(json) {}

    inline SavedSubscriptionPlan getSavedPlan() const
    {
        return value("saved_plan").toObject();
    }
    inline void setSavedPlan(const SavedSubscriptionPlan &value)
    {
        insert("saved_plan", value);
    }
    inline QDateTime getStartDate() const { return QDateTime::fromString(value("start_date").toString(), Qt::ISODate); }
    inline void setStartDate(const QDateTime &value) { insert("start_date", value.toString(Qt::ISODate)); }
    inline bool getValid() const { return value("valid").toBool(); }
    inline void setValid(bool value) { insert("valid", value); }

    inline bool isValid() const
    {
        auto valid = getSavedPlan().isValid() && (*this)["start_date"].isString() &&
                     (*this)["valid"].isBool();
        return valid;
    }
};

class AccountResourceUsage : public QJsonObject {
public:
    AccountResourceUsage() = default;
    AccountResourceUsage(const QJsonObject &json) : QJsonObject(json) {}

    inline int getStages() const { return value("stages").toInt(); }
    inline void setStages(int value) { insert("stages", value); }
    inline int getParties() const { return value("parties").toInt(); }
    inline void setParties(int value) { insert("parties", value); }
    inline int getPartyEvents() const { return value("party_events").toInt(); }
    inline void setPartyEvents(int value) { insert("party_events", value); }
    inline int getConcurrentPartyEvents() const { return value("concurrent_party_events").toInt(); }
    inline void setConcurrentPartyEvents(int value) { insert("concurrent_party_events", value); }
    inline int getRelayConnections() const { return value("relay_connections").toInt(); }
    inline void setRelayConnections(int value) { insert("relay_connections", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["stages"].isDouble() && (*this)["parties"].isDouble() &&
                     (*this)["party_events"].isDouble() && (*this)["concurrent_party_events"].isDouble() &&
                     (*this)["relay_connections"].isDouble();
        return valid;
    }
};

class Account : public QJsonObject {
public:
    Account() = default;
    Account(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getId() const { return value("_id").toString(); }
    inline void setId(const QString &value) { insert("_id", value); }
    inline QString getDisplayName() const { return value("display_name").toString(); };
    inline void setDisplayName(const QString &value) { insert("display_name", value); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["_id"].isString() && (*this)["display_name"].isString() &&
                     maybe((*this)["picture_id"], (*this)["picture_id"].isString());
        return valid;
    }
};

class AccountInfo : public QJsonObject {
public:
    AccountInfo() = default;
    AccountInfo(const QJsonObject &json) : QJsonObject(json) {}

    inline Account getAccount() const { return value("account").toObject(); }
    inline void setAccount(const Account &value) { insert("account", value); }
    inline SubscriptionLicense getSubscriptionLicense() const { return value("subscription_license").toObject(); }
    inline void setSubscriptionLicense(const SubscriptionLicense &value) { insert("subscription_license", value); }
    inline AccountResourceUsage getResourceUsage() const { return value("resource_usage").toObject(); }
    inline void setResourceUsage(const AccountResourceUsage &value) { insert("resource_usage", value); }

    inline bool isValid() const
    {
        auto valid = getAccount().isValid() && getSubscriptionLicense().isValid() && getResourceUsage().isValid();
        return valid;
    }
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

    inline bool isValid() const
    {
        auto valid = (*this)["name"].isString() && (*this)["display_name"].isString() &&
                     maybe((*this)["description"], (*this)["description"].isString());
        return valid;
    }
};

typedef TypedJsonArray<StageSource> StageSourceArray;

class StageSeat : public QJsonObject {
public:
    StageSeat() = default;
    StageSeat(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getDisplayName() const { return value("display_name").toString(); }
    inline void setDisplayName(const QString &value) { insert("display_name", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["name"].isString() && (*this)["display_name"].isString();
        return valid;
    }
};

typedef TypedJsonArray<StageSeat> StageSeatArray;

class StageSeatView : public QJsonObject {
public:
    StageSeatView() = default;
    StageSeatView(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getDisplayName() const { return value("display_name").toString(); }
    inline void setDisplayName(const QString &value) { insert("display_name", value); }

    inline bool isValid() const { return (*this)["display_name"].isString(); }
};

class AccountView : public QJsonObject {
public:
    AccountView() = default;
    AccountView(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getDisplayName() const { return value("display_name").toString(); }
    inline void setDisplayName(const QString &value) { insert("display_name", value); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["display_name"].isString() &&
                     maybe((*this)["picture_id"], (*this)["picture_id"].isString());
        return valid;
    }
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
    inline StageSourceArray getSources() const { return value("sources").toArray(); }
    inline void setSources(const StageSourceArray &value) { insert("sources", value); }
    inline StageSeatArray getSeats() const { return value("seats").toArray(); }
    inline void setSeats(const StageSeatArray &value) { insert("seats", value); }
    inline AccountView getOwnerAccountView() const { return value("owner_account_view").toObject(); }
    inline void setOwnerAccountView(const AccountView &value) { insert("owner_account_view", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["_id"].isString() && (*this)["name"].isString() &&
                     maybe((*this)["description"], (*this)["description"].isString()) &&
                     maybe((*this)["picture_id"], (*this)["picture_id"].isString()) && (*this)["sources"].isArray() &&
                     getSources().every([](const StageSource &value) { return value.isValid(); }) &&
                     (*this)["seats"].isArray() &&
                     getSeats().every([](const StageSeat &value) { return value.isValid(); }) &&
                     getOwnerAccountView().isValid();
        return valid;
    }
};

class StageView : public QJsonObject {
public:
    StageView() = default;
    StageView(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }
    inline QString getDescription() const { return value("description").toString(); }
    inline void setDescription(const QString &value) { insert("description", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["name"].isString() && maybe((*this)["picture_id"], (*this)["picture_id"].isString()) &&
                     maybe((*this)["description"], (*this)["description"].isString());
        return valid;
    }
};

typedef TypedJsonArray<Stage> StageArray;

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

    inline AccountView getOwnerAccountView() const { return value("owner_account_view").toObject(); }
    inline void setOwnerAccountView(const AccountView &value) { insert("owner_account_view", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["_id"].isString() && (*this)["name"].isString() &&
                     maybe((*this)["description"], (*this)["description"].isString()) &&
                     maybe((*this)["picture_id"], (*this)["picture_id"].isString()) &&
                     maybe((*this)["capacity"], (*this)["capacity"].isDouble()) && getOwnerAccountView().isValid();
        return valid;
    }
};

class PartyView : public QJsonObject {
public:
    PartyView() = default;
    PartyView(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["name"].isString() && maybe((*this)["picture_id"], (*this)["picture_id"].isString());
        return valid;
    }
};

typedef TypedJsonArray<Party> PartyArray;

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

    inline PartyView getPartyView() const { return value("party_view").toObject(); }
    inline void setPartyView(const PartyView &value) { insert("party_view", value); }
    inline StageView getStageView() const { return value("stage_view").toObject(); }
    inline void setStageView(const StageView &value) { insert("stage_view", value); }
    inline AccountView getOwnerAccountView() const { return value("owner_account_view").toObject(); }
    inline void setOwnerAccountView(const AccountView &value) { insert("owner_account_view", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["_id"].isString() && (*this)["name"].isString() &&
                     maybe((*this)["description"], (*this)["description"].isString()) &&
                     (*this)["start_time"].isString() && maybe((*this)["end_time"], (*this)["end_time"].isString()) &&
                     maybe((*this)["picture_id"], (*this)["picture_id"].isString()) && (*this)["status"].isString() &&
                     (*this)["status_changed_at"].isString() && getPartyView().isValid() && getStageView().isValid() &&
                     getOwnerAccountView().isValid();
        return valid;
    }
};

class PartyEventView : public QJsonObject {
public:
    PartyEventView() = default;
    PartyEventView(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }
    inline QString getDesciption() const { return value("description").toString(); }
    inline void setDescription(const QString &value) { insert("description", value); }
    inline QString getStatus() const { return value("status").toString(); }
    inline void setStatus(const QString &value) { insert("status", value); }
    inline QDateTime getStatusChangedAt() const
    {
        return QDateTime::fromString(value("status_changed_at").toString(), Qt::ISODate);
    }
    inline void setStatusChangedAt(const QDateTime &value) { insert("status_changed_at", value.toString(Qt::ISODate)); }

    inline bool isValid() const
    {
        auto valid = (*this)["name"].isString() && maybe((*this)["picture_id"], (*this)["picture_id"].isString()) &&
                     maybe((*this)["description"], (*this)["description"].isString()) && (*this)["status"].isString() &&
                     (*this)["status_changed_at"].isString();
        return valid;
    }
};

typedef TypedJsonArray<PartyEvent> PartyEventArray;

class PartyEventParticipant : public QJsonObject {
public:
    PartyEventParticipant() = default;
    PartyEventParticipant(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getId() const { return value("_id").toString(); }
    inline void setId(const QString &value) { insert("_id", value); }
    inline QString getPartyEventId() const { return value("party_event_id").toString(); }
    inline void setPartyEventId(const QString &value) { insert("party_event_id", value); }
    inline QString getStageId() const { return value("stage_id").toString(); }
    inline void setStageId(const QString &value) { insert("stage_id", value); }
    inline QString getMemberId() const { return value("member_id").toString(); }
    inline void setMemberId(const QString &value) { insert("member_id", value); }
    inline QString getAccountId() const { return value("account_id").toString(); }
    inline void setAccountId(const QString &value) { insert("account_id", value); }
    inline QString getSeatName() const { return value("seat_name").toString(); }
    inline void setSeatName(const QString &value) { insert("seat_name", value); }
    inline bool getDisabled() const { return value("disabled").toBool(); }
    inline void setDisabled(bool value) { insert("disabled", value); }

    inline PartyView getPartyView() const { return value("party_view").toObject(); }
    inline void setPartyView(const PartyView &value) { insert("party_view", value); }
    inline PartyEventView getPartyEventView() const { return value("party_event_view").toObject(); }
    inline void setPartyEventView(const PartyEventView &value) { insert("party_event_view", value); }
    inline StageView getStageView() const { return value("stage_view").toObject(); }
    inline void setStageView(const StageView &value) { insert("stage_view", value); }
    inline AccountView getAccountView() const { return value("account_view").toObject(); }
    inline void setAccountView(const AccountView &value) { insert("account_view", value); }
    inline StageSeatView getStageSeatView() const { return value("stage_seat_view").toObject(); }
    inline void setStageSeatView(const StageSeatView &value) { insert("stage_seat_view", value); }
    inline bool getByol() const { return value("byol").toBool(); }
    inline void setByol(bool value) { insert("byol", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["_id"].isString() && (*this)["party_event_id"].isString() &&
                     (*this)["stage_id"].isString() && maybe((*this)["member_id"], (*this)["member_id"].isString()) &&
                     (*this)["account_id"].isString() && maybe((*this)["seat_name"], (*this)["seat_name"].isString()) &&
                     maybe((*this)["disabled"], (*this)["disabled"].isBool()) && getPartyView().isValid() &&
                     getPartyEventView().isValid() && getStageView().isValid() &&
                     maybe((*this)["account_view"], getAccountView().isValid()) && getStageSeatView().isValid() &&
                     maybe((*this)["byol"], (*this)["byol"].isBool());
        return valid;
    }
};

typedef TypedJsonArray<PartyEventParticipant> PartyEventParticipantArray;

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
    inline QString getStreamId() const { return value("stream_id").toString(); }
    inline void setStreamId(const QString &value) { insert("stream_id", value); }
    inline QString getPassphrase() const { return value("passphrase").toString(); }
    inline void setPassphrase(const QString &value) { insert("passphrase", value); }
    inline QString getParameters() const { return value("parameters").toString(); }
    inline void setParameters(const QString &value) { insert("parameters", value); }
    inline bool getRelay() const { return value("relay").toBool(); }
    inline void setRelay(bool value) { insert("relay", value); }
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
    inline bool getDisabled() const { return value("disabled").toBool(); }
    inline void setDisabled(bool value) { insert("disabled", value); }
    inline QString getAllocationId() const { return value("allocation_id").toString(); }
    inline void setAllocationId(const QString &value) { insert("allocation_id", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["_id"].isString() && (*this)["stage_id"].isString() && (*this)["seat_name"].isString() &&
                     (*this)["source_name"].isString() && (*this)["protocol"].isString() &&
                     (*this)["server"].isString() && (*this)["port"].isDouble() && (*this)["stream_id"].isString() &&
                     (*this)["passphrase"].isString() && (*this)["parameters"].isString() &&
                     (*this)["relay"].isBool() && (*this)["max_bitrate"].isDouble() &&
                     (*this)["min_bitrate"].isDouble() && (*this)["width"].isDouble() && (*this)["height"].isDouble() &&
                     (*this)["revision"].isDouble() && maybe((*this)["disabled"], (*this)["disabled"].isBool()) &&
                     maybe((*this)["allocation_id"], (*this)["allocation_id"].isString());
        return valid;
    }
};

typedef TypedJsonArray<StageConnection> StageConnectionArray;

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
    inline bool getDisabled() const { return value("disabled").toBool(); }
    inline void setDisabled(bool value) { insert("disabled", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["_id"].isString() && (*this)["party_id"].isString() &&
                     (*this)["party_event_id"].isString() && (*this)["stage_id"].isString() &&
                     (*this)["seat_name"].isString() && (*this)["member_id"].isString() &&
                     (*this)["participant_id"].isString() && (*this)["account_id"].isString() &&
                     maybe((*this)["disabled"], (*this)["disabled"].isBool());
        return valid;
    }
};

class UplinkInfo : public QJsonObject {
public:
    UplinkInfo() = default;
    UplinkInfo(const QJsonObject &json) : QJsonObject(json) {}

    inline StageSeatAllocation getAllocation() const { return value("allocation").toObject(); }
    inline void setAllocation(const StageSeatAllocation &value) { insert("allocation", value); }
    inline Stage getStage() const { return value("stage").toObject(); }
    inline void setStage(const Stage &value) { insert("stage", value); }
    inline StageConnectionArray getConnections() const { return value("connections").toArray(); }
    inline void setConnections(const StageConnectionArray &value) { insert("connections", value); }

    inline bool isValid() const
    {
        auto valid = maybe((*this)["allocation"], getAllocation().isValid()) && getStage().isValid() &&
                     (*this)["connections"].isArray() &&
                     getConnections().every([](const StageConnection &value) { return value.isValid(); });
        return valid;
    }
};

class DownlinkInfo : public QJsonObject {
public:
    DownlinkInfo() = default;
    DownlinkInfo(const QJsonObject &json) : QJsonObject(json) {}

    inline StageConnection getConnection() const { return value("connection").toObject(); }
    inline void setConnection(const StageConnection &value) { insert("connection", value); }

    inline bool isValid() const
    {
        auto valid = getConnection().isValid();
        return valid;
    }
};

class WebSocketMessage : public QJsonObject {
public:
    WebSocketMessage() = default;
    WebSocketMessage(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getEvent() const { return value("event").toString(); }
    inline void setEvent(const QString &value) { insert("event", value); }
    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getId() const { return value("id").toString(); }
    inline void setId(const QString &value) { insert("id", value); }
    inline QJsonObject getPayload() const { return value("payload").toObject(); }
    inline void setPayload(const QJsonObject &value) { insert("payload", value); }

    inline bool isValid() const
    {
        auto valid = (*this)["event"].isString() && (*this)["name"].isString() && (*this)["id"].isString() &&
                     maybe((*this)["payload"], (*this)["payload"].isObject());
        return valid;
    }
};

class DownlinkRequestBody : public QJsonObject {
public:
    DownlinkRequestBody() = default;
    DownlinkRequestBody(const QJsonObject &json) : QJsonObject(json) {}

    inline QString getStageId() const { return value("stage_id").toString(); }
    inline void setStageId(const QString &value) { insert("stage_id", value); }
    inline QString getSeatName() const { return value("seat_name").toString(); }
    inline void setSeatName(const QString &value) { insert("seat_name", value); }
    inline QString getSourceName() const { return value("source_name").toString(); }
    inline void setSourceName(const QString &value) { insert("source_name", value); }
    inline QString getProtocol() const { return value("protocol").toString(); }
    inline void setProtocol(const QString &value) { insert("protocol", value); }
    inline int getPort() const { return value("port").toInt(); }
    inline void setPort(int value) { insert("port", value); }
    inline QString getStreamId() const { return value("stream_id").toString(); }
    inline void setStreamId(const QString &value) { insert("stream_id", value); }
    inline QString getPassphrase() const { return value("passphrase").toString(); }
    inline void setPassphrase(const QString &value) { insert("passphrase", value); }
    inline QString getParameters() const { return value("parameters").toString(); }
    inline void setParameters(const QString &value) { insert("parameters", value); }
    inline bool getRelay() const { return value("relay").toBool(); }
    inline void setRelay(bool value) { insert("relay", value); }
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

    inline bool isValid() const
    {
        auto valid =
            (*this)["stage_id"].isString() && (*this)["seat_name"].isString() && (*this)["source_name"].isString() &&
            (*this)["protocol"].isString() && (*this)["port"].isDouble() && (*this)["stream_id"].isString() &&
            (*this)["passphrase"].isString() && (*this)["parameters"].isString() && (*this)["relay"].isBool() &&
            (*this)["max_bitrate"].isDouble() && (*this)["min_bitrate"].isDouble() && (*this)["width"].isDouble() &&
            (*this)["height"].isDouble() && (*this)["revision"].isDouble();
        return valid;
    }
};
