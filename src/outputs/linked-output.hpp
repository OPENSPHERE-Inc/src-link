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

class LinkedOutput : public QObject {
    Q_OBJECT

    QString name;
    QString sourceUuid; // Empty means capture program out.

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

    bool outputActive;
    uint64_t connectionAttemptingAt;
    QTimer *pollingTimer;
    QTimer *monitoringTimer;
    QString sourceName;
    int width;
    int height;

    void loadSettings();
    void saveSettings();
    obs_data_t *createEgressSettings(const StageConnection &connection);

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
    inline const QString getSourceUuid() const { return sourceUuid; }
    inline void setSourceUuid(const QString &value = PROGRAM_OUT_SOURCE) { sourceUuid = value; }

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