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

#define DEFAULT_INTERLOCK_TYPE "virtual_cam"

#define PROGRAM_OUT_SOURCE QString()
#define INTERLOCK_TYPE_NONE QString()

class OutputAudioSource;

enum EgressLinkOutputStatus {
    EGRESS_LINK_OUTPUT_STATUS_INACTIVE,
    EGRESS_LINK_OUTPUT_STATUS_STAND_BY,
    EGRESS_LINK_OUTPUT_STATUS_ACTIVATING,
    EGRESS_LINK_OUTPUT_STATUS_ACTIVE,
    EGRESS_LINK_OUTPUT_STATUS_ERROR,
    EGRESS_LINK_OUTPUT_STATUS_DISABLED,
};

enum RecordingOutputStatus {
    RECORDING_OUTPUT_STATUS_INACTIVE,
    RECORDING_OUTPUT_STATUS_ACTIVATING,
    RECORDING_OUTPUT_STATUS_ACTIVE,
    RECORDING_OUTPUT_STATUS_ERROR,
    RECORDING_OUTPUT_STATUS_DISABLED
};

class EgressLinkOutput : public QObject {
    Q_OBJECT

    QString name;

    SRCLinkApiClient *apiClient;
    StageConnection connection;
    OBSDataAutoRelease settings;
    OBSServiceAutoRelease service;
    OBSOutputAutoRelease streamingOutput;
    OBSOutputAutoRelease recordingOutput;
    OBSEncoderAutoRelease videoEncoder;
    OBSEncoderAutoRelease audioEncoder;
    OBSSourceAutoRelease source; // NULL if main output is used.
    OBSView sourceView;
    OBSAudio audioSilence;
    OutputAudioSource *audioSource;
    QMutex outputMutex;

    EgressLinkOutputStatus status;
    RecordingOutputStatus recordingStatus;
    QString activeSourceUuid;
    int storedSettingsRev;
    int activeSettingsRev;
    uint64_t connectionAttemptingAt; // milliseconds
    QTimer *snapshotTimer;
    QTimer *monitoringTimer;
    int width;
    int height;

    void loadSettings();
    void saveSettings();
    obs_data_t *createEgressSettings(const StageConnection &connection);
    obs_data_t *createRecordingSettings(obs_data_t *egressSettings);
    void setStatus(EgressLinkOutputStatus value);
    void setRecordingStatus(RecordingOutputStatus value);
    void startStreaming();
    void restartStreaming();
    void startRecording();
    void restartRecording();
    void retrieveConnection();
    bool createSource(QString sourceUuid);
    video_t *createVideo(obs_video_info *vi);
    audio_t *createAudio(QString audioSourceUuid);
    bool createStreamingOutput(obs_data_t *egressSettings);
    bool createRecordingOutput(obs_data_t *egressSettings);
    bool createVideoEncoder(obs_data_t *egressSettings, video_t *video, int width, int height);
    bool createAudioEncoder(obs_data_t *egressSettings, QString audioSourceUuid, audio_t *audio);
    void destroyPipeline();

    static void onOBSFrontendEvent(enum obs_frontend_event event, void *paramd);

signals:
    void statusChanged(EgressLinkOutputStatus status);
    void recordingStatusChanged(RecordingOutputStatus status);

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
    inline EgressLinkOutputStatus getStatus() const { return status; }
    inline RecordingOutputStatus getRecordingStatus() const { return recordingStatus; }
    inline bool getVisible() const { return obs_data_get_bool(settings, "visible"); }
};
