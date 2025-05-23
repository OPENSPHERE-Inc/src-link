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

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QUrlQuery>
#include <QJsonDocument>

#include "../plugin-support.h"
#include "../utils.hpp"
#include "ingress-link-source.hpp"

#define SETTINGS_JSON_NAME "ingress-link-source.json"
#define FILLER_IMAGE_NAME "filler.jpg"
#define PORTS_ERROR_IMAGE_NAME "ports-error.jpg"
#define CONNECTING_IMAGE_NAME "connecting.jpg"
#define UNREACHABLE_IMAGE_NAME "unreachable.jpg"

//--- IngressLinkSource class ---//

IngressLinkSource::IngressLinkSource(
    obs_data_t *settings, obs_source_t *_source, SRCLinkApiClient *_apiClient, QObject *parent
)
    : QObject(parent),
      weakSource(obs_source_get_weak_source(_source)),
      apiClient(_apiClient),
      uuid(obs_source_get_uuid(_source)),
      audioThread(nullptr),
      revision(0)
{
    name = obs_source_get_name(_source);
    obs_log(LOG_DEBUG, "%s: Source creating", qUtf8Printable(name));
    connRequest.setPort(0);
    connRequest.setRelayApps(QJsonArray({RELAY_APP_SRTRELAY, RELAY_APP_MEDIAMTX}));

    if (!strcmp(obs_data_get_json(settings), "{}")) {
        // Initial creation -> Load recently settings from file
        loadSettings(settings);
    }

    // Capture source's settings first
    captureSettings(settings);

    // Create decoder private source (SRT, RIST, etc.)
    OBSDataAutoRelease decoderSettings = createDecoderSettings();
    QString decoderName = QString("%1 (decoder)").arg(obs_source_get_name(_source));
    decoderSource = obs_source_create_private("ffmpeg_source", qUtf8Printable(decoderName), decoderSettings);
    obs_source_inc_active(decoderSource);

    // Create filler private source
    QString fillerFile = QString("%1/%2").arg(obs_get_module_data_path(obs_current_module())).arg(FILLER_IMAGE_NAME);
    fillerRenderer = new ImageRenderer(false, fillerFile, this);

    QString portsErrorFile =
        QString("%1/%2").arg(obs_get_module_data_path(obs_current_module())).arg(PORTS_ERROR_IMAGE_NAME);
    portsErrorRenderer = new ImageRenderer(false, portsErrorFile, this);

    QString connectingFile =
        QString("%1/%2").arg(obs_get_module_data_path(obs_current_module())).arg(CONNECTING_IMAGE_NAME);
    connectingRenderer = new ImageRenderer(false, connectingFile, this);

    QString unreachableFile =
        QString("%1/%2").arg(obs_get_module_data_path(obs_current_module())).arg(UNREACHABLE_IMAGE_NAME);
    unreachableRenderer = new ImageRenderer(false, unreachableFile, this);

    // Register connection to server
    putConnection();

    // Start audio handling
    obs_audio_info ai = {0};
    obs_get_audio_info(&ai);
    speakers = ai.speakers;
    samplesPerSec = ai.samples_per_sec;

    startAudio();

    connect(apiClient, SIGNAL(downlinkReady(const DownlinkInfo &)), this, SLOT(onDownlinkReady(const DownlinkInfo &)));
    connect(apiClient, SIGNAL(downlinkRemoved(const QString &)), this, SLOT(onDeleteDownlinkSucceeded(const QString &)));
    connect(apiClient, SIGNAL(putDownlinkFailed(const QString &)), this, SLOT(onPutDownlinkFailed(const QString &)));
    connect(
        apiClient, SIGNAL(putDownlinkStatusFailed(const QString &)), this, SLOT(onPutDownlinkFailed(const QString &))
    );
    connect(
        apiClient, SIGNAL(deleteDownlinkSucceeded(const QString &)), this,
        SLOT(onDeleteDownlinkSucceeded(const QString &))
    );
    connect(apiClient, SIGNAL(stagesReady(const StageArray &)), this, SLOT(onStagesReady(const StageArray &)));
    connect(apiClient, &SRCLinkApiClient::licenseChanged, [this](const SubscriptionLicense &license) {
        if (license.getLicenseValid()) {
            reactivate();
        }
    });
    connect(apiClient, &SRCLinkApiClient::ingressRefreshNeeded, [this]() { reactivate(); });
    connect(apiClient, SIGNAL(loginSucceeded()), this, SLOT(onLoginSucceeded()));
    connect(apiClient, SIGNAL(logoutSucceeded()), this, SLOT(onLogoutSucceeded()));
    connect(this, SIGNAL(settingsUpdate(obs_data_t *)), this, SLOT(onSettingsUpdate(obs_data_t *)));

    renameSignal.Connect(
        obs_source_get_signal_handler(_source), "rename",
        [](void *data, calldata_t *cd) {
            auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
            ingressLinkSource->name = calldata_string(cd, "new_name");
        },
        this
    );

    obs_frontend_add_event_callback(onOBSFrontendEvent, this);

    obs_log(LOG_INFO, "%s: Source created", qUtf8Printable(name));
}

