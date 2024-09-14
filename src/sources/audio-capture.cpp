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

#include <obs-module.h>

#include "audio-capture.hpp"
#include "../plugin-support.h"

#define MAX_AUDIO_BUFFER_FRAMES 131071

//--- SourceAudioCapture class ---//

SourceAudioCapture::SourceAudioCapture(
    obs_source_t *source, uint32_t _samplesPerSec, speaker_layout _speakers, QObject *parent
)
    : QObject(parent),
      weakSource(obs_source_get_weak_source(source)),
      samplesPerSec(_samplesPerSec),
      speakers(_speakers),
      audioBuffer({0}),
      audioBufferFrames(0),
      audioConvBuffer(nullptr),
      audioConvBufferSize(0)
{
    obs_source_add_audio_capture_callback(source, onSourceAudio, this);    
    obs_log(LOG_DEBUG, "%s: Source audio capture created.", obs_source_get_name(source));
}

SourceAudioCapture::~SourceAudioCapture()
{
    OBSSourceAutoRelease source = obs_weak_source_get_source(weakSource);
    obs_source_remove_audio_capture_callback(source, onSourceAudio, this);
    
    deque_free(&audioBuffer);
    bfree(audioConvBuffer);

    obs_log(LOG_DEBUG, "%s: Source audio capture destroyed.", obs_source_get_name(source));
}

void SourceAudioCapture::pushAudio(const audio_data *audioData, obs_source_t *source)
{
    if (!active) {
        return;
    }

    audioBufferMutex.lock();
    {
        // Push audio data to buffer
        if (audioBufferFrames + audioData->frames > MAX_AUDIO_BUFFER_FRAMES) {
            obs_log(LOG_WARNING, "%s: The audio buffer is full", obs_source_get_name(source));
            deque_free(&audioBuffer);
            deque_init(&audioBuffer);
            audioBufferFrames = 0;
        }

        // Compute header
        AudioBufferHeader header = {0};
        header.frames = audioData->frames;
        header.timestamp = audioData->timestamp;
        header.samples_per_sec = samplesPerSec;
        header.speakers = speakers;
        header.format = AUDIO_FORMAT_FLOAT_PLANAR;

        for (auto i = 0, channels = 0; i < header.speakers; i++) {
            if (!audioData->data[i]) {
                continue;
            }
            header.data_idx[i] = sizeof(AudioBufferHeader) + channels * audioData->frames * 4;
            channels++;
        }

        // Push audio data to buffer
        deque_push_back(&audioBuffer, &header, sizeof(AudioBufferHeader));
        for (auto i = 0; i < header.speakers; i++) {
            if (!audioData->data[i]) {
                continue;
            }
            deque_push_back(&audioBuffer, audioData->data[i], audioData->frames * 4);
        }

        auto dataSize = sizeof(AudioBufferHeader) + header.speakers * header.frames * 4;
        if (dataSize > audioConvBufferSize) {
            obs_log(
                LOG_DEBUG, "%s: Expand audioConvBuffer from %zu to %zu bytes",
                obs_source_get_name(source), audioConvBufferSize, dataSize
            );
            audioConvBuffer = (uint8_t *)brealloc(audioConvBuffer, dataSize);
            audioConvBufferSize = dataSize;
        }

        audioBufferFrames += audioData->frames;
    }
    audioBufferMutex.unlock();
}

// Callback from obs_source_add_audio_capture_callback
void SourceAudioCapture::onSourceAudio(void *param, obs_source_t *source, const audio_data *audioData, bool muted)
{
     if (muted) {
        return;
    }

    auto sourceCapture = (SourceAudioCapture *)param;
    sourceCapture->pushAudio(audioData, source);
}