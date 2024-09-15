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

#define PROGRAM_OUT_SOURCE QString()

class LinkedOutputAudioSource;

enum LinkedOutputStatus {
    LINKED_OUTPUT_STATUS_INACTIVE,
    LINKED_OUTPUT_STATUS_STAND_BY,
    LINKED_OUTPUT_STATUS_ACTIVE,
    LINKED_OUTPUT_STATUS_ERROR,
    LINKED_OUTPUT_STATUS_DISABLED
};

class LinkedOutput : public QObject {
    Q_OBJECT

    QString name;

    SourceLinkApiClient *apiClient;
    OBSDataAutoRelease settings;
    OBSServiceAutoRelease service;
    OBSOutputAutoRelease output;
    OBSEncoderAutoRelease videoEncoder;
    OBSEncoderAutoRelease audioEncoder;
    OBSView sourceView;
    video_t *sourceVideo;
    OBSWeakSourceAutoRelease weakSource;
    LinkedOutputAudioSource *audioSource;

    LinkedOutputStatus status;
    uint64_t connectionAttemptingAt;
    QTimer *pollingTimer;
    QTimer *monitoringTimer;
    QString sourceName;
    int width;
    int height;

    void loadSettings();
    void saveSettings();
    obs_data_t *createEgressSettings(const StageConnection &connection);

    inline void setStatus(LinkedOutputStatus value)
    {
        if (status != value) {
            status = value;
            emit statusChanged(status);
        }
    }

signals:
    void statusChanged(LinkedOutputStatus status);

public:
    explicit LinkedOutput(const QString &_name, SourceLinkApiClient *_apiClient, QObject *parent = nullptr);
    ~LinkedOutput();

    obs_properties_t *getProperties();
    void getDefault(obs_data_t *defaults);
    void update(obs_data_t *newSettings);
    void startOutput();
    void stopOutput();

    inline const QString &getName() const { return name; }
    inline void setName(const QString &value) { name = value; }
    inline obs_data_t *getSettings() const { return settings; }
    inline const QString getSourceUuid() const { return obs_data_get_string(settings, "source_uuid"); }
    inline void setSourceUuid(const QString &value = PROGRAM_OUT_SOURCE)
    {
        obs_data_set_string(settings, "source_uuid", qPrintable(value));
        saveSettings();
    }
    inline bool getStatus() const { return status; }

private slots:
    void onPollingTimerTimeout();
    void onMonitoringTimerTimeout();
};

class LinkedOutputAudioSource : public SourceAudioCapture {
    Q_OBJECT

    audio_t *audio;

public:
    explicit LinkedOutputAudioSource(
        obs_source_t *source, uint32_t _samplesPerSec, speaker_layout _speakers, QObject *parent = nullptr
    );
    ~LinkedOutputAudioSource();

    inline audio_t *getAudio() { return audio; }

    uint64_t popAudio(uint64_t startTsIn, uint32_t mixers, audio_output_data *audioData);

private:
    static bool onOutputAudio(
        void *param, uint64_t startTsIn, uint64_t, uint64_t *outTs, uint32_t mixers, audio_output_data *audioData
    );
};