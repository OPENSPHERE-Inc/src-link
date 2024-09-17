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

#include <obs-module.h>
#include <obs.hpp>
#include <util/deque.h>
#include <obs-frontend-api.h>

#include <QObject>
#include <QMutex>

#include "../api-client.hpp"
#include "../schema.hpp"
#include "../sources/audio-capture.hpp"
#include "../utils.hpp"

#define PROGRAM_OUT_SOURCE QString()

class OutputAudioSource;

enum EgressLinkOutputStatus {
    LINKED_OUTPUT_STATUS_INACTIVE,
    LINKED_OUTPUT_STATUS_STAND_BY,
    LINKED_OUTPUT_STATUS_ACTIVE,
    LINKED_OUTPUT_STATUS_ERROR,
    LINKED_OUTPUT_STATUS_DISABLED
};

class EgressLinkOutput : public QObject {
    Q_OBJECT

    QString name;

    SourceLinkApiClient *apiClient;
    StageConnection connection;
    OBSDataAutoRelease settings;
    OBSServiceAutoRelease service;
    OBSOutputAutoRelease output;
    OBSEncoderAutoRelease videoEncoder;
    OBSEncoderAutoRelease audioEncoder;
    OBSSourceAutoRelease source;  // NULL if main output is used.
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
    QTimer *pollingTimer;
    QTimer *monitoringTimer;
    int width;
    int height;

    void loadSettings();
    void saveSettings();
    obs_data_t *createEgressSettings(const StageConnection &connection);
    void setStatus(EgressLinkOutputStatus value);
    void releaseResources(bool stopStatus = false);

signals:
    void statusChanged(EgressLinkOutputStatus status);

private slots:
    void onPollingTimerTimeout();
    void onMonitoringTimerTimeout();
    void onSeatAllocationReady(const StageSeatInfo &seat);

public:
    explicit EgressLinkOutput(const QString &_name, SourceLinkApiClient *_apiClient);
    ~EgressLinkOutput();

    obs_properties_t *getProperties();
    void getDefault(obs_data_t *defaults);
    void update(obs_data_t *newSettings);
    void start();
    void stop();
    void setSourceUuid(const QString &value = PROGRAM_OUT_SOURCE);
    void setVisible(bool value);

    inline const QString &getName() const { return name; }
    inline void setName(const QString &value) { name = value; }
    inline obs_data_t *getSettings() const { return settings; }
    inline const QString getSourceUuid() const { return obs_data_get_string(settings, "source_uuid"); }
    inline bool getStatus() const { return status; }
    inline bool getVisible() const { return obs_data_get_bool(settings, "visible"); }
};

class OutputAudioSource : public SourceAudioCapture {
    Q_OBJECT

    audio_t *audio;

    static bool onOutputAudio(
        void *param, uint64_t startTsIn, uint64_t, uint64_t *outTs, uint32_t mixers, audio_output_data *audioData
    );

public:
    explicit OutputAudioSource(
        obs_source_t *source, uint32_t _samplesPerSec, speaker_layout _speakers, QObject *parent = nullptr
    );
    ~OutputAudioSource();

    inline audio_t *getAudio() { return audio; }

    uint64_t popAudio(uint64_t startTsIn, uint32_t mixers, audio_output_data *audioData);
};

