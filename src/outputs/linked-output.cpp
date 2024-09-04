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

#include "linked-output.hpp"

//--- LinkedOutput class ---//

LinkedOutput::LinkedOutput(obs_data_t *settings, obs_output_t *output, SourceLinkApiClient *_apiClient, QObject *parent)
    : QObject(parent),
      apiClient(_apiClient),
      output(output)
{
}

LinkedOutput::~LinkedOutput()
{
}

obs_properties_t *LinkedOutput::getProperties()
{
    auto props = obs_properties_create();

    return props;
}

void LinkedOutput::handleConnection()
{
    if (!partyId.isEmpty() && !partyEventId.isEmpty()) {
        
    } else {

    }
}

//--- Output registration ---//

extern SourceLinkApiClient *apiClient;

obs_properties_t *getProperties(void *data)
{
    auto linkedOutput = static_cast<LinkedOutput *>(data);
}

void getDefaults(obs_data_t *settings)
{
}

void *createOutput(obs_data_t *settings, obs_output_t *output)
{
    return new LinkedOutput(settings, output, apiClient);
}

void destroyOutput(void *data)
{    
    auto linkedOutput = static_cast<LinkedOutput *>(data);
    delete linkedOutput;
}

bool startOutput(void *data)
{
    auto linkedOutput = static_cast<LinkedOutput *>(data);

    return true;
}

void update(void *data, obs_data_t *settings)
{
}

void stopOutput(void *data, uint64_t ts)
{
    auto linkedOutput = static_cast<LinkedOutput *>(data);
}

void outputRawVideo(void *data, video_data *frame)
{
}

void outputRawAudio(void *data, audio_data *frame)
{
}

obs_output_info createLinkedOutputInfo()
{
    obs_output_info info = {};
 
    info.id = "linked_output";
    info.flags = OBS_OUTPUT_AV;

    info.get_name = [](void *type_data) { return "Linked Output"; };
    info.get_properties = getProperties;
    info.get_defaults = getDefaults;
    info.create = createOutput;
    info.destroy = destroyOutput;
    info.start = startOutput;
    info.update = update;
    info.stop = stopOutput;
    info.raw_video = outputRawVideo;
    info.raw_audio = outputRawAudio;

    return info;
}