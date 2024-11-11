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

#include "audio-source.hpp"

//--- OutputAudioSource class ---//

OutputAudioSource::OutputAudioSource(
    obs_source_t *source, uint32_t _samplesPerSec, speaker_layout _speakers, QObject *parent
)
    : SourceAudioCapture(source, _samplesPerSec, _speakers, parent),
      audio(nullptr)
{
    audio_output_info aoi = {0};
    aoi.name = obs_source_get_name(source);
    aoi.samples_per_sec = _samplesPerSec;
    aoi.speakers = _speakers;
    aoi.format = AUDIO_FORMAT_FLOAT_PLANAR;
    aoi.input_param = this;
    aoi.input_callback = OutputAudioSource::onOutputAudio;

    if (audio_output_open(&audio, &aoi) < 0) {
        audio = nullptr;
        return;
    }

    active = true;
}

OutputAudioSource::~OutputAudioSource()
{
    active = false;

    if (audio) {
        audio_output_close(audio);
    }
}

uint64_t OutputAudioSource::popAudio(uint64_t startTsIn, uint32_t mixers, audio_output_data *audioData)
{
    QMutexLocker locker(&audioBufferMutex);
    {
        if (audioBufferFrames < AUDIO_OUTPUT_FRAMES) {
            // Wait until enough frames are receved.
            // DO NOT stall audio output pipeline
            return startTsIn;
        }

        size_t maxFrames = AUDIO_OUTPUT_FRAMES;

        while (maxFrames > 0 && audioBufferFrames) {
            // Peek header of first chunk
            deque_peek_front(&audioBuffer, audioConvBuffer, sizeof(AudioBufferHeader));
            auto header = (AudioBufferHeader *)audioConvBuffer;
            auto dataSize = sizeof(AudioBufferHeader) + header->speakers * header->frames * 4;

            // Read chunk data
            deque_peek_front(&audioBuffer, audioConvBuffer, dataSize);

            auto chunkFrames = header->frames - header->offset;
            auto frames = (chunkFrames > maxFrames) ? maxFrames : chunkFrames;
            auto outOffset = AUDIO_OUTPUT_FRAMES - maxFrames;

            for (auto tr = 0; tr < MAX_AUDIO_MIXES; tr++) {
                if ((mixers & (1 << tr)) == 0) {
                    continue;
                }
                for (auto ch = 0; ch < header->speakers; ch++) {
                    auto out = audioData[tr].data[ch] + outOffset;
                    if (!header->data_idx[ch]) {
                        continue;
                    }
                    auto in = (float *)(audioConvBuffer + header->data_idx[ch]) + header->offset;

                    for (auto i = 0; i < frames; i++) {
                        *out += *(in++);
                        if (*out > 1.0f) {
                            *out = 1.0f;
                        } else if (*out < -1.0f) {
                            *out = -1.0f;
                        }
                        out++;
                    }
                }
            }

            if (frames == chunkFrames) {
                // Remove fulfilled chunk from buffer
                deque_pop_front(&audioBuffer, NULL, dataSize);
            } else {
                // Chunk frames are remaining -> modify chunk header
                header->offset += frames;
                deque_place(&audioBuffer, 0, header, sizeof(AudioBufferHeader));
            }

            maxFrames -= frames;

            // Decrement buffer usage
            audioBufferFrames -= frames;
        }
    }
    locker.unlock();

    return startTsIn;
}

// Callback from audio_output_open
bool OutputAudioSource::onOutputAudio(
    void *param, uint64_t start_ts_in, uint64_t, uint64_t *out_ts, uint32_t mixers, audio_output_data *mixes
)
{
    auto *audioSource = static_cast<OutputAudioSource *>(param);
    *out_ts = audioSource->popAudio(start_ts_in, mixers, mixes);
    return true;
}