IngressLinkSource::~IngressLinkSource()
{
    disconnect(this);

    apiClient->deleteDownlink(uuid, true);
    if (connRequest.getPort()) {
        apiClient->releasePort(connRequest.getPort());
    }

    stopAudio();

    obs_frontend_remove_event_callback(onOBSFrontendEvent, this);
}

void IngressLinkSource::destroyCallback()
{
    obs_log(LOG_DEBUG, "%s: Source destroying", qUtf8Printable(name));

    renameSignal.Disconnect();

    // Free decoder source
    obs_source_dec_active(decoderSource);
    decoderSource = nullptr;
    weakSource = nullptr;

    // Delete self in proper thread
    deleteLater();

    obs_log(LOG_INFO, "%s: Source destroyed", qUtf8Printable(name));
}

void IngressLinkSource::onOBSFrontendEvent(enum obs_frontend_event event, void *param)
{
    auto source = static_cast<IngressLinkSource *>(param);
    // Force stop on shutdown
    switch (event) {
    case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
        source->stopAudio();
        break;
    default:
        // Nothing to do
        break;
    }
}

void IngressLinkSource::startAudio()
{
    if (audioThread) {
        return;
    }

    audioThread = new SourceAudioThread(this, this);
    audioThread->start();
}

void IngressLinkSource::stopAudio()
{
    if (!audioThread) {
        return;
    }

    // Destroy decoder private source
    audioThread->requestInterruption();
    audioThread->wait();
    delete audioThread;
    audioThread = nullptr;
}

QString IngressLinkSource::compositeParameters(obs_data_t *settings, const DownlinkRequestBody &req)
{
    auto apiSettings = apiClient->getSettings();
    QString parameters;

    if (req.getProtocol() == "srt") {
        // Generate SRT parameters
        auto latency = apiSettings->getIngressSrtLatency();
        if (obs_data_get_bool(settings, "advanced_settings")) {
            latency = (int)obs_data_get_int(settings, "srt_latency");
        }

        if (req.getRelay()) {
            // FIXME: Currently encryption not supported !
            parameters = QString("latency=%1").arg(latency * 1000); // Convert to microseconds

        } else {
            parameters = QString("latency=%1&pbkeylen=%2")
                             .arg(latency * 1000) // Convert to microseconds
                             .arg(apiSettings->getIngressSrtPbkeylen());
        }
    }

    return parameters;
}

void IngressLinkSource::loadSettings(obs_data_t *settings)
{
    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    OBSDataAutoRelease recently_settings = obs_data_create_from_json_file(path);

    if (recently_settings) {
        obs_data_erase(recently_settings, "stage_id");
        obs_data_erase(recently_settings, "seat_name");
        obs_data_erase(recently_settings, "source_name");
        obs_data_apply(settings, recently_settings);
    }
}

