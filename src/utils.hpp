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