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

#include <QObject>

#include "../api-client.hpp"
#include "../objects.hpp"

#define OUTPUT_MAX_RETRIES 7
#define OUTPUT_RETRY_DELAY_SECS 1

class LinkedOutput : public QObject {
    Q_OBJECT

    QString sourceName;
    StageConnection *connection;

    SourceLinkApiClient *apiClient;
    obs_service_t *service;
    obs_output_t *output;
    obs_encoder_t *videoEncoder;
    obs_encoder_t *audioEncoder;
    bool outputActive;

    obs_data_t *createEgressSettings(obs_data_t *settings);


public:
    explicit LinkedOutput(
        obs_data_t *settings, SourceLinkApiClient *_apiClient, QObject *parent = nullptr
    );
    ~LinkedOutput();

    obs_properties_t *getProperties();
    void startOutput(obs_data_t *settings, video_t *video, audio_t *audio);
    void stopOutput();

};
