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
#include <util/deque.h>

#include <QObject>
#include <QThread>
#include <QMutex>

#include "../api-client.hpp"
#include "audio-capture.hpp"
#include "image-renderer.hpp"

#define MAX_AUDIO_BUFFER_FRAMES 131071

class IngressLinkSource : public QObject {
    Q_OBJECT

    friend class SourceAudioThread;

    QString uuid;
    QString name;
    QString localParameters;
    int reconnectDelaySec;
    int bufferingMb;
    bool hwDecode;
    bool clearOnMediaEnd;
    DownlinkRequestBody connRequest;

    SRLinkApiClient *apiClient;
    OBSWeakSourceAutoRelease weakSource; // Don't grab strong reference because cannot finalize by OBS
    OBSSourceAutoRelease decoderSource;
    ImageRenderer *fillerRenderer;
    speaker_layout speakers;
    uint32_t samplesPerSec;
    SourceAudioThread *audioThread;
    OBSSignal renameSignal;
    int revision;
    StageConnection connection;

    void captureSettings(obs_data_t *settings);
    // Return value must be release via obs_data_release()
    obs_data_t *createDecoderSettings();
    // Unregister connection if no stage/seat/source selected.
    const RequestInvoker *putConnection();
    QString compositeParameters(obs_data_t *settings, const DownlinkRequestBody &req, bool remote = false);
    void loadSettings(obs_data_t *settings);
    void saveSettings(obs_data_t *settings);

signals:
    void settingsUpdate(obs_data_t *settings);

private slots:
    void onPutDownlinkFailed(const QString &uuid);
    void onDeleteDownlinkSucceeded(const QString &uuid);
    void onDownlinkReady(const DownlinkInfo &downlink);
    void onLoginSucceeded();
    void onLogoutSucceeded();
    void onWebSocketReady(bool reconnect);
    void onSettingsUpdate(obs_data_t *settings);
    void reactivate();
    
public:
    explicit IngressLinkSource(
        obs_data_t *settings, obs_source_t *source, SRLinkApiClient *_apiClient, QObject *parent = nullptr
    );
    ~IngressLinkSource();

    obs_properties_t *getProperties();
    inline uint32_t getWidth() { return connRequest.getWidth(); }
    inline uint32_t getHeight() { return connRequest.getHeight(); }
    void videoRenderCallback(gs_effect_t* effect);
    void updateCallback(obs_data_t *settings);

    static void getDefaults(obs_data_t *settings, SRLinkApiClient *apiClient);
};

class SourceAudioThread : public QThread, SourceAudioCapture {
    Q_OBJECT

    IngressLinkSource *ingressLinkSource;

public:
    explicit SourceAudioThread(IngressLinkSource *_linkedSource);
    ~SourceAudioThread();

    void run() override;
};
