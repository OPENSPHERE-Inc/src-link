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
    int port;

    SourceLinkApiClient *apiClient;
    obs_source_t *source;
    obs_source_t *decoderSource;
    bool connected;

    QString compositeParameters(obs_data_t *settings, bool client = false);
    // Return value must be release via obs_data_release()
    obs_data_t *createDecoderSettings(obs_data_t *settings);
    // Unregister connection if no stage/seat/source selected.
    void handleConnection(obs_data_t *settings);


    
public:
    LinkedSource(obs_data_t *settings, obs_source_t* source, SourceLinkApiClient* _apiClient, QObject *parent = nullptr);
    ~LinkedSource();

    obs_properties_t* getProperties();
    void getDefault(obs_data_t *settings);
    // Returns source's actual width
    uint32_t getWidth();
    // Returns source's actual height
    uint32_t getHeight();
    void update(obs_data_t *settings);

    void videoRenderCallback();

private slots:
    void onConnectionPutSucceeded(StageConnection *connection);
    void onConnectionPutFailed();

};
