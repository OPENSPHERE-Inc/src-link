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

#include <util/platform.h>

#include "settings.hpp"
#include "utils.hpp"
#include "plugin-support.h"

#define SETTINGS_JSON_NAME "settings.json"

//--- SRLinkSettingsStore class ---//

SRLinkSettingsStore::SRLinkSettingsStore(QObject *parent) : O0AbstractStore(parent)
{
    OBSString config_dir_path = obs_module_get_config_path(obs_current_module(), "");
    os_mkdirs(config_dir_path);

    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    settingsData = obs_data_create_from_json_file(path);
    if (!settingsData) {
        settingsData = obs_data_create();
    }

    obs_log(LOG_DEBUG, "client: SRLinkSettingsStore created");
}

SRLinkSettingsStore::~SRLinkSettingsStore()
{
    obs_log(LOG_DEBUG, "client: SRLinkSettingsStore destroyed");
}

QString SRLinkSettingsStore::value(const QString &key, const QString &defaultValue)
{
    auto value = QString(obs_data_get_string(settingsData, qUtf8Printable(key)));
    if (!value.isEmpty()) {
        return value;
    } else {
        return defaultValue;
    }
}

void SRLinkSettingsStore::setValue(const QString &key, const QString &value)
{
    obs_data_set_string(settingsData, qUtf8Printable(key), qUtf8Printable(value));
    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    obs_data_save_json_safe(settingsData, path, "tmp", "bak");
}
