/*
SR Link
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

#include "../sources/audio-capture.hpp"

class OutputAudioSource : public SourceAudioCapture {
    Q_OBJECT

    audio_t *audio;

    static bool onOutputAudio(
        void *param, uint64_t startTsIn, uint64_t, uint64_t *outTs, uint32_t mixers, audio_output_data *audioData
    );

public:
    explicit OutputAudioSource(
        obs_source_t *source, uint32_t _samplesPerSec, speaker_layout _speakers, QObject *parent = nullptr
    );
    ~OutputAudioSource();

    inline audio_t *getAudio() { return audio; }

    uint64_t popAudio(uint64_t startTsIn, uint32_t mixers, audio_output_data *audioData);
};
