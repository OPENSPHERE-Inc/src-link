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
#include <util/deque.h>

#include <QObject>
#include <QThread>
#include <QMutex>

#include "../api-client.hpp"
#include "audio-capture.hpp"

#define MAX_AUDIO_BUFFER_FRAMES 131071


class LinkedSource : public QObject {
    Q_OBJECT

    friend class LinkedSourceAudioThread;

    QString uuid;
    int port;
    QString stageId;
    QString seatName;
    QString sourceName;
    QString protocol;
    QString passphrase;
    QString streamId;
    QString localParameters;
    QString remoteParameters;
    int maxBitrate;
    int minBitrate;
    int width;
    int height;
    int reconnectDelaySec;
    int bufferingMb;
    bool hwDecode;
    bool clearOnMediaEnd;

    SourceLinkApiClient *apiClient;
    obs_source_t *source;  // Don't increse reference because couldn't finalize by OBS
    OBSSourceAutoRelease decoderSource;
    speaker_layout speakers;
    uint32_t samplesPerSec;
    bool connected;
    LinkedSourceAudioThread *audioThread;

    void captureSettings(obs_data_t *settings);
    // Return value must be release via obs_data_release()
    obs_data_t *createDecoderSettings();
    // Unregister connection if no stage/seat/source selected.
    void handleConnection();

private slots:
    void onConnectionPutSucceeded(const StageConnection &connection);
    void onConnectionPutFailed();
    void onConnectionDeleteSucceeded(const QString &uuid);

public:
    explicit LinkedSource(obs_data_t *settings, obs_source_t *source, SourceLinkApiClient *_apiClient, QObject *parent = nullptr);
    ~LinkedSource();

    obs_properties_t *getProperties();
    void getDefault(obs_data_t *settings);
    // Returns source's actual width
    uint32_t getWidth();
    // Returns source's actual height
    uint32_t getHeight();
    void update(obs_data_t *settings);

    void videoRenderCallback();
};


class LinkedSourceAudioThread : public QThread, SourceAudioCapture {
    Q_OBJECT

    LinkedSource *linkedSource;

public:
    explicit LinkedSourceAudioThread(LinkedSource* _linkedSource);
    ~LinkedSourceAudioThread();

    void run() override;
};
