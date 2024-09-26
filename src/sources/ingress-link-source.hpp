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

class IngressLinkSource : public QObject {
    Q_OBJECT

    friend class SourceAudioThread;

    QString uuid;
    QString name;
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
    int revision; // Connection revision

    SourceLinkApiClient *apiClient;
    OBSWeakSourceAutoRelease weakSource; // Don't grab strong reference because cannot finalize by OBS
    OBSSourceAutoRelease decoderSource;
    speaker_layout speakers;
    uint32_t samplesPerSec;
    bool connected;
    SourceAudioThread *audioThread;
    QTimer *intervalTimer;
    OBSSignal renameSignal;

    void captureSettings(obs_data_t *settings);
    // Return value must be release via obs_data_release()
    obs_data_t *createDecoderSettings();
    // Unregister connection if no stage/seat/source selected.
    void handleConnection();
    QString compositeParameters(obs_data_t* settings, bool remote = false);

private slots:
    void onConnectionPutSucceeded(const StageConnection &connection);
    void onConnectionPutFailed();
    void onConnectionDeleteSucceeded(const QString &uuid);
    void onStageConnectionReady(const StageConnection &connection);

public:
    explicit IngressLinkSource(
        obs_data_t *settings, obs_source_t *source, SourceLinkApiClient *_apiClient, QObject *parent = nullptr
    );
    ~IngressLinkSource();

    obs_properties_t *getProperties();
    inline uint32_t getWidth() { return width; }
    inline uint32_t getHeight() { return height; }
    void update(obs_data_t *settings);
    void reconnect();

    void videoRenderCallback();

    static void getDefaults(obs_data_t *settings, SourceLinkApiClient *apiClient);
};

class SourceAudioThread : public QThread, SourceAudioCapture {
    Q_OBJECT

    IngressLinkSource *ingressLinkSource;

public:
    explicit SourceAudioThread(IngressLinkSource *_linkedSource);
    ~SourceAudioThread();

    void run() override;
};