void IngressLinkSource::saveSettings(obs_data_t *settings)
{
    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    obs_data_save_json_safe(settings, path, "tmp", "bak");
}

void IngressLinkSource::captureSettings(obs_data_t *settings)
{
    auto newRequest = connRequest;
    newRequest.setProtocol(apiClient->getSettings()->getIngressProtocol());
    newRequest.setLanServer(apiClient->retrievePrivateIp());

    auto stageId = obs_data_get_string(settings, "stage_id");
    newRequest.setStageId(stageId);

    newRequest.setSeatName(obs_data_get_string(settings, "seat_name"));
    newRequest.setSourceName(obs_data_get_string(settings, "source_name"));
    newRequest.setMaxBitrate((int)obs_data_get_int(settings, "max_bitrate"));
    newRequest.setMinBitrate((int)obs_data_get_int(settings, "min_bitrate"));
    newRequest.setWidth((int)obs_data_get_int(settings, "width"));
    newRequest.setHeight((int)obs_data_get_int(settings, "height"));

    hwDecode = obs_data_get_bool(settings, "hw_decode");
    clearOnMediaEnd = obs_data_get_bool(settings, "clear_on_media_end");

    newRequest.setRelay(obs_data_get_bool(settings, "relay"));

    if (obs_data_get_bool(settings, "advanced_settings")) {
        reconnectDelaySec = (int)obs_data_get_int(settings, "reconnect_delay_sec");
        bufferingMb = (int)obs_data_get_int(settings, "buffering_mb");
    } else {
        reconnectDelaySec = apiClient->getSettings()->getIngressReconnectDelayTime();
        bufferingMb = apiClient->getSettings()->getIngressNetworkBufferSize();
    }

    // Generate new stream ID here (passphrase will be generated in the server)
    newRequest.setStreamId(generatePassword(32, ""));
    newRequest.setParameters(compositeParameters(settings, newRequest));

    // Increment revision when we have some changes
    if (newRequest != connRequest) {
        revision++;
    }
    newRequest.setRevision(revision);

    connRequest = newRequest;

    // Re-allocate port
    if (connRequest.getPort()) {
        apiClient->releasePort(connRequest.getPort());
        connRequest.setPort(0);
    }
    if (!connRequest.getRelay()) {
        connRequest.setPort(apiClient->getFreePort());
    }
}

obs_data_t *IngressLinkSource::createDecoderSettings()
{
    auto decoderSettings = obs_data_create();

    if (!connection.getAllocationId().isEmpty() && connection.getProtocol() == "srt") {
        QUrl input;
        input.setScheme("srt");
        input.setHost("0.0.0.0");
        input.setPort(connection.getPort());

        QUrlQuery parameters(connection.getParameters());

        if (connection.getRelay()) {
            // No latency override on relay mode
            // FIXME: Currently encryption not supported !
            parameters.addQueryItem("mode", "caller");
            if (connection.getRelayApp() == RELAY_APP_MEDIAMTX) {
                parameters.addQueryItem(
                    "streamid", QString("read:%1:%2:%3")
                                    .arg(connection.getStreamId())
                                    .arg(connection.getId())
                                    .arg(connection.getPassphrase())
                );
            } else {
                parameters.addQueryItem(
                    "streamid", QString("play/%1/%2").arg(connection.getStreamId()).arg(connection.getPassphrase())
                );
            }
            input.setHost(connection.getServer());
        } else {
            if (connection.getLatency()) {
                // Override latency with participant's settings
                parameters.removeQueryItem("latency");
                parameters.addQueryItem(
                    "latency", QString::number(connection.getLatency() * 1000)
                ); // Convert to microseconds
            }

            parameters.addQueryItem("mode", "listener");
            parameters.addQueryItem("streamid", connection.getStreamId());
            parameters.addQueryItem("passphrase", connection.getPassphrase());
        }

        input.setQuery(parameters);

        obs_data_set_string(decoderSettings, "input", qUtf8Printable(input.toString()));
        obs_log(LOG_DEBUG, "%s: SRT input is %s", qUtf8Printable(name), qUtf8Printable(input.toString()));

    } else {
        obs_data_set_string(decoderSettings, "input", "");
        obs_log(LOG_DEBUG, "%s: SRT input is empty!", qUtf8Printable(name));
    }

    obs_data_set_int(decoderSettings, "reconnect_delay_sec", reconnectDelaySec);
    obs_data_set_int(decoderSettings, "buffering_mb", bufferingMb);
    obs_data_set_bool(decoderSettings, "hw_decode", hwDecode);
    obs_data_set_bool(decoderSettings, "clear_on_media_end", clearOnMediaEnd);

    // Static parameters
    obs_data_set_string(decoderSettings, "input_format", "mpegts");
    obs_data_set_bool(decoderSettings, "is_local_file", false);

    return decoderSettings;
}

