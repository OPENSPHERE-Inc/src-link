/*
SRC-Link
Copyright (C) 2024 OPENSPHERE Inc. ifo@opensphere.co.jp

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
#include <obs-frontend-api.h>
#include <util/threading.h>

#include <QString>
#include <QRandomGenerator>
#include <QWidget>
#include <QMutex>
#include <QNetworkInterface>

using OBSProperties = OBSPtr<obs_properties_t *, obs_properties_destroy>;
using OBSAudio = OBSPtr<audio_t *, audio_output_close>;

inline void strFree(char *ptr)
{
    bfree(ptr);
}
using OBSString = OBSPtr<char *, strFree>;

inline QString
generatePassword(const int length = 10, const QString &symbol = "_!#%&()*+-.,/~$", const QString &exclude = "lIO")
{
    QString chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890" + symbol;
    QString password;
    for (int i = 0; i < length; i++) {
        auto index = QRandomGenerator::global()->bounded(chars.size());
        auto c = chars[index];
        if (exclude.contains(c)) {
            continue;
        }
        password.append(c);
    }
    return password;
}

inline void applyDefaults(obs_data_t *dest, obs_data_t *src)
{
    for (auto item = obs_data_first(src); item; obs_data_item_next(&item)) {
        auto name = obs_data_item_get_name(item);
        auto type = obs_data_item_gettype(item);

        switch (type) {
        case OBS_DATA_STRING:
            obs_data_set_default_string(dest, name, obs_data_item_get_string(item));
            break;
        case OBS_DATA_NUMBER: {
            auto numtype = obs_data_item_numtype(item);
            if (numtype == OBS_DATA_NUM_DOUBLE) {
                obs_data_set_default_double(dest, name, obs_data_item_get_double(item));
            } else if (numtype == OBS_DATA_NUM_INT) {
                obs_data_set_default_int(dest, name, obs_data_item_get_int(item));
            }
            break;
        }
        case OBS_DATA_BOOLEAN:
            obs_data_set_default_bool(dest, name, obs_data_item_get_bool(item));
            break;
        case OBS_DATA_OBJECT: {
            OBSDataAutoRelease value = obs_data_item_get_obj(item);
            obs_data_set_default_obj(dest, name, value);
            break;
        }
        case OBS_DATA_ARRAY: {
            OBSDataArrayAutoRelease value = obs_data_item_get_array(item);
            obs_data_set_default_array(dest, name, value);
            break;
        }
        case OBS_DATA_NULL:
            break;
        }
    }
}

// Hardcoded in obs-studio/UI/window-basic-main.hpp
#define SIMPLE_ENCODER_X264 "x264"
#define SIMPLE_ENCODER_X264_LOWCPU "x264_lowcpu"
#define SIMPLE_ENCODER_QSV "qsv"
#define SIMPLE_ENCODER_QSV_AV1 "qsv_av1"
#define SIMPLE_ENCODER_NVENC "nvenc"
#define SIMPLE_ENCODER_NVENC_AV1 "nvenc_av1"
#define SIMPLE_ENCODER_NVENC_HEVC "nvenc_hevc"
#define SIMPLE_ENCODER_AMD "amd"
#define SIMPLE_ENCODER_AMD_HEVC "amd_hevc"
#define SIMPLE_ENCODER_AMD_AV1 "amd_av1"
#define SIMPLE_ENCODER_APPLE_H264 "apple_h264"
#define SIMPLE_ENCODER_APPLE_HEVC "apple_hevc"

inline bool encoderAvailable(const char *encoder)
{
    const char *val;
    int i = 0;

    while (obs_enum_encoder_types(i++, &val)) {
        if (strcmp(val, encoder) == 0) {
            return true;
        }
    }

    return false;
}

// Hardcoded in obs-studio/UI/window-basic-main-outputs.cpp
inline const char *getSimpleVideoEncoder(const char *encoder)
{
    if (!strcmp(encoder, SIMPLE_ENCODER_X264)) {
        return "obs_x264";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_X264_LOWCPU)) {
        return "obs_x264";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_QSV)) {
        return "obs_qsv11_v2";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_QSV_AV1)) {
        return "obs_qsv11_av1";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_AMD)) {
        return "h264_texture_amf";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_AMD_HEVC)) {
        return "h265_texture_amf";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_AMD_AV1)) {
        return "av1_texture_amf";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_NVENC)) {
        return encoderAvailable("obs_nvenc_h264_tex") ? "obs_nvenc_h264_tex" // Since OBS 31
               : encoderAvailable("jim_nvenc")        ? "jim_nvenc"          // Until OBS 30
                                                      : "ffmpeg_nvenc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_NVENC_HEVC)) {
        return encoderAvailable("obs_nvenc_hevc_tex") ? "obs_nvenc_hevc_tex" // Since OBS 31
               : encoderAvailable("jim_hevc_nvenc")   ? "jim_hevc_nvenc"     // Until OBS 30
                                                      : "ffmpeg_hevc_nvenc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_NVENC_AV1)) {
        return encoderAvailable("obs_nvenc_av1_tex") ? "obs_nvenc_av1_tex" // Since OBS 31
                                                     : "jim_av1_nvenc";    // Until OBS 30
    } else if (!strcmp(encoder, SIMPLE_ENCODER_APPLE_H264)) {
        return "com.apple.videotoolbox.videoencoder.ave.avc";
    } else if (!strcmp(encoder, SIMPLE_ENCODER_APPLE_HEVC)) {
        return "com.apple.videotoolbox.videoencoder.ave.hevc";
    }

    return "obs_x264";
}

// Hardcoded in obs-studio/UI/window-basic-main-outputs.cpp
inline const char *getSimpleAudioEncoder(const char *encoder)
{
    if (strcmp(encoder, "opus")) {
        return "ffmpeg_opus";
    } else {
        return "ffmpeg_aac";
    }
}

inline QString QTStr(const char *lookupVal)
{
    return QString::fromUtf8(obs_module_text(lookupVal));
}

QImage
takeSourceScreenshot(obs_source_t *source, bool &success, uint32_t requestedWidth = 0, uint32_t requestedHeight = 0);

inline bool isPrivateIPv4(quint32 ip)
{
    return (ip & 0xFF000000) == 0x0A000000 || (ip & 0xFFF00000) == 0xAC100000 || (ip & 0xFFFF0000) == 0xC0A80000;
}

inline QList<QString> getPrivateIPv4Addresses()
{
    QList<QString> privateAddresses;
    auto addresses = QNetworkInterface::allAddresses();

    foreach (auto &address, addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && isPrivateIPv4(address.toIPv4Address())) {
            privateAddresses.append(address.toString());
        }
    }

    return privateAddresses;
}

// Decide source/scene is private or not
inline bool sourceIsPrivate(obs_source_t *source)
{
    auto finder = source;
    auto callback = [](void *param, obs_source_t *_source) {
        auto _finder = (obs_source_t **)param;
        if (_source == *_finder) {
            *_finder = nullptr;
            return false;
        }
        return true;
    };

    obs_enum_scenes(callback, &finder);
    if (finder != nullptr) {
        obs_enum_sources(callback, &finder);
    }

    return finder != nullptr;
}

inline bool isSourceAvailable(obs_source_t *source)
{
    auto width = obs_source_get_width(source);
    auto height = obs_source_get_height(source);
    if (width == 0 || height == 0) {
        return false;
    }

    auto found = !!obs_scene_from_source(source);
    if (found) {
        return true;
    }

    obs_frontend_source_list scenes = {0};
    obs_frontend_get_scenes(&scenes);

    for (size_t i = 0; i < scenes.sources.num && !found; i++) {
        obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
        found = !!obs_scene_find_source_recursive(scene, obs_source_get_name(source));
    }

    obs_frontend_source_list_free(&scenes);

    return found;
}
