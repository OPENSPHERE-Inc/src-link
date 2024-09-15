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

#include <obs-module.h>

#include <QFrame>
#include <QGraphicsScene>

#include "ui_source-link-dock.h"
#include "../api-client.hpp"

class SourceLinkDock : public QFrame {
    Q_OBJECT

    Ui::SourceLinkDock *ui;

    SourceLinkApiClient *apiClient;

    QImage defaultPartyPicture = QImage(":/source-link/images/unknownparty.png");
    QImage defaultPartyEventPicture = QImage(":/source-link/images/unknownevent.png");

    QGraphicsScene *partyPictureScene;
    QGraphicsScene *partyEventPictureScene;

private slots:
    void onPartiesReady(const QList<Party> &parties);
    void onPartyEventsReady(const QList<PartyEvent> &partyEvents);
    void onActivePartyChanged(int index);
    void onActivePartyEventChanged(int index);
    void onPictureReady(const QString &pictureId, const QImage &picture);
    void onPictureFailed(const QString &pictureId);

public:
    explicit SourceLinkDock(SourceLinkApiClient *_apiClient, QWidget *parent = nullptr);
    ~SourceLinkDock();

};
