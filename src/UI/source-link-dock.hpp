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

#include "../api-client.hpp"
#include "output-dialog.hpp"
#include "ui_source-link-dock.h"
#include "ui_source-link-connection-widget.h"

class SourceLinkConnectionWidget;

class SourceLinkDock : public QFrame {
    Q_OBJECT

    Ui::SourceLinkDock *ui;

    SourceLinkApiClient *apiClient;

    QImage defaultPartyPicture = QImage(":/source-link/images/unknownparty.png");
    QImage defaultPartyEventPicture = QImage(":/source-link/images/unknownevent.png");

    QGraphicsScene *partyPictureScene;
    QGraphicsScene *partyEventPictureScene;
    QList<SourceLinkConnectionWidget *> connectionWidgets;

    void updateConnections(const Stage &stage);

private slots:
    void onPartiesReady(const QList<Party> &parties);
    void onPartyEventsReady(const QList<PartyEvent> &partyEvents);
    void onActivePartyChanged(int index);
    void onActivePartyEventChanged(int index);
    void onPictureReady(const QString &pictureId, const QImage &picture);
    void onPictureFailed(const QString &pictureId);
    void onSeatAllocationReady(const StageSeatInfo &seat);
    void onSeatAllocationFailed();

public:
    explicit SourceLinkDock(SourceLinkApiClient *_apiClient, QWidget *parent = nullptr);
    ~SourceLinkDock();
};

class SourceLinkConnectionWidget : public QWidget {
    Q_OBJECT

    friend class SourceLinkDock;

    Ui::SourceLinkConnectionWidget *ui;

    StageSource source;

    SourceLinkApiClient *apiClient;
    EgressLinkOutput *output;
    OutputDialog *outputDialog;

    static void onOBSSourcesChanged(void *data, calldata_t *cd);

private slots:
    void onSettingsButtonClick();
    void onVideoSourceChanged(int index);
    void onOutputStatusChanged(EgressLinkOutputStatus status);
    void updateSourceList();
    void onVisibilityChanged(bool value);

public:
    explicit SourceLinkConnectionWidget(
        const StageSource &_source, SourceLinkApiClient *_apiClient, QWidget *parent = nullptr
    );
    ~SourceLinkConnectionWidget();

    void setSource(const StageSource &_source);
};