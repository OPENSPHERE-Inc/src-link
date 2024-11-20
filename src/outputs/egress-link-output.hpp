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

#include <obs-module.h>
#include <obs.hpp>
#include <util/deque.h>
#include <obs-frontend-api.h>

#include <QObject>
#include <QMutex>

#include "../api-client.hpp"
#include "../schema.hpp"
#include "../utils.hpp"

#define PROGRAM_OUT_SOURCE QString()
#define INTERLOCK_TYPE_NONE QString()

class OutputAudioSource;

enum EgressLinkOutputStatus {
    EGRESS_LINK_OUTPUT_STATUS_INACTIVE,
    EGRESS_LINK_OUTPUT_STATUS_STAND_BY,
    EGRESS_LINK_OUTPUT_STATUS_ACTIVE,
    EGRESS_LINK_OUTPUT_STATUS_ERROR,
    EGRESS_LINK_OUTPUT_STATUS_DISABLED
};

class EgressLinkOutput : public QObject {
    Q_OBJECT

    QString name;

    SRCLinkApiClient *apiClient;
    StageConnection connection;
    OBSDataAutoRelease settings;
    OBSServiceAutoRelease service;
    OBSOutputAutoRelease output;
    OBSEncoderAutoRelease videoEncoder;
    OBSEncoderAutoRelease audioEncoder;
    OBSSourceAutoRelease source; // NULL if main output is used.
    OBSView sourceView;
    video_t *sourceVideo;
    OBSAudio audioSilence;
    OutputAudioSource *audioSource;
    QMutex outputMutex;

    EgressLinkOutputStatus status;
    QString activeSourceUuid;
    int storedSettingsRev;
    int activeSettingsRev;
    uint64_t connectionAttemptingAt;
    QTimer *snapshotTimer;
    QTimer *monitoringTimer;
    int width;
    int height;

    void loadSettings();
    void saveSettings();
    obs_data_t *createEgressSettings(const StageConnection &connection);
    void setStatus(EgressLinkOutputStatus value);
    void releaseResources(bool stopStatus = false);

    static void onOBSFrontendEvent(enum obs_frontend_event event, void *paramd);

signals:
    void statusChanged(EgressLinkOutputStatus status);

private slots:
    void onSnapshotTimerTimeout();
    void onMonitoringTimerTimeout();
    void onUplinkReady(const UplinkInfo &uplink);

public:
    explicit EgressLinkOutput(const QString &_name, SRCLinkApiClient *_apiClient);
    ~EgressLinkOutput();

    obs_properties_t *getProperties();
    void getDefaults(obs_data_t *defaults);
    void update(obs_data_t *newSettings);
    void start();
    void stop();
    void setSourceUuid(const QString &value = PROGRAM_OUT_SOURCE);
    void setVisible(bool value);
    void refresh();

    inline const QString &getName() const { return name; }
    inline void setName(const QString &value) { name = value; }
    inline obs_data_t *getSettings() const { return settings; }
    inline const QString getSourceUuid() const { return obs_data_get_string(settings, "source_uuid"); }
    inline bool getStatus() const { return status; }
    inline bool getVisible() const { return obs_data_get_bool(settings, "visible"); }
};
