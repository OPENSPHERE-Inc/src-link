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

#pragma once

#include <obs-module.h>

#include <QFrame>
#include <QGraphicsScene>

#include "../api-client.hpp"
#include "output-dialog.hpp"
#include "ui_egress-link-dock.h"
#include "redeem-invite-code-dialog.hpp"

class EgressLinkConnectionWidget;

class EgressLinkDock : public QFrame {
    Q_OBJECT

    Ui::EgressLinkDock *ui;

    SRCLinkApiClient *apiClient;
    QImage defaultAccountPicture;
    QImage defaultStagePicture;
    QList<EgressLinkConnectionWidget *> connectionWidgets;
    RedeemInviteCodeDialog *redeemInviteCodeDialog;

    void updateConnections(const Stage &stage);
    void clearConnections();
    void setClientActive(bool active);
    void updateGuidance();

private slots:
    void onAccountInfoReady(const AccountInfo &accountInfo);
    void onParticipantsReady(const PartyEventParticipantArray &participants);
    void onActiveParticipantChanged(int index);
    void onPictureReady(const QString &pictureId, const QImage &picture);
    void onPictureFailed(const QString &pictureId);
    void onUplinkReady(const UplinkInfo &uplink);
    void onUplinkFailed(const QString &uuid);
    void onInterlockTypeChanged(int index);
    void onConnectionButtonClicked();
    void onLogoutSucceeded();
    void onControlPanelButtonClicked();
    void onMembershipsButtonClicked();
    void onSignupButtonClicked();
    void onRedeemInviteCodeAccepted(const QString &inviteCode);

public:
    explicit EgressLinkDock(SRCLinkApiClient *_apiClient, QWidget *parent = nullptr);
    ~EgressLinkDock();
};
