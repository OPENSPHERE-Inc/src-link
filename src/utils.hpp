/*
Source Link
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

#include <QString>
#include <QRandomGenerator>

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
            auto value = obs_data_item_get_obj(item);
            obs_data_set_default_obj(dest, name, value);
            obs_data_release(value);
            break;
        }
        case OBS_DATA_ARRAY: {
            auto value = obs_data_item_get_array(item);
            obs_data_set_default_array(dest, name, value);
            obs_data_array_release(value);
            break;
        }
        case OBS_DATA_NULL:
            break;
        }
    }
}