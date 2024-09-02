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


class LinkedSource : public QObject {
    Q_OBJECT

    QString uuid;

    SourceLinkApiClient *apiClient;
    obs_source_t *source;
    obs_source_t *decoderSource;
    obs_data_t *decoderSettings;
    bool connected;

    void applyDecoderSettings(obs_data_t *settings);
    void applyConnection(obs_data_t *settings);

public:
    LinkedSource(obs_data_t *settings, obs_source_t* source, SourceLinkApiClient* _apiClient, QObject *parent = nullptr);
    ~LinkedSource();

    obs_properties_t* getProperties();
    uint32_t getWidth();
    uint32_t getHeight();
    void update(obs_data_t *settings);

    void videoRenderCallback();

private slots:
    void onConnectionPutSucceeded(StageConnection *connection);
    void onConnectionPutFailed();

};
