/*
SRC-Link
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

//#define SCHEMA_DEBUG

#ifdef SCHEMA_DEBUG
#include <obs-module.h>
#include "plugin-support.h"
#endif

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
    TypedJsonArray(const QJsonArray &_json) : QJsonArray(_json) {}

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

inline const std::string dumpJsonObject(const QJsonObject &_json)
{
    return QJsonDocument(_json).toJson().toStdString();
}

class SubscriptionFeatures : public QJsonObject {
public:
    SubscriptionFeatures() = default;
    SubscriptionFeatures(const QJsonObject &_json) : QJsonObject(_json) {}

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
    inline int getMaxSrtrelayServersPerStage() const { return value("max_srtrelay_servers_per_stage").toInt(); }
    inline void setMaxSrtrelayServersPerStage(int value) { insert("max_srtrelay_servers_per_stage", value); }
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

    inline bool isValid() const
    {
        auto hostAbilityValid = (*this)["host_ability"].isBool();
        auto guestAbilityValid = (*this)["guest_ability"].isBool();
        auto maxStagesValid = (*this)["max_stages"].isDouble();
        auto maxPartiesValid = (*this)["max_parties"].isDouble();
        auto maxConcurrentPartyEventsValid = (*this)["max_concurrent_party_events"].isDouble();
        auto maxSourcesPerStageSeatValid = (*this)["max_sources_per_stage_seat"].isDouble();
        auto validMaxSeatsPerStage = (*this)["max_seats_per_stage"].isDouble();
        auto validMaxSrtrelayServersPerStage = (*this)["max_srtrelay_servers_per_stage"].isDouble();
        auto validMaxMembersPerParty = (*this)["max_members_per_party"].isDouble();
        auto validMaxParticipantsPerPartyEvent = (*this)["max_participants_per_party_event"].isDouble();
        auto validMaxUplinkDuration = (*this)["max_uplink_duration"].isDouble();
        auto validUiType = (*this)["ui_type"].isString();
        auto validByolAbility = (*this)["byol_ability"].isBool();

        auto valid = hostAbilityValid && guestAbilityValid && maxStagesValid && maxPartiesValid &&
                     maxConcurrentPartyEventsValid && maxSourcesPerStageSeatValid && validMaxSeatsPerStage &&
                     validMaxSrtrelayServersPerStage && validMaxMembersPerParty && validMaxParticipantsPerPartyEvent &&
                     validMaxUplinkDuration && validUiType && validByolAbility;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR,
            "SubscriptionFeatures: host_ability=%d, guest_ability=%d, max_stages=%d, max_parties=%d, max_concurrent_party_events=%d, "
            "max_sources_per_stage_seat=%d, max_seats_per_stage=%d, max_srtrelay_servers_per_stage=%d, max_members_per_party=%d, "
            "max_participants_per_party_event=%d, max_uplink_duration=%d, ui_type=%d, byol_ability=%d",
            hostAbilityValid, guestAbilityValid, maxStagesValid, maxPartiesValid, maxConcurrentPartyEventsValid,
            maxSourcesPerStageSeatValid, validMaxSeatsPerStage, validMaxSrtrelayServersPerStage,
            validMaxMembersPerParty, validMaxParticipantsPerPartyEvent, validMaxUplinkDuration, validUiType,
            validByolAbility
        );
#endif

        return valid;
    }
};

class SavedSubscriptionPlan : public QJsonObject {
public:
    SavedSubscriptionPlan() = default;
    SavedSubscriptionPlan(const QJsonObject &_json) : QJsonObject(_json) {}

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
        auto validName = (*this)["name"].isString();
        auto validDescription = maybe((*this)["description"], (*this)["description"].isString());
        auto validFeatures = getFeatures().isValid();
        auto validPeriodMonths = (*this)["period_months"].isDouble();

        auto valid = validName && validDescription && validFeatures && validPeriodMonths;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR,
            "SavedSubscriptionPlan: name=%d, description=%d, features=%d, period_months=%d", validName,
            validDescription, validFeatures, validPeriodMonths
        );
#endif

        return valid;
    }
};

class SubscriptionLicense : public QJsonObject {
public:
    SubscriptionLicense() = default;
    SubscriptionLicense(const QJsonObject &_json) : QJsonObject(_json) {}

    inline SavedSubscriptionPlan getSavedPlan() const { return value("saved_plan").toObject(); }
    inline void setSavedPlan(const SavedSubscriptionPlan &value) { insert("saved_plan", value); }
    inline QDateTime getStartDate() const { return QDateTime::fromString(value("start_date").toString(), Qt::ISODate); }
    inline void setStartDate(const QDateTime &value) { insert("start_date", value.toString(Qt::ISODate)); }
    inline bool getLicenseValid() const { return value("valid").toBool(); }
    inline void setLicenseValid(bool value) { insert("valid", value); }

    inline bool isValid() const
    {
        auto validSavedPlan = getSavedPlan().isValid();
        auto validStartDate = (*this)["start_date"].isString();
        auto validValid = (*this)["valid"].isBool();

        auto valid = validSavedPlan && validStartDate && validValid;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR, "SubscriptionLicense: saved_plan=%d, start_date=%d, valid=%d",
            validSavedPlan, validStartDate, validValid
        );
#endif

        return valid;
    }
};

class AccountResourceUsage : public QJsonObject {
public:
    AccountResourceUsage() = default;
    AccountResourceUsage(const QJsonObject &_json) : QJsonObject(_json) {}

    inline int getStages() const { return value("stages").toInt(); }
    inline void setStages(int value) { insert("stages", value); }
    inline int getParties() const { return value("parties").toInt(); }
    inline void setParties(int value) { insert("parties", value); }
    inline int getPartyEvents() const { return value("party_events").toInt(); }
    inline void setPartyEvents(int value) { insert("party_events", value); }
    inline int getConcurrentPartyEvents() const { return value("concurrent_party_events").toInt(); }
    inline void setConcurrentPartyEvents(int value) { insert("concurrent_party_events", value); }
    inline int getMaxStageSources() const { return value("max_stage_sources").toInt(); }
    inline void setMaxStageSources(int value) { insert("max_stage_sources", value); }
    inline int getMaxStageSeats() const { return value("max_stage_seats").toInt(); }
    inline void setMaxStageSeats(int value) { insert("max_stage_seats", value); }
    inline int getMaxSrtrelayServers() const { return value("max_srtrelay_servers").toInt(); }
    inline void setMaxSrtrelayServers(int value) { insert("max_srtrelay_servers", value); }
    inline int getMaxPartyMembers() const { return value("max_party_members").toInt(); }
    inline void setMaxPartyMembers(int value) { insert("max_party_members", value); }
    inline int getMaxPartyEventParticipants() const { return value("max_party_event_participants").toInt(); }
    inline void setMaxPartyEventParticipants(int value) { insert("max_party_event_participants", value); }

    inline bool isValid() const
    {
        auto validStages = (*this)["stages"].isDouble();
        auto validParties = (*this)["parties"].isDouble();
        auto validPartyEvents = (*this)["party_events"].isDouble();
        auto validConcurrentPartyEvents = (*this)["concurrent_party_events"].isDouble();
        auto validMaxStageSources = (*this)["max_stage_sources"].isDouble();
        auto validMaxStageSeats = (*this)["max_stage_seats"].isDouble();
        auto validMaxSrtrelayServers = (*this)["max_srtrelay_servers"].isDouble();
        auto validMaxPartyMembers = (*this)["max_party_members"].isDouble();
        auto validMaxPartyEventParticipants = (*this)["max_party_event_participants"].isDouble();

        auto valid = validStages && validParties && validPartyEvents && validConcurrentPartyEvents &&
                     validMaxStageSources && validMaxStageSeats && validMaxSrtrelayServers && validMaxPartyMembers &&
                     validMaxPartyEventParticipants;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR,
            "AccountResourceUsage: stages=%d, parties=%d, party_events=%d, concurrent_party_events=%d, "
            "max_stage_sources=%d, max_stage_seats=%d, max_srtrelay_servers=%d, max_party_members=%d, "
            "max_party_event_participants=%d",
            validStages, validParties, validPartyEvents, validConcurrentPartyEvents, validMaxStageSources,
            validMaxStageSeats, validMaxSrtrelayServers, validMaxPartyMembers, validMaxPartyEventParticipants
        );
#endif

        return valid;
    }
};

class Account : public QJsonObject {
public:
    Account() = default;
    Account(const QJsonObject &_json) : QJsonObject(_json) {}

    inline QString getId() const { return value("_id").toString(); }
    inline void setId(const QString &value) { insert("_id", value); }
    inline QString getDisplayName() const { return value("display_name").toString(); };
    inline void setDisplayName(const QString &value) { insert("display_name", value); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }

    inline bool isValid() const
    {
        auto validId = (*this)["_id"].isString();
        auto validDisplayName = (*this)["display_name"].isString();
        auto validPictureId = maybe((*this)["picture_id"], (*this)["picture_id"].isString());

        auto valid = validId && validDisplayName && validPictureId;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR, "Account: id=%d, display_name=%d, picture_id=%d", validId, validDisplayName,
            validPictureId
        );
#endif

        return valid;
    }
};

class AccountInfo : public QJsonObject {
public:
    AccountInfo() = default;
    AccountInfo(const QJsonObject &_json) : QJsonObject(_json) {}

    inline Account getAccount() const { return value("account").toObject(); }
    inline void setAccount(const Account &value) { insert("account", value); }
    inline SubscriptionLicense getSubscriptionLicense() const { return value("subscription_license").toObject(); }
    inline void setSubscriptionLicense(const SubscriptionLicense &value) { insert("subscription_license", value); }
    inline AccountResourceUsage getResourceUsage() const { return value("resource_usage").toObject(); }
    inline void setResourceUsage(const AccountResourceUsage &value) { insert("resource_usage", value); }

    inline bool isValid() const
    {
        auto validAccount = getAccount().isValid();
        auto validSubscriptionLicense = getSubscriptionLicense().isValid();
        auto validResourceUsage = getResourceUsage().isValid();

        auto valid = validAccount && validSubscriptionLicense && validResourceUsage;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR, "AccountInfo: account=%d, subscription_license=%d, resource_usage=%d",
            validAccount, validSubscriptionLicense, validResourceUsage
        );
#endif

        return valid;
    }
};

class StageSource : public QJsonObject {
public:
    StageSource() = default;
    StageSource(const QJsonObject &_json) : QJsonObject(_json) {}

    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getDisplayName() const { return value("display_name").toString(); }
    inline void setDisplayName(const QString &value) { insert("display_name", value); }
    inline QString getDescription() const { return value("description").toString(); }
    inline void setDescription(const QString &value) { insert("description", value); }

    inline bool isValid() const
    {
        auto validName = (*this)["name"].isString();
        auto validDisplayName = (*this)["display_name"].isString();
        auto validDescription = maybe((*this)["description"], (*this)["description"].isString());

        auto valid = validName && validDisplayName && validDescription;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR, "StageSource: name=%d, display_name=%d, description=%d", validName,
            validDisplayName, validDescription
        );
#endif

        return valid;
    }
};

typedef TypedJsonArray<StageSource> StageSourceArray;

class StageSeat : public QJsonObject {
public:
    StageSeat() = default;
    StageSeat(const QJsonObject &_json) : QJsonObject(_json) {}

    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getDisplayName() const { return value("display_name").toString(); }
    inline void setDisplayName(const QString &value) { insert("display_name", value); }

    inline bool isValid() const
    {
        auto validName = (*this)["name"].isString();
        auto validDisplayName = (*this)["display_name"].isString();

        auto valid = validName && validDisplayName;

#ifdef SCHEMA_DEBUG
        obs_log(valid ? LOG_DEBUG : LOG_ERROR, "StageSeat: name=%d, display_name=%d", validName, validDisplayName);
#endif

        return valid;
    }
};

typedef TypedJsonArray<StageSeat> StageSeatArray;

class SrtrelayServer : public QJsonObject {
public:
    SrtrelayServer() = default;
    SrtrelayServer(const QJsonObject &_json) : QJsonObject(_json) {}

    inline QString getAddress() const { return value("address").toString(); }
    inline void setAddress(const QString &value) { insert("address", value); }
    inline int getPort() const { return value("port").toInt(); }
    inline void setPort(int value) { insert("port", value); }

    inline bool isValid() const
    {
        auto validAddress = (*this)["address"].isString();
        auto validPort = (*this)["port"].isDouble();

        auto valid = validAddress && validPort;

#ifdef SCHEMA_DEBUG
        obs_log(valid ? LOG_DEBUG : LOG_ERROR, "SrtrelayServer: address=%d, port=%d", validAddress, validPort);
#endif

        return valid;
    }
};

typedef TypedJsonArray<SrtrelayServer> SrtrelayServerArray;

class StageSeatView : public QJsonObject {
public:
    StageSeatView() = default;
    StageSeatView(const QJsonObject &_json) : QJsonObject(_json) {}

    inline QString getDisplayName() const { return value("display_name").toString(); }
    inline void setDisplayName(const QString &value) { insert("display_name", value); }

    inline bool isValid() const
    {
        auto validDisplayName = maybe((*this)["display_name"], (*this)["display_name"].isString());

        auto valid = validDisplayName;

#ifdef SCHEMA_DEBUG
        obs_log(valid ? LOG_DEBUG : LOG_ERROR, "StageSeatView: display_name=%d", validDisplayName);
#endif

        return valid;
    }
};

class AccountView : public QJsonObject {
public:
    AccountView() = default;
    AccountView(const QJsonObject &_json) : QJsonObject(_json) {}

    inline QString getDisplayName() const { return value("display_name").toString(); }
    inline void setDisplayName(const QString &value) { insert("display_name", value); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }

    inline bool isValid() const
    {
        auto validDisplayName = (*this)["display_name"].isString();
        auto validPictureId = maybe((*this)["picture_id"], (*this)["picture_id"].isString());

        auto valid = validDisplayName && validPictureId;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR, "AccountView: display_name=%d, picture_id=%d", validDisplayName,
            validPictureId
        );
#endif

        return valid;
    }
};

class Stage : public QJsonObject {
public:
    Stage() = default;
    Stage(const QJsonObject &_json) : QJsonObject(_json) {}

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
    inline SrtrelayServerArray getSrtrelayServers() const { return value("srtrelay_servers").toArray(); }
    inline void setSrtrelayServers(const SrtrelayServerArray &value) { insert("srtrelay_servers", value); }
    inline AccountView getOwnerAccountView() const { return value("owner_account_view").toObject(); }
    inline void setOwnerAccountView(const AccountView &value) { insert("owner_account_view", value); }
    inline QString getOwnerUserId() const { return value("owner_user_id").toString(); }
    inline void setOwnerUserId(const QString &value) { insert("owner_user_id", value); }

    inline bool isValid() const
    {
        auto validId = (*this)["_id"].isString();
        auto validName = (*this)["name"].isString();
        auto validDescription = maybe((*this)["description"], (*this)["description"].isString());
        auto validPictureId = maybe((*this)["picture_id"], (*this)["picture_id"].isString());
        auto validSources =
            maybe((*this)["sources"], (*this)["sources"].isArray() && getSources().every([](const StageSource &value) {
                return value.isValid();
            }));
        auto validSeats =
            maybe((*this)["seats"], (*this)["seats"].isArray() && getSeats().every([](const StageSeat &value) {
                return value.isValid();
            }));
        auto validSrtrelayServers = maybe(
            (*this)["srtrelay_servers"],
            (*this)["srtrelay_servers"].isArray() &&
                getSrtrelayServers().every([](const SrtrelayServer &value) { return value.isValid(); })
        );
        auto validOwnerAccountView = getOwnerAccountView().isValid();
        auto validOwnerUserId = (*this)["owner_user_id"].isString();

        auto valid = validId && validName && validDescription && validPictureId && validSources && validSeats &&
                     validSrtrelayServers && validOwnerAccountView && validOwnerUserId;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR,
            "Stage: _id=%d, name=%d, description=%d, picture_id=%d, sources=%d, seats=%d, srtrelay_servers=%d, "
            "owner_account_view=%d, owner_user_id=%d",
            validId, validName, validDescription, validPictureId, validSources, validSeats, validSrtrelayServers,
            validOwnerAccountView, validOwnerUserId
        );
#endif

        return valid;
    }
};

class StageView : public QJsonObject {
public:
    StageView() = default;
    StageView(const QJsonObject &_json) : QJsonObject(_json) {}

    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }
    inline QString getDescription() const { return value("description").toString(); }
    inline void setDescription(const QString &value) { insert("description", value); }

    inline bool isValid() const
    {
        auto validName = (*this)["name"].isString();
        auto validPictureId = maybe((*this)["picture_id"], (*this)["picture_id"].isString());
        auto validDescription = maybe((*this)["description"], (*this)["description"].isString());

        auto valid = validName && validPictureId && validDescription;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR, "StageView: name=%d, picture_id=%d, description=%d", validName,
            validPictureId, validDescription
        );
#endif

        return valid;
    }
};

typedef TypedJsonArray<Stage> StageArray;

class Party : public QJsonObject {
public:
    Party() = default;
    Party(const QJsonObject &_json) : QJsonObject(_json) {}

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
    inline QString getOwnerUserId() const { return value("owner_user_id").toString(); }
    inline void setOwnerUserId(const QString &value) { insert("owner_user_id", value); }

    inline bool isValid() const
    {
        auto validId = (*this)["_id"].isString();
        auto validName = (*this)["name"].isString();
        auto validDescription = maybe((*this)["description"], (*this)["description"].isString());
        auto validPictureId = maybe((*this)["picture_id"], (*this)["picture_id"].isString());
        auto validCapacity = maybe((*this)["capacity"], (*this)["capacity"].isDouble());
        auto validOwnerAccountView = getOwnerAccountView().isValid();
        auto validOwnerUserId = (*this)["owner_user_id"].isString();

        auto valid = validId && validName && validDescription && validPictureId && validCapacity &&
                     validOwnerAccountView && validOwnerUserId;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR,
            "Party: _id=%d, name=%d, description=%d, picture_id=%d, capacity=%d, owner_account_view=%d, "
            "owner_user_id=%d",
            validId, validName, validDescription, validPictureId, validCapacity, validOwnerAccountView, validOwnerUserId
        );
#endif

        return valid;
    }
};

class PartyView : public QJsonObject {
public:
    PartyView() = default;
    PartyView(const QJsonObject &_json) : QJsonObject(_json) {}

    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }

    inline bool isValid() const
    {
        auto validName = (*this)["name"].isString();
        auto validPictureId = maybe((*this)["picture_id"], (*this)["picture_id"].isString());

        auto valid = validName && validPictureId;

#ifdef SCHEMA_DEBUG
        obs_log(valid ? LOG_DEBUG : LOG_ERROR, "PartyView: name=%d, picture_id=%d", validName, validPictureId);
#endif

        return valid;
    }
};

typedef TypedJsonArray<Party> PartyArray;

class PartyEvent : public QJsonObject {
public:
    PartyEvent() = default;
    PartyEvent(const QJsonObject &_json) : QJsonObject(_json) {}

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
    inline int getCapacity() const { return value("capacity").toInt(); }
    inline void setCapacity(int value) { insert("capacity", value); }
    inline PartyView getPartyView() const { return value("party_view").toObject(); }
    inline void setPartyView(const PartyView &value) { insert("party_view", value); }
    inline StageView getStageView() const { return value("stage_view").toObject(); }
    inline void setStageView(const StageView &value) { insert("stage_view", value); }
    inline AccountView getOwnerAccountView() const { return value("owner_account_view").toObject(); }
    inline void setOwnerAccountView(const AccountView &value) { insert("owner_account_view", value); }
    inline QString getOwnerUserId() const { return value("owner_user_id").toString(); }
    inline void setOwnerUserId(const QString &value) { insert("owner_user_id", value); }

    inline bool isValid() const
    {
        auto validId = (*this)["_id"].isString();
        auto validName = (*this)["name"].isString();
        auto validDescription = maybe((*this)["description"], (*this)["description"].isString());
        auto validStartTime = (*this)["start_time"].isString();
        auto validEndTime = maybe((*this)["end_time"], (*this)["end_time"].isString());
        auto validPictureId = maybe((*this)["picture_id"], (*this)["picture_id"].isString());
        auto validStatus = (*this)["status"].isString();
        auto validStatusChangedAt = (*this)["status_changed_at"].isString();
        auto validCapacity = maybe((*this)["capacity"], (*this)["capacity"].isDouble());
        auto validPartyView = getPartyView().isValid();
        auto validStageView = getStageView().isValid();
        auto validOwnerAccountView = getOwnerAccountView().isValid();
        auto validOwnerUserId = (*this)["owner_user_id"].isString();

        auto valid = validId && validName && validDescription && validStartTime && validEndTime && validPictureId &&
                     validStatus && validStatusChangedAt && validCapacity && validPartyView && validStageView &&
                     validOwnerAccountView && validOwnerUserId;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR,
            "PartyEvent: _id=%d, name=%d, description=%d, start_time=%d, end_time=%d, picture_id=%d, "
            "status=%d, status_changed_at=%d, capacity=%d, party_view=%d, stage_view=%d, "
            "owner_account_view=%d, owner_user_id=%d",
            validId, validName, validDescription, validStartTime, validEndTime, validPictureId, validStatus,
            validStatusChangedAt, validCapacity, validPartyView, validStageView, validOwnerAccountView, validOwnerUserId
        );
#endif

        return valid;
    }
};

class PartyEventView : public QJsonObject {
public:
    PartyEventView() = default;
    PartyEventView(const QJsonObject &_json) : QJsonObject(_json) {}

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
        auto validName = (*this)["name"].isString();
        auto validPictureId = maybe((*this)["picture_id"], (*this)["picture_id"].isString());
        auto validDescription = maybe((*this)["description"], (*this)["description"].isString());
        auto validStatus = (*this)["status"].isString();
        auto validStatusChangedAt = (*this)["status_changed_at"].isString();

        auto valid = validName && validPictureId && validDescription && validStatus && validStatusChangedAt;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR,
            "PartyEventView: name=%d, picture_id=%d, description=%d, status=%d, status_changed_at=%d", validName,
            validPictureId, validDescription, validStatus, validStatusChangedAt
        );
#endif

        return valid;
    }
};

typedef TypedJsonArray<PartyEvent> PartyEventArray;

class PartyEventParticipant : public QJsonObject {
public:
    PartyEventParticipant() = default;
    PartyEventParticipant(const QJsonObject &_json) : QJsonObject(_json) {}

    inline QString getId() const { return value("_id").toString(); }
    inline void setId(const QString &value) { insert("_id", value); }
    inline QString getPartyId() const { return value("party_id").toString(); }
    inline void setPartyId(const QString &value) { insert("party_id", value); }
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
    inline bool getLan() const { return value("lan").toBool(); }
    inline void setLan(bool value) { insert("lan", value); }
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
    inline QString getOwnerUserId() const { return value("owner_user_id").toString(); }
    inline void setOwnerUserId(const QString &value) { insert("owner_user_id", value); }
    inline AccountView getOwnerAccountView() const { return value("owner_account_view").toObject(); }
    inline void setOwnerAccountView(const AccountView &value) { insert("owner_account_view", value); }

    inline bool isValid() const
    {
        auto validId = (*this)["_id"].isString();
        auto validPartyId = (*this)["party_id"].isString();
        auto validPartyEventId = (*this)["party_event_id"].isString();
        auto validStageId = (*this)["stage_id"].isString();
        auto validMemberId = maybe((*this)["member_id"], (*this)["member_id"].isString());
        auto validAccountId = maybe((*this)["account_id"], (*this)["account_id"].isString());
        auto validSeatName = maybe((*this)["seat_name"], (*this)["seat_name"].isString());
        auto validDisabled = maybe((*this)["disabled"], (*this)["disabled"].isBool());
        auto validLan = maybe((*this)["lan"], (*this)["lan"].isBool());
        auto validByol = maybe((*this)["byol"], (*this)["byol"].isBool());
        auto validStageView = getStageView().isValid();
        auto validPartyView = getPartyView().isValid();
        auto validPartyEventView = getPartyEventView().isValid();
        auto validAccountView = maybe((*this)["account_view"], getAccountView().isValid());
        auto validStageSeatView = maybe((*this)["stage_seat_view"], getStageSeatView().isValid());
        auto validOwnerUserId = (*this)["owner_user_id"].isString();
        auto validOwnerAccountView = maybe((*this)["owner_account_view"], getOwnerAccountView().isValid());

        auto valid = validId && validPartyId && validPartyEventId && validStageId && validMemberId && validAccountId &&
                     validSeatName && validDisabled && validLan && validByol && validStageView && validPartyView &&
                     validPartyEventView && validAccountView && validStageSeatView && validOwnerUserId &&
                     validOwnerAccountView;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR,
            "PartyEventParticipant: _d=%d, party_id=%d, party_event_id=%d, stage_id=%d, member_id=%d, "
            "account_id=%d, seat_name=%d, disabled=%d, lan=%d, byol=%d, stage_view=%d, party_view=%d, "
            "party_event_view=%d, account_view=%d, stage_seat_view=%d, owner_user_id=%d, owner_account_view=%d",
            validId, validPartyId, validPartyEventId, validStageId, validMemberId, validAccountId, validSeatName,
            validDisabled, validLan, validByol, validStageView, validPartyView, validPartyEventView, validAccountView,
            validStageSeatView, validOwnerUserId, validOwnerAccountView
        );
#endif

        return valid;
    }
};

typedef TypedJsonArray<PartyEventParticipant> PartyEventParticipantArray;

class ConnectionAdvices : public QJsonObject {
public:
    ConnectionAdvices() = default;
    ConnectionAdvices(const QJsonObject &_json) : QJsonObject(_json) {}

    inline bool getUnreachable() const { return value("unreachable").toBool(); }
    inline void setUnreachable(bool value) { insert("unreachable", value); }

    inline bool isValid() const
    {
        auto validUnreachable = maybe((*this)["unreachable"], (*this)["unreachable"].isBool());

        auto valid = validUnreachable;

#ifdef SCHEMA_DEBUG
        obs_log(valid ? LOG_DEBUG : LOG_ERROR, "ConnectionAdvices: unreachable=%d", validUnreachable);
#endif

        return valid;
    }
};

class StageConnection : public QJsonObject {
public:
    StageConnection() = default;
    StageConnection(const QJsonObject &_json) : QJsonObject(_json) {}

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
    inline ConnectionAdvices getConnectionAdvices() const { return value("connection_advices").toObject(); }
    inline void setConnectionAdvices(const ConnectionAdvices &value) { insert("connection_advices", value); }
    inline QString getLanServer() const { return value("lan_server").toString(); }
    inline void setLanServer(const QString &value) { insert("lan_server", value); }
    inline int getLatency() const { return value("latency").toInt(); }
    inline void setLatency(int value) { insert("latency", value); }
    inline QString getOwnerUserId() const { return value("owner_user_id").toString(); }
    inline void setOwnerUserId(const QString &value) { insert("owner_user_id", value); }

    inline bool isValid() const
    {
        auto validId = (*this)["_id"].isString();
        auto validStageId = (*this)["stage_id"].isString();
        auto validSeatName = (*this)["seat_name"].isString();
        auto validSourceName = (*this)["source_name"].isString();
        auto validProtocol = (*this)["protocol"].isString();
        auto validServer = (*this)["server"].isString();
        auto validPort = (*this)["port"].isDouble();
        auto validStreamId = (*this)["stream_id"].isString();
        auto validPassphrase = (*this)["passphrase"].isString();
        auto validParameters = (*this)["parameters"].isString();
        auto validRelay = (*this)["relay"].isBool();
        auto validMaxBitrate = (*this)["max_bitrate"].isDouble();
        auto validMinBitrate = (*this)["min_bitrate"].isDouble();
        auto validWidth = (*this)["width"].isDouble();
        auto validHeight = (*this)["height"].isDouble();
        auto validRevision = (*this)["revision"].isDouble();
        auto validDisabled = maybe((*this)["disabled"], (*this)["disabled"].isBool());
        auto validAllocationId = maybe((*this)["allocation_id"], (*this)["allocation_id"].isString());
        auto vlaidConnectionAdvices = maybe((*this)["connection_advices"], getConnectionAdvices().isValid());
        auto validLanServer = maybe((*this)["lan_server"], (*this)["lan_server"].isString());
        auto validLatency = maybe((*this)["latency"], (*this)["latency"].isDouble());
        auto validOwnerUserId = (*this)["owner_user_id"].isString();

        auto valid = validId && validStageId && validSeatName && validSourceName && validProtocol && validServer &&
                     validPort && validStreamId && validPassphrase && validParameters && validRelay &&
                     validMaxBitrate && validMinBitrate && validWidth && validHeight && validRevision &&
                     validDisabled && validAllocationId && vlaidConnectionAdvices && validLanServer && validLatency &&
                     validOwnerUserId;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR,
            "StageConnection: id=%d, stage_id=%d, seat_name=%d, source_name=%d, protocol=%d, "
            "server=%d, port=%d, stream_id=%d, passphrase=%d, parameters=%d, relay=%d, "
            "max_bitrate=%d, min_bitrate=%d, width=%d, height=%d, revision=%d, disabled=%d, "
            "allocation_id=%d, connection_advices=%d, lan_server=%d, latency=%d, owner_user_id=%d",
            validId, validStageId, validSeatName, validSourceName, validProtocol, validServer, validPort, validStreamId,
            validPassphrase, validParameters, validRelay, validMaxBitrate, validMinBitrate, validWidth, validHeight,
            validRevision, validDisabled, validAllocationId, vlaidConnectionAdvices, validLanServer, validLatency,
            validOwnerUserId
        );
#endif

        return valid;
    }
};

typedef TypedJsonArray<StageConnection> StageConnectionArray;

class StageSeatAllocation : public QJsonObject {
public:
    StageSeatAllocation() = default;
    StageSeatAllocation(const QJsonObject &_json) : QJsonObject(_json) {}

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
    inline bool getLan() const { return value("lan").toBool(); }
    inline void setLan(bool value) { insert("lan", value); }
    inline int getLatency() const { return value("latency").toInt(); }
    inline void setLatency(int value) { insert("latency", value); }
    inline QString getOwnerUserId() const { return value("owner_user_id").toString(); }
    inline void setOwnerUserId(const QString &value) { insert("owner_user_id", value); }

    inline bool isValid() const
    {
        auto validId = (*this)["_id"].isString();
        auto validPartyId = (*this)["party_id"].isString();
        auto validPartyEventId = (*this)["party_event_id"].isString();
        auto validStageId = (*this)["stage_id"].isString();
        auto validSeatName = (*this)["seat_name"].isString();
        auto validMemberId = (*this)["member_id"].isString();
        auto validParticipantId = (*this)["participant_id"].isString();
        auto validAccountId = (*this)["account_id"].isString();
        auto validDisabled = maybe((*this)["disabled"], (*this)["disabled"].isBool());
        auto validLan = maybe((*this)["lan"], (*this)["lan"].isBool());
        auto validLatency = maybe((*this)["latency"], (*this)["latency"].isDouble());
        auto validOwnerUserId = (*this)["owner_user_id"].isString();

        auto valid = validId && validPartyId && validPartyEventId && validStageId && validSeatName && validMemberId &&
                     validParticipantId && validAccountId && validDisabled && validLan && validLatency &&
                     validOwnerUserId;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR,
            "StageSeatAllocation: id=%d, party_id=%d, party_event_id=%d, stage_id=%d, seat_name=%d, "
            "member_id=%d, participant_id=%d, account_id=%d, disabled=%d, lan=%d, latency=%d, owner_user_id=%d",
            validId, validPartyId, validPartyEventId, validStageId, validSeatName, validMemberId, validParticipantId,
            validAccountId, validDisabled, validLan, validLatency, validOwnerUserId
        );
#endif

        return valid;
    }
};

class UplinkInfo : public QJsonObject {
public:
    UplinkInfo() = default;
    UplinkInfo(const QJsonObject &_json) : QJsonObject(_json) {}

    inline StageSeatAllocation getAllocation() const { return value("allocation").toObject(); }
    inline void setAllocation(const StageSeatAllocation &value) { insert("allocation", value); }
    inline Stage getStage() const { return value("stage").toObject(); }
    inline void setStage(const Stage &value) { insert("stage", value); }
    inline StageConnectionArray getConnections() const { return value("connections").toArray(); }
    inline void setConnections(const StageConnectionArray &value) { insert("connections", value); }
    inline QString getPublicAddress() const { return value("public_address").toString(); }
    inline void setPublicAddress(const QString &value) { insert("public_address", value); }

    inline bool isValid() const
    {
        auto validAllocation = maybe((*this)["allocation"], getAllocation().isValid());
        auto validStage = maybe((*this)["stage"], getStage().isValid());
        auto validConnections = (*this)["connections"].isArray() &&
                                getConnections().every([](const StageConnection &value) { return value.isValid(); });
        auto validPublicAddress = (*this)["public_address"].isString();

        auto valid = validAllocation && validStage && validConnections && validPublicAddress;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR, "UplinkInfo: allocation=%d, stage=%d, connections=%d, public_address=%d",
            validAllocation, validStage, validConnections, validPublicAddress
        );
#endif

        return valid;
    }
};

class DownlinkInfo : public QJsonObject {
public:
    DownlinkInfo() = default;
    DownlinkInfo(const QJsonObject &_json) : QJsonObject(_json) {}

    inline StageConnection getConnection() const { return value("connection").toObject(); }
    inline void setConnection(const StageConnection &value) { insert("connection", value); }

    inline bool isValid() const
    {
        auto validConnections = getConnection().isValid();

        auto valid = validConnections;

#ifdef SCHEMA_DEBUG
        obs_log(valid ? LOG_DEBUG : LOG_ERROR, "DownlinkInfo: connection=%d", validConnections);
#endif

        return valid;
    }
};

class WebSocketMessage : public QJsonObject {
public:
    WebSocketMessage() = default;
    WebSocketMessage(const QJsonObject &_json) : QJsonObject(_json) {}

    inline QString getEvent() const { return value("event").toString(); }
    inline void setEvent(const QString &value) { insert("event", value); }
    inline QString getReason() const { return value("reason").toString(); }
    inline void setReason(const QString &value) { insert("reason", value); }
    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getId() const { return value("id").toString(); }
    inline void setId(const QString &value) { insert("id", value); }
    inline QJsonObject getPayload() const { return value("payload").toObject(); }
    inline void setPayload(const QJsonObject &value) { insert("payload", value); }
    inline bool getContinuous() const { return value("continuous").toBool(); }
    inline void setContinuous(bool value) { insert("continuous", value); }

    inline bool isValid() const
    {
        auto validEvent = (*this)["event"].isString();
        auto validReason = maybe((*this)["reason"], (*this)["reason"].isString());
        auto validName = maybe((*this)["name"], (*this)["name"].isString());
        auto validId = maybe((*this)["id"], (*this)["id"].isString());
        auto validPayload = maybe((*this)["payload"], (*this)["payload"].isObject());
        auto validContinuous = maybe((*this)["continuous"], (*this)["continuous"].isBool());

        auto valid = validEvent && validReason && validName && validId && validPayload && validContinuous;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR,
            "WebSocketMessage: event=%d, reason=%d, name=%d, id=%d, payload=%d, continuous=%d", validEvent, validReason,
            validName, validId, validPayload, validContinuous
        );
#endif

        return valid;
    }
};

class DownlinkRequestBody : public QJsonObject {
public:
    DownlinkRequestBody() = default;
    DownlinkRequestBody(const QJsonObject &_json) : QJsonObject(_json) {}

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
    inline QString getLanServer() const { return value("lan_server").toString(); }
    inline void setLanServer(const QString &value) { insert("lan_server", value); }

    inline bool isValid() const
    {
        auto validStageId = (*this)["stage_id"].isString();
        auto validSeatName = (*this)["seat_name"].isString();
        auto validSourceName = (*this)["source_name"].isString();
        auto validProtocol = (*this)["protocol"].isString();
        auto validPort = (*this)["port"].isDouble();
        auto validStreamId = (*this)["stream_id"].isString();
        auto validPassphrase = (*this)["passphrase"].isString();
        auto validParameters = (*this)["parameters"].isString();
        auto validRelay = (*this)["relay"].isBool();
        auto validMaxBitrate = (*this)["max_bitrate"].isDouble();
        auto validMinBitrate = (*this)["min_bitrate"].isDouble();
        auto validWidth = (*this)["width"].isDouble();
        auto validHeight = (*this)["height"].isDouble();
        auto validRevision = (*this)["revision"].isDouble();
        auto validLanServer = (*this)["lan_server"].isString();

        auto valid = validStageId && validSeatName && validSourceName && validProtocol && validPort && validStreamId &&
                     validPassphrase && validParameters && validRelay && validMaxBitrate && validMinBitrate &&
                     validWidth && validHeight && validRevision && validLanServer;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR,
            "DownlinkRequestBody: stage_id=%d, seat_name=%d, source_name=%d, protocol=%d, port=%d, stream_id=%d, "
            "passphrase=%d, parameters=%d, relay=%d, max_bitrate=%d, min_bitrate=%d, width=%d, height=%d, revision=%d"
            "lan_server=%d",
            validStageId, validSeatName, validSourceName, validProtocol, validPort, validStreamId, validPassphrase,
            validParameters, validRelay, validMaxBitrate, validMinBitrate, validWidth, validHeight, validRevision,
            validLanServer
        );
#endif

        return valid;
    }
};

class WsPortalFacilityView : public QJsonObject {
public:
    WsPortalFacilityView() = default;
    WsPortalFacilityView(const QJsonObject &_json) : QJsonObject(_json) {}

    inline QString getAddress() const { return value("address").toString(); }
    inline void setAddress(const QString &value) { insert("address", value); }
    inline int getPort() const { return value("port").toInt(); }
    inline void setPort(int value) { insert("port", value); }
    inline int getTlsPort() const { return value("tls_port").toInt(); }
    inline void setTlsPort(int value) { insert("tls_port", value); }

    inline QString getHost() const
    {
        // The host always has "api" subdomain added to the address
        return QString("api.%1").arg(getAddress());
    }

    inline QString getHostAndPort() const
    {
        return QString("%1:%2").arg(getHost()).arg(getTlsPort() ? getTlsPort() : getPort());
    }

    inline QString getUrl() const { return QString("%1://%2").arg(getTlsPort() ? "wss" : "ws").arg(getHostAndPort()); }

    inline bool isValid() const
    {
        auto validAddress = (*this)["address"].isString();
        auto validPort = (*this)["port"].isDouble();
        auto validTlsPort = maybe((*this)["tls_port"], (*this)["tls_port"].isDouble());

        auto valid = validAddress && validPort && validTlsPort;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR, "WsPortalFacilityView: address=%d, port=%d, tls_port=%d", validAddress,
            validPort, validTlsPort
        );
#endif

        return valid;
    }
};

class WsPortal : public QJsonObject {
public:
    WsPortal() = default;
    WsPortal(const QJsonObject &_json) : QJsonObject(_json) {}

    inline QString getId() const { return value("_id").toString(); }
    inline void setId(const QString &value) { insert("_id", value); }
    inline QString getName() const { return value("name").toString(); }
    inline void setName(const QString &value) { insert("name", value); }
    inline QString getDescription() const { return value("description").toString(); }
    inline void setDescription(const QString &value) { insert("description", value); }
    inline QString getPictureId() const { return value("picture_id").toString(); }
    inline void setPictureId(const QString &value) { insert("picture_id", value); }
    inline AccountView getOwnerAccountView() const { return value("owner_account_view").toObject(); }
    inline void setOwnerAccountView(const AccountView &value) { insert("owner_account_view", value); }
    inline QString getOwnerUserId() const { return value("owner_user_id").toString(); }
    inline void setOwnerUserId(const QString &value) { insert("owner_user_id", value); }
    inline int getEventSubscriptions() const { return value("event_subscriptions").toInt(); }
    inline void setEventSubscriptions(int value) { insert("event_subscriptions", value); }
    inline QString getFacilityId() const { return value("facility_id").toString(); }
    inline void setFacilityId(const QString &value) { insert("facility_id", value); }
    inline WsPortalFacilityView getFacilityView() const { return value("facility_view").toObject(); }
    inline void setFacilityView(const WsPortalFacilityView &value) { insert("facility_view", value); }

    inline bool isValid() const
    {
        auto validId = (*this)["_id"].isString();
        auto validName = (*this)["name"].isString();
        auto validDescription = maybe((*this)["description"], (*this)["description"].isString());
        auto validPictureId = maybe((*this)["picture_id"], (*this)["picture_id"].isString());
        auto validEventSubscriptions = maybe((*this)["event_subscriptions"], (*this)["event_subscriptions"].isDouble());

        auto validOwnerAccountView = getOwnerAccountView().isValid();
        auto validOwnerUserId = (*this)["owner_user_id"].isString();
        auto validFacilityId = (*this)["facility_id"].isString();
        auto validFacilityView = maybe((*this)["facility_view"], getFacilityView().isValid());

        auto valid = validId && validName && validDescription && validPictureId && validEventSubscriptions &&
                     validOwnerAccountView && validOwnerUserId && validFacilityId && validFacilityView;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR,
            "Party: _id=%d, name=%d, description=%d, picture_id=%d, event_subscriptions=%d owner_account_view=%d, "
            "owner_user_id=%d, facility_id=%d, facility_view=%d",
            validId, validName, validDescription, validPictureId, validEventSubscriptions, validOwnerAccountView,
            validOwnerUserId, validFacilityId, validFacilityView
        );
#endif

        return valid;
    }
};

typedef TypedJsonArray<WsPortal> WsPortalArray;

class WsPortalMessage : public WebSocketMessage {
public:
    WsPortalMessage() = default;
    WsPortalMessage(const QJsonObject &_json) : WebSocketMessage(_json) {}

    inline QString getConnectionId() const { return value("connection_id").toString(); }
    inline void setConnectionId(const QString &value) { insert("connection_id", value); }
    inline QString getWsPortalId() const { return value("ws_portal_id").toString(); }
    inline void setWsPortalId(const QString &value) { insert("ws_portal_id", value); }

    inline bool isValid() const
    {
        auto validMessage = WebSocketMessage::isValid();
        auto validConnectionId = (*this)["connection_id"].isString();
        auto validWsPortalId = (*this)["ws_portal_id"].isString();

        auto valid = validMessage && validConnectionId && validWsPortalId;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR, "WsPortalMessage: message=%d, connection_id=%d, ws_portal_id=%d",
            validMessage, validConnectionId, validWsPortalId
        );
#endif

        return valid;
    }
};

class OutputMetric : public QJsonObject {
public:
    OutputMetric() = default;
    OutputMetric(const QJsonObject &_json) : QJsonObject(_json) {}

    inline double getBitrate() const { return value("bitrate").toDouble(); }
    inline void setBitrate(double value) { insert("bitrate", value); }
    inline int getTotalFrames() const { return value("total_frames").toInt(); }
    inline void setTotalFrames(int value) { insert("total_frames", value); }
    inline int getDroppedFrames() const { return value("dropped_frames").toInt(); }
    inline void setDroppedFrames(int value) { insert("dropped_frames", value); }
    inline int getTotalSize() const { return value("total_size").toInt(); }
    inline void setTotalSize(int value) { insert("total_size", value); }

    inline bool isValid() const
    {
        auto validBitrate = (*this)["bitrate"].isDouble();
        auto validTotalFrames = (*this)["total_frames"].isDouble();
        auto validDroppedFrames = (*this)["dropped_frames"].isDouble();
        auto validTotalSize = (*this)["total_size"].isDouble();

        auto valid = validBitrate && validTotalFrames && validDroppedFrames && validTotalSize;

#ifdef SCHEMA_DEBUG
        obs_log(
            valid ? LOG_DEBUG : LOG_ERROR, "OutputMetric: bitrate=%d, total_frames=%d, dropped_frames=%d, total_size=%d",
            validBitrate, validTotalFrames, validDroppedFrames, validTotalSize
        );
#endif

        return valid;
    }
};