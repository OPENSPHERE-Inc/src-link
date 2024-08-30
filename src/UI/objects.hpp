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

#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>

class AccountInfo : public QObject {
    Q_OBJECT

    QString id;
    QString displayName;
    QString pictureId;

public:
    explicit inline AccountInfo(QObject *parent = nullptr) : QObject(parent) {}

    inline QString getId() const { return id; }
    inline void setId(const QString &value) { id = value; }

    inline QString getDisplayName() const { return displayName; };
    inline void setDisplayName(const QString &value) { displayName = value; }

    inline QString getPictureId() const { return pictureId; }
    inline void setPictureId(const QString &value) { pictureId = value; }

    static inline AccountInfo *fromJson(const QJsonObject &json)
    {
        AccountInfo *accountInfo = new AccountInfo();
        accountInfo->setId(json["id"].toString());
        accountInfo->setDisplayName(json["display_name"].toString());
        accountInfo->setPictureId(json["picture_id"].toString());
        return accountInfo;
    }
};
