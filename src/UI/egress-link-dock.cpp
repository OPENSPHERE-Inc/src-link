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

#include <obs-module.h>
#include <qt-wrappers.hpp>

#include <QmessageBox>

#include "../outputs/egress-link-output.hpp"
#include "egress-link-dock.hpp"
#include "egress-link-connection-widget.hpp"

//--- SouceLinkDock class ---//

EgressLinkDock::EgressLinkDock(SRLinkApiClient *_apiClient, QWidget *parent)
    : QFrame(parent),
      ui(new Ui::EgressLinkDock),
      apiClient(_apiClient),
      defaultAccountPicture(":/src-link/images/unknownman.png"),
      defaultStagePicture(":/src-link/images/unknownstage.png")
{
    ui->setupUi(this);

    ui->accountPictureLabel->setPixmap(QPixmap::fromImage(defaultAccountPicture));
    ui->participantPictureLabel->setPixmap(QPixmap::fromImage(defaultStagePicture));

    ui->interlockTypeComboBox->addItem(QTStr("Streaming"), "streaming");
    ui->interlockTypeComboBox->addItem(QTStr("Recording"), "recording");
    ui->interlockTypeComboBox->addItem(QTStr("StreamingOrRecording"), "streaming_recording");
    ui->interlockTypeComboBox->addItem(QTStr("VirtualCam"), "virtual_cam");
    ui->interlockTypeComboBox->addItem(QTStr("AlwaysON"), "always_on");

    ui->interlockTypeComboBox->setCurrentIndex(
        ui->interlockTypeComboBox->findData(apiClient->getSettings()->value("interlock_type", "streaming"))
    );

    connect(
        apiClient, SIGNAL(accountInfoReady(const AccountInfo &)), this, SLOT(onAccountInfoReady(const AccountInfo &))
    );
    connect(
        apiClient, SIGNAL(participantsReady(const PartyEventParticipantArray &)), this,
        SLOT(onParticipantsReady(const PartyEventParticipantArray &))
    );
    connect(
        apiClient, SIGNAL(getPictureSucceeded(const QString &, const QImage &)), this,
        SLOT(onPictureReady(const QString &, const QImage &))
    );
    connect(apiClient, SIGNAL(getPictureFailed(const QString &)), this, SLOT(onPictureFailed(const QString &)));
    connect(apiClient, SIGNAL(uplinkReady(const UplinkInfo &)), this, SLOT(onUplinkReady(const UplinkInfo &)));
    connect(apiClient, SIGNAL(uplinkFailed(const QString &)), this, SLOT(onUplinkFailed(const QString &)));

    connect(ui->participantComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onActiveParticipantChanged(int)));
    connect(ui->interlockTypeComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onInterlockTypeChanged(int)));
    connect(ui->logoutButton, SIGNAL(clicked()), this, SLOT(onLogoutButtonClicked()));

    if (!apiClient->getAccountInfo().isEmpty()) {
        onAccountInfoReady(apiClient->getAccountInfo());
    }
    if (!apiClient->getParticipants().isEmpty()) {
        onParticipantsReady(apiClient->getParticipants());
    }

    obs_log(LOG_DEBUG, "EgressLinkDock created");
}

EgressLinkDock::~EgressLinkDock()
{
    disconnect(this);

    obs_log(LOG_DEBUG, "EgressLinkDock destroyed");
}

void EgressLinkDock::onAccountInfoReady(const AccountInfo &accountInfo)
{
    auto account = accountInfo.getAccount();
    ui->accountNameLabel->setText(account.getDisplayName());
    ui->accountPictureLabel->setProperty("pictureId", account.getPictureId());

    if (!account.getPictureId().isEmpty()) {
        apiClient->getPicture(account.getPictureId());
    } else {
        ui->accountPictureLabel->setPixmap(QPixmap::fromImage(defaultAccountPicture));
    }
}

void EgressLinkDock::onParticipantsReady(const PartyEventParticipantArray &participants)
{
    auto prev = ui->participantComboBox->currentData().toString();
    auto selected = prev;

    ui->participantComboBox->blockSignals(true);
    {
        ui->participantComboBox->clear();

        // Display stage's names instead of party event
        foreach (const auto &participant, participants.values()) {
            ui->participantComboBox->addItem(participant.getStageView().getName(), participant.getId());
        }

        // Restore selection (or apply default)
        if (selected.isEmpty()) {
            selected = apiClient->getSettings()->getParticipantId();
        }
        if (!selected.isEmpty()) {
            ui->participantComboBox->setCurrentIndex(std::max(ui->participantComboBox->findData(selected), 0));
        } else {
            ui->participantComboBox->setCurrentIndex(0);
        }

        // Reset picture for no party events
        if (!participants.size()) {
            ui->participantPictureLabel->setProperty("pictureId", "");
            ui->participantPictureLabel->setPixmap(QPixmap::fromImage(defaultStagePicture));
        }
    }
    ui->participantComboBox->blockSignals(false);

    if (prev != selected) {
        onActiveParticipantChanged(ui->participantComboBox->currentIndex());
    }
}

void EgressLinkDock::onActiveParticipantChanged(int index)
{
    auto participantId = ui->participantComboBox->currentData().toString();
    auto participant = apiClient->getParticipants().find([participantId](const PartyEventParticipant &participant) {
        return participant.getId() == participantId;
    });

    if (!participant.isEmpty()) {
        auto stage = participant.getStageView();
        if (!stage.getPictureId().isEmpty()) {
            // Apply stage picture
            ui->participantPictureLabel->setProperty("pictureId", stage.getPictureId());
            apiClient->getPicture(stage.getPictureId());
        } else {
            // Apply default picture
            ui->participantPictureLabel->setProperty("pictureId", "");
            ui->participantPictureLabel->setPixmap(QPixmap::fromImage(defaultStagePicture));
        }
    }

    if (apiClient->getSettings()->getParticipantId() != participantId) {
        apiClient->getSettings()->setParticipantId(participantId);
        apiClient->putUplink();
    }
}

void EgressLinkDock::onPictureReady(const QString &pictureId, const QImage &picture)
{
    if (pictureId == ui->participantPictureLabel->property("pictureId").toString()) {
        // Update party event picture received image
        ui->participantPictureLabel->setPixmap(QPixmap::fromImage(picture));
    } else if (pictureId == ui->accountPictureLabel->property("pictureId").toString()) {
        // Update account picture with received image
        ui->accountPictureLabel->setPixmap(QPixmap::fromImage(picture));
    }
}

void EgressLinkDock::onPictureFailed(const QString &pictureId)
{
    if (pictureId == ui->participantPictureLabel->property("pictureId").toString()) {
        // Reset party event picture to default
        ui->participantPictureLabel->setPixmap(QPixmap::fromImage(defaultStagePicture));
    } else if (pictureId == ui->accountPictureLabel->property("pictureId").toString()) {
        // Reset account picture to default
        ui->accountPictureLabel->setPixmap(QPixmap::fromImage(defaultAccountPicture));
    }
}

void EgressLinkDock::onUplinkReady(const UplinkInfo &uplink)
{
    // Always show outputs
    updateConnections(uplink.getStage());

    if (!uplink.getAllocation().isEmpty()) {
        ui->seatAllocationStatus->setText(QTStr("Ready"));
        setThemeID(ui->seatAllocationStatus, "good");
    } else {
        ui->seatAllocationStatus->setText(QTStr("NoSlot"));
        setThemeID(ui->seatAllocationStatus, "error");
    }
}

void EgressLinkDock::onUplinkFailed(const QString &)
{
    ui->seatAllocationStatus->setText(QTStr("NoSlot"));
    setThemeID(ui->seatAllocationStatus, "error");

    qDeleteAll(connectionWidgets);
    connectionWidgets.clear();
}

void EgressLinkDock::updateConnections(const Stage &stage)
{
    // Update connection widgets
    foreach (const auto widget, connectionWidgets) {
        auto found = false;
        foreach (const auto &source, stage.getSources().values()) {
            if (widget->source.getName() == source.getName()) {
                found = true;
                break;
            }
        }
        if (!found) {
            ui->connectionsLayout->removeWidget(widget);
            widget->deleteLater();
            connectionWidgets.removeOne(widget);
        }
    }

    foreach (const auto &source, stage.getSources().values()) {
        auto newcommer = true;
        foreach (const auto widget, connectionWidgets) {
            if (widget->source.getName() == source.getName()) {
                newcommer = false;
                break;
            }
        }
        if (newcommer) {
            auto interlockType = ui->interlockTypeComboBox->currentData().toString();
            auto widget = new EgressLinkConnectionWidget(source, interlockType, apiClient, this);
            connectionWidgets.append(widget);
            ui->connectionsLayout->addWidget(widget);
        }
    }
}

void EgressLinkDock::onInterlockTypeChanged(int)
{
    auto interlockType = ui->interlockTypeComboBox->currentData().toString();
    apiClient->getSettings()->setValue("interlock_type", interlockType);
}

void EgressLinkDock::onLogoutButtonClicked()
{
    int ret = QMessageBox::warning(
        // "Are you sure you want to logout?"
        this, QTStr("Logout"), QTStr("LogoutConfirmation"), QMessageBox::Yes | QMessageBox::Cancel
    );

    if (ret == QMessageBox::Yes) {
        apiClient->logout();
    }
}
