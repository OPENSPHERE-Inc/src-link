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
#include <obs.hpp>
#include <util/deque.h>

#include <QObject>
#include <QMutex>

class SourceAudioCapture : public QObject {
    Q_OBJECT

    OBSWeakSourceAutoRelease weakSource;

protected:
    uint32_t samplesPerSec;
    speaker_layout speakers;

    deque audioBuffer;
    size_t audioBufferFrames;
    uint8_t *audioConvBuffer;
    size_t audioConvBufferSize;
    QMutex audioBufferMutex;
    bool active;

public:
    struct AudioBufferHeader {
        size_t data_idx[MAX_AV_PLANES]; // Zero means unused channel
        uint32_t frames;
        speaker_layout speakers;
        audio_format format;
        uint32_t samples_per_sec;
        uint64_t timestamp;
        size_t offset;
    };

    explicit SourceAudioCapture(
        obs_source_t *source, uint32_t _samplesPerSec, speaker_layout _speakers, QObject *parent = nullptr
    );
    ~SourceAudioCapture();

    void pushAudio(const audio_data *audioData, obs_source_t *source);
    inline bool getActive() { return active; }
    inline void setActive(bool value) { active = value; }
    inline QMutex *getAudioBufferMutex() { return &audioBufferMutex; }
    inline deque *getAudioBuffer() { return &audioBuffer; }
    inline uint8_t *getAudioConvBuffer() const { return audioConvBuffer; }
    inline size_t getAudioBufferFrames() const { return audioBufferFrames; }
    inline void decrementAudioBufferFrames(size_t amount) { audioBufferFrames -= amount; }

private:
    static void onSourceAudio(void *param, obs_source_t *, const audio_data *audioData, bool muted);
};