/*
SRC-Link
Copyright (C) 2025 OPENSPHERE Inc. info@opensphere.co.jp

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
#include "../ws-portal/ws-portal-client.hpp"
#include "ui_ws-portal-dock.h"

class WsPortalDock : public QFrame {
    Q_OBJECT

    Ui::WsPortalDock *ui;

    SRCLinkApiClient *apiClient;
    WsPortalClient *wsPortalClient;
    QImage defaultAccountPicture;
    QImage defaultWsPortalPicture;

    void setClientActive(bool active);

private slots:
    void onAccountInfoReady(const AccountInfo &accountInfo);
    void onLogoutSucceeded();
    void onConnectionButtonClicked();
    void onPictureReady(const QString &pictureId, const QImage &picture);
    void onPictureFailed(const QString &pictureId);
    void onActiveWsPortalChanged(int index);
    void onWsPortalsReady(const WsPortalArray &portals);
    void onWsPortalsButtonClicked();
    void onControlPanelButtonClicked();
    void onConnected();
    void onDisconnected();
    void onReconnecting();

public:
    explicit WsPortalDock(SRCLinkApiClient *_apiClient, QWidget *parent = nullptr);
    ~WsPortalDock();
};