// Passphrase will be generated in the server
// The connection will be activated on onDownlinkReady()
const RequestInvoker *IngressLinkSource::putConnection()
{
    auto port = connRequest.getPort();
    auto relay = connRequest.getRelay();
    auto stageId = connRequest.getStageId();
    auto seatName = connRequest.getSeatName();
    auto sourceName = connRequest.getSourceName();

    if ((port > 0 || relay) && !stageId.isEmpty() && !seatName.isEmpty() && !sourceName.isEmpty()) {
        // Register connection to server
        return apiClient->putDownlink(uuid, connRequest);
    } else {
        if (!port && !relay) {
            obs_log(LOG_ERROR, "%s: Port is not available.", qUtf8Printable(name));
        }
        // Unregister connection if no stage/seat/source selected.
        return apiClient->deleteDownlink(uuid);
    }
}

obs_properties_t *IngressLinkSource::getProperties()
{
    obs_log(LOG_DEBUG, "%s: Properties creating", qUtf8Printable(name));
    auto props = obs_properties_create();
    obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

    // Connection group
    auto connectionGroup = obs_properties_create();
    obs_properties_add_group(props, "connection", obs_module_text("Connection"), OBS_GROUP_NORMAL, connectionGroup);

    // Connection group -> Stage list
    auto stageList = obs_properties_add_list(
        connectionGroup, "stage_id", obs_module_text("Receiver"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(stageList, "", "");
    foreach (auto &stage, apiClient->getStages().values()) {
        obs_property_list_add_string(
            stageList,
            stage.getOwnerUserId() == apiClient->getAccountInfo().getAccount().getId()
                ? qUtf8Printable(stage.getName())
                : qUtf8Printable(
                      QString("%1 (%2)").arg(stage.getName()).arg(stage.getOwnerAccountView().getDisplayName())
                  ),
            qUtf8Printable(stage.getId())
        );
    }

    // Connection group -> Seat list
    auto seatList = obs_properties_add_list(
        connectionGroup, "seat_name", obs_module_text("Slot"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(seatList, "", "");

    // Connection group -> Source list
    auto sourceList = obs_properties_add_list(
        connectionGroup, "source_name", obs_module_text("Source"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(sourceList, "", "");

    // Stage change stage -> Update connection group
    obs_property_set_modified_callback2(
        stageList,
        [](void *param, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
            obs_log(LOG_DEBUG, "Receiver has been changed.");

            auto _apiClient = static_cast<SRCLinkApiClient *>(param);
            QString stageId = obs_data_get_string(settings, "stage_id");

            auto _connectionGroup = obs_property_group_content(obs_properties_get(_props, "connection"));

            auto _seatList = obs_properties_get(_connectionGroup, "seat_name");
            obs_property_list_clear(_seatList);

            auto _sourceList = obs_properties_get(_connectionGroup, "source_name");
            obs_property_list_clear(_sourceList);

            if (!_apiClient->getStages().size()) {
                obs_property_list_add_string(_seatList, "", "");
                obs_property_list_add_string(_sourceList, "", "");
                return true;
            }

            auto _stage =
                _apiClient->getStages().find([stageId](const Stage &stage) { return stage.getId() == stageId; });

            if (!_stage.getSeats().size()) {
                obs_property_list_add_string(_seatList, "", "");
            } else {
                obs_property_list_add_string(_seatList, "", "");
            }
            if (!_stage.getSources().size()) {
                obs_property_list_add_string(_sourceList, "", "");
            } else {
                obs_property_list_add_string(_sourceList, "", "");
            }

            foreach (auto &seat, _stage.getSeats().values()) {
                obs_property_list_add_string(
                    _seatList, qUtf8Printable(seat.getDisplayName()), qUtf8Printable(seat.getName())
                );
            }
            foreach (auto &source, _stage.getSources().values()) {
                obs_property_list_add_string(
                    _sourceList, qUtf8Printable(source.getDisplayName()), qUtf8Printable(source.getName())
                );
            }

            auto relay = obs_properties_get(_connectionGroup, "relay");
            auto relayServerAvailable = _stage.getRelayServers().size() > 0;
            obs_property_set_enabled(relay, relayServerAvailable);
            obs_data_set_bool(settings, "relay", obs_data_get_bool(settings, "relay") && relayServerAvailable);

            return true;
        },
        apiClient
    );

    // Connection group -=> Reload button
    obs_properties_add_button2(
        connectionGroup, "reload_stages", obs_module_text("ReloadReceivers"),
        [](obs_properties_t *, obs_property_t *, void *param) {
            auto ingressLinkSource = static_cast<IngressLinkSource *>(param);
            auto invoker = ingressLinkSource->apiClient->requestStages();
            if (invoker) {
                QObject::connect(
                    invoker, &RequestInvoker::finished,
                    [ingressLinkSource](QNetworkReply::NetworkError, QByteArray) {
                        // Reload source properties
                        OBSSourceAutoRelease source = obs_weak_source_get_source(ingressLinkSource->weakSource);
                        obs_frontend_open_source_properties(source);
                    }
                );
            }

            return true;
        },
        this
    );

    // Connection group -> Create button
    obs_properties_add_button2(
        connectionGroup, "manage_stages", obs_module_text("ManageReceivers"),
        [](obs_properties_t *, obs_property_t *, void *param) {
            auto ingressLinkSource = static_cast<IngressLinkSource *>(param);
            ingressLinkSource->apiClient->openStagesPage();
            return true;
        },
        this
    );

    // Connection group -> Relay checkbox
    obs_properties_add_bool(connectionGroup, "relay", obs_module_text("UseRelayServer"));

    // Other stage settings
    auto maxBitrate = obs_properties_add_int(props, "max_bitrate", obs_module_text("MaxBitrate"), 0, 1000000000, 100);
    obs_property_int_set_suffix(maxBitrate, " kbps");

    auto minBitrate = obs_properties_add_int(props, "min_bitrate", obs_module_text("MinBitrate"), 0, 1000000000, 100);
    obs_property_int_set_suffix(minBitrate, " kbps");

    auto width = obs_properties_add_int(props, "width", obs_module_text("SpecifiedWidth"), 0, 3840, 2);
    obs_property_int_set_suffix(width, " px");

    auto height = obs_properties_add_int(props, "height", obs_module_text("SpecifiedHeight"), 0, 2160, 2);
    obs_property_int_set_suffix(height, " px");

    // Private source settings
    obs_properties_add_bool(props, "hw_decode", obs_module_text("HardwareDecode"));
    obs_properties_add_bool(props, "clear_on_media_end", obs_module_text("ClearOnMediaEnd"));

    auto advancedSettings = obs_properties_add_bool(props, "advanced_settings", obs_module_text("AdvancedSettings"));
    obs_property_set_modified_callback2(
        advancedSettings,
        [](void *param, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
            IngressLinkSource *ingressLinkSource = static_cast<IngressLinkSource *>(param);
            auto apiSettings = ingressLinkSource->apiClient->getSettings();
            auto advanced = obs_data_get_bool(settings, "advanced_settings");

            obs_property_set_visible(obs_properties_get(_props, "reconnect_delay_sec"), advanced);
            obs_property_set_visible(obs_properties_get(_props, "buffering_mb"), advanced);
            obs_property_set_visible(
                obs_properties_get(_props, "srt_latency"), advanced && apiSettings->getIngressProtocol() == "srt"
            );

            return true;
        },
        this
    );

    // Advanced settings
    auto reconnectDelay =
        obs_properties_add_int_slider(props, "reconnect_delay_sec", obs_module_text("ReconnectDelayTime"), 1, 60, 1);
    obs_property_int_set_suffix(reconnectDelay, " secs");

    auto buffering = obs_properties_add_int_slider(props, "buffering_mb", obs_module_text("BufferingMB"), 0, 16, 1);
    obs_property_int_set_suffix(buffering, " MB");

    auto srtLatency = obs_properties_add_int(props, "srt_latency", obs_module_text("LatencyMsecs"), 0, 60000, 1);
    obs_property_int_set_suffix(srtLatency, " ms");
    obs_property_set_visible(srtLatency, apiClient->getSettings()->getIngressProtocol() == "srt");

    obs_log(LOG_DEBUG, "%s: Properties created", qUtf8Printable(name));
    return props;
}

void IngressLinkSource::getDefaults(obs_data_t *settings, SRCLinkApiClient *apiClient)
{
    obs_log(LOG_DEBUG, "Default settings applying.");

    obs_data_set_default_bool(settings, "hw_decode", false);
    obs_data_set_default_bool(settings, "clear_on_media_end", false);
    obs_data_set_default_int(settings, "max_bitrate", 10000);
    obs_data_set_default_int(settings, "min_bitrate", 5000);
    obs_data_set_default_bool(settings, "advanced_settings", false);
    obs_data_set_default_int(settings, "srt_latency", apiClient->getSettings()->getIngressSrtLatency());
    obs_data_set_default_int(settings, "reconnect_delay_sec", apiClient->getSettings()->getIngressReconnectDelayTime());
    obs_data_set_default_int(settings, "buffering_mb", apiClient->getSettings()->getIngressNetworkBufferSize());

    obs_video_info ovi = {0};
    if (obs_get_video_info(&ovi)) {
        obs_data_set_default_int(settings, "width", ovi.base_width);
        obs_data_set_default_int(settings, "height", ovi.base_height);
    }

    obs_log(LOG_DEBUG, "Default settings applied.");
}

void IngressLinkSource::videoRenderCallback(gs_effect_t *effect)
{
    // Just pass through the video
    if (!connection.isEmpty()) {
        if (connection.getConnectionAdvices().getUnreachable()) {
            // Display unreachable image
            unreachableRenderer->render(effect, getWidth(), getHeight());
        } else if (!clearOnMediaEnd && (!obs_source_get_width(decoderSource) || !obs_source_get_height(decoderSource))) {
            // Display connecting image
            connectingRenderer->render(effect, getWidth(), getHeight());
        } else {
            obs_source_video_render(decoderSource);
        }
    } else {
        if (!connRequest.getPort() && !connRequest.getRelay()) {
            // Display ports error image
            portsErrorRenderer->render(effect, getWidth(), getHeight());
        } else {
            // Display filler image
            fillerRenderer->render(effect, getWidth(), getHeight());
        }
    }
}

void IngressLinkSource::onSettingsUpdate(obs_data_t *settings)
{
    obs_log(LOG_DEBUG, "%s: Source updating", qUtf8Printable(name));

    captureSettings(settings);
    connect(putConnection(), &RequestInvoker::finished, [this, settings](QNetworkReply::NetworkError error, QByteArray) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "%s: Source update failed", qUtf8Printable(name));
            return;
        }

        // Store settings to file as recently settings.
        saveSettings(settings);

        obs_log(LOG_INFO, "%s: Source updated", qUtf8Printable(name));
    });
}

void IngressLinkSource::resetDecoder(const StageConnection &_connection)
{
    connection = _connection;

    // Update decoder settings
    OBSDataAutoRelease decoderSettings = createDecoderSettings();
    obs_source_update(decoderSource, decoderSettings);
}

void IngressLinkSource::onPutDownlinkFailed(const QString &_uuid)
{
    if (_uuid != uuid) {
        return;
    }
    resetDecoder();
}

void IngressLinkSource::onDeleteDownlinkSucceeded(const QString &_uuid)
{
    if (_uuid != uuid) {
        return;
    }
    resetDecoder();
}

void IngressLinkSource::onDownlinkReady(const DownlinkInfo &downlink)
{
    if (downlink.getConnection().getId() != uuid) {
        return;
    }

    // DO NOT unify revision and connection. Connection is possibly emptied.
    auto incomingConnection = downlink.getConnection();
    auto populated = !connection.getAllocationId().isEmpty();
    auto reconnectionNeeded = populated != !incomingConnection.getAllocationId().isEmpty() ||
                              revision < incomingConnection.getRevision() ||
                              connection.getPassphrase() != incomingConnection.getPassphrase();

    if (reconnectionNeeded) {
        // Prevent infinite loop
        revision = incomingConnection.getRevision();
        resetDecoder(incomingConnection);
    } else {
        // Just update data
        connection = incomingConnection;
    }
}

void IngressLinkSource::onStagesReady(const StageArray &stages)
{
    auto stageId = connRequest.getStageId();
    auto seatName = connRequest.getSeatName();
    auto sourceName = connRequest.getSourceName();

    if (!connection.isEmpty() || stageId.isEmpty() || seatName.isEmpty() || sourceName.isEmpty()) {
        return;
    }

    // No connection but stage/seat/source selected situation
    auto stage = stages.find([this, stageId, seatName, sourceName](const Stage &_stage) {
        if (_stage.getId() != stageId) {
            return false;
        }
        auto seat =
            _stage.getSeats().find([this, seatName](const StageSeat &_seat) { return _seat.getName() == seatName; });
        if (seat.isEmpty()) {
            return false;
        }
        auto source = _stage.getSources().find([this, sourceName](const StageSource &_source) {
            return _source.getName() == sourceName;
        });
        return !source.isEmpty();
    });

    if (stage.isEmpty()) {
        return;
    }

    // Stage/seat/source found -> Re-put connection
    putConnection();
}

// This is called when link or refresh token succeeded
void IngressLinkSource::onLoginSucceeded()
{
    if (!connection.isEmpty()) {
        return;
    }

    // Re-put connection
    putConnection();
}

void IngressLinkSource::onLogoutSucceeded()
{
    resetDecoder();
}

void IngressLinkSource::reactivate()
{
    obs_log(LOG_DEBUG, "%s: Source reactivating with rev.%d", qUtf8Printable(name), revision);
    OBSSourceAutoRelease source = obs_weak_source_get_source(weakSource);
    OBSDataAutoRelease settings = obs_source_get_settings(source);

    onSettingsUpdate(settings);
    obs_log(LOG_DEBUG, "%s: Source reactivated with rev.%d", qUtf8Printable(name), revision);
}

void IngressLinkSource::updateCallback(obs_data_t *settings)
{
    // Note: apiClient instance might live in a different thread
    emit settingsUpdate(settings);
}

//--- SourceLinkAudioThread class ---//

SourceAudioThread::SourceAudioThread(IngressLinkSource *_linkedSource, QObject *parent)
    : QThread(parent),
      ingressLinkSource(_linkedSource),
      audioCapture(_linkedSource->decoderSource, _linkedSource->samplesPerSec, _linkedSource->speakers)
{
    obs_log(LOG_DEBUG, "%s: Audio thread creating.", qUtf8Printable(ingressLinkSource->name));
}

SourceAudioThread::~SourceAudioThread()
{
    if (isRunning()) {
        requestInterruption();
        wait();
    }

    obs_log(LOG_DEBUG, "%s: Audio thread destroyed.", qUtf8Printable(ingressLinkSource->name));
}

void SourceAudioThread::run()
{
    obs_log(LOG_DEBUG, "%s: Audio thread started.", qUtf8Printable(ingressLinkSource->name));
    audioCapture.setActive(true);

    while (!isInterruptionRequested()) {
        QMutexLocker locker(audioCapture.getAudioBufferMutex());
        {
            OBSSourceAutoRelease source = obs_weak_source_get_source(ingressLinkSource->weakSource);
            if (!source) {
                break;
            }
            if (!audioCapture.getAudioBufferFrames()) {
                // No data at this time
                locker.unlock();
                msleep(10);
                continue;
            }

            // Peek header of first chunk
            deque_peek_front(
                audioCapture.getAudioBuffer(), audioCapture.getAudioConvBuffer(),
                sizeof(SourceAudioCapture::AudioBufferHeader)
            );
            auto header = (SourceAudioCapture::AudioBufferHeader *)audioCapture.getAudioConvBuffer();
            size_t dataSize = sizeof(SourceAudioCapture::AudioBufferHeader) + header->speakers * header->frames * 4;

            // Read chunk data
            deque_pop_front(audioCapture.getAudioBuffer(), audioCapture.getAudioConvBuffer(), dataSize);

            // Create audio data to send source output
            obs_source_audio audioData = {0};
            audioData.frames = header->frames;
            audioData.timestamp = header->timestamp;
            audioData.speakers = header->speakers;
            audioData.format = header->format;
            audioData.samples_per_sec = header->samples_per_sec;

            for (int i = 0; i < header->speakers; i++) {
                if (!header->data_idx[i]) {
                    continue;
                }
                audioData.data[i] = audioCapture.getAudioConvBuffer() + header->data_idx[i];
            }

            // Send data to source output
            obs_source_output_audio(source, &audioData);

            audioCapture.decrementAudioBufferFrames(header->frames);
        }
        locker.unlock();
    }

    audioCapture.setActive(false);
    obs_log(LOG_DEBUG, "%s: Audio thread stopped.", qUtf8Printable(ingressLinkSource->name));
}

//--- Source registration ---//

extern SRCLinkApiClient *apiClient;

void *createSource(obs_data_t *settings, obs_source_t *source)
{
    return new IngressLinkSource(settings, source, apiClient);
}

void destroySource(void *data)
{
    auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
    // Delete with thread-safe way
    ingressLinkSource->destroyCallback();
}

obs_properties_t *getProperties(void *data)
{
    auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
    return ingressLinkSource->getProperties();
}

void getDefaults(obs_data_t *settings)
{
    IngressLinkSource::getDefaults(settings, apiClient);
}

uint32_t getWidth(void *data)
{
    auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
    return ingressLinkSource->getWidth();
}

uint32_t getHeight(void *data)
{
    auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
    return ingressLinkSource->getHeight();
}

void videoRender(void *data, gs_effect_t *effect)
{
    auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
    ingressLinkSource->videoRenderCallback(effect);
}

void update(void *data, obs_data_t *settings)
{
    auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
    ingressLinkSource->updateCallback(settings);
}

obs_source_info createLinkedSourceInfo()
{
    obs_source_info sourceInfo = {0};

    sourceInfo.id = "ingress_link_source";
    sourceInfo.type = OBS_SOURCE_TYPE_INPUT;
    sourceInfo.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;

    sourceInfo.get_name = [](void *) {
        return obs_module_text("DownlinkInput");
    };
    sourceInfo.create = createSource;
    sourceInfo.destroy = destroySource;
    sourceInfo.get_properties = getProperties;
    sourceInfo.get_defaults = getDefaults;
    sourceInfo.get_width = getWidth;
    sourceInfo.get_height = getHeight;
    sourceInfo.video_render = videoRender;
    sourceInfo.update = update;

    return sourceInfo;
}
