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

#include <QMessageBox>

#include "../outputs/egress-link-output.hpp"
#include "egress-link-dock.hpp"
#include "egress-link-connection-widget.hpp"

//--- SouceLinkDock class ---//

EgressLinkDock::EgressLinkDock(SRCLinkApiClient *_apiClient, QWidget *parent)
    : QFrame(parent),
      ui(new Ui::EgressLinkDock),
      apiClient(_apiClient),
      defaultAccountPicture(":/src-link/images/unknownman.png"),
      defaultStagePicture(":/src-link/images/unknownstage.png"),
      errorText("")
{
    ui->setupUi(this);

    redeemInviteCodeDialog = new RedeemInviteCodeDialog(this);

    ui->accountPictureLabel->setPixmap(QPixmap::fromImage(defaultAccountPicture));
    ui->participantPictureLabel->setPixmap(QPixmap::fromImage(defaultStagePicture));

    ui->interlockTypeComboBox->addItem(QTStr("Streaming"), "streaming");
    ui->interlockTypeComboBox->addItem(QTStr("Recording"), "recording");
    ui->interlockTypeComboBox->addItem(QTStr("StreamingOrRecording"), "streaming_recording");
    ui->interlockTypeComboBox->addItem(QTStr("VirtualCam"), "virtual_cam");
    ui->interlockTypeComboBox->addItem(QTStr("AlwaysON"), "always_on");

    ui->interlockTypeComboBox->setCurrentIndex(
        ui->interlockTypeComboBox->findData(apiClient->getSettings()->value("interlock_type", DEFAULT_INTERLOCK_TYPE))
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
    connect(apiClient, SIGNAL(logoutSucceeded()), this, SLOT(onLogoutSucceeded()));
    connect(
        apiClient, SIGNAL(putUplinkFailed(const QString &, QNetworkReply::NetworkError)), this,
        SLOT(onPutUplinkFailed(const QString &, QNetworkReply::NetworkError))
    );

    connect(ui->participantComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onActiveParticipantChanged(int)));
    connect(ui->interlockTypeComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onInterlockTypeChanged(int)));
    connect(ui->connectionButton, SIGNAL(clicked()), this, SLOT(onConnectionButtonClicked()));
    connect(ui->controlPanelButton, SIGNAL(clicked()), this, SLOT(onControlPanelButtonClicked()));
    connect(ui->membershipsButton, SIGNAL(clicked()), this, SLOT(onMembershipsButtonClicked()));
    connect(ui->signupButton, SIGNAL(clicked()), this, SLOT(onSignupButtonClicked()));
    connect(ui->redeemInviteCodeButton, &QPushButton::clicked, this, [&]() { redeemInviteCodeDialog->show(); });

    connect(
        redeemInviteCodeDialog, SIGNAL(accepted(const QString &)), this,
        SLOT(onRedeemInviteCodeAccepted(const QString &))
    );

    setClientActive(apiClient->isLoggedIn());
    if (!apiClient->getAccountInfo().isEmpty()) {
        onAccountInfoReady(apiClient->getAccountInfo());
    }
    if (!apiClient->getParticipants().isEmpty()) {
        onParticipantsReady(apiClient->getParticipants());
    }

    // Translations
    ui->egressLinkLabel->setText(QTStr("Uplink"));
    ui->interlockTypeLabel->setText(QTStr("Interlock"));
    ui->participantComboBox->setPlaceholderText(QTStr("NoReceiver"));
    ui->controlPanelButton->setText(QTStr("SRCLinkControlPanel"));
    ui->membershipsButton->setText(QTStr("Manage"));
    ui->signupButton->setText(QTStr("SignupSRCLinkControlPanel"));
    ui->redeemInviteCodeButton->setText(QTStr("RedeemInvitationCode"));

    obs_log(LOG_DEBUG, "EgressLinkDock created");
}

EgressLinkDock::~EgressLinkDock()
{
    disconnect(this);

    obs_log(LOG_DEBUG, "EgressLinkDock destroyed");
}

void EgressLinkDock::setClientActive(bool active)
{
    if (!active) {
        ui->connectionButton->setText(QTStr("Login"));
        ui->accountNameLabel->setText(QTStr("NotLoggedInYet"));
        ui->uplinkWidget->setVisible(false);
        ui->signupWidget->setVisible(true);
        ui->guidanceWidget->setVisible(false);
        ui->participantComboBox->clear();
        ui->redeemInviteCodeWidget->setVisible(false);
        clearConnections();
    } else {
        ui->connectionButton->setText(QTStr("Logout"));
        ui->uplinkWidget->setVisible(true);
        ui->signupWidget->setVisible(false);
        ui->guidanceWidget->setVisible(true);
        updateGuidance();
    }
}

void EgressLinkDock::updateGuidance()
{
    if (!errorText.isEmpty()) {
        ui->guidanceLabel->setText(errorText);
        ui->redeemInviteCodeWidget->setVisible(false);
        setThemeID(ui->guidanceLabel, "error", "text-danger");
    } else if (apiClient->getUplink().getStage().isEmpty()) {
        ui->guidanceLabel->setText(QTStr("Guidance.SelectReceiver"));
        ui->redeemInviteCodeWidget->setVisible(true);
        setThemeID(ui->guidanceLabel, "", "");
    } else {
        auto interlockType = ui->interlockTypeComboBox->currentData().toString();
        ui->guidanceLabel->setText(QTStr(qUtf8Printable(QString("Guidance.%1").arg(interlockType))));
        ui->redeemInviteCodeWidget->setVisible(false);
        setThemeID(ui->guidanceLabel, "", "");
    }
}

void EgressLinkDock::onAccountInfoReady(const AccountInfo &accountInfo)
{
    setClientActive(true);

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
        if (participants.size()) {
            ui->participantComboBox->addItem("", PARTICIPANT_SEELCTION_NONE); // No selection (id == "none")
            foreach (const auto &participant, participants.values()) {
                ui->participantComboBox->addItem(
                    participant.getOwnerAccountView().isEmpty()
                        ? participant.getStageView().getName()
                        : QString("%1 (%2)")
                              .arg(participant.getStageView().getName())
                              .arg(participant.getOwnerAccountView().getDisplayName()),
                    participant.getId()
                );
            }
        }

        // Restore selection (or apply default)
        if (selected.isEmpty() || selected == PARTICIPANT_SEELCTION_NONE) {
            // Returns empty string if not selected
            selected = apiClient->getSettings()->getParticipantId();
        }
        if (!selected.isEmpty() && selected != PARTICIPANT_SEELCTION_NONE) {
            ui->participantComboBox->setCurrentIndex(std::max(ui->participantComboBox->findData(selected), 0));
        } else {
            ui->participantComboBox->setCurrentIndex(0);
        }
        // Ensure selected with current data
        selected = ui->participantComboBox->currentData().toString();

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

void EgressLinkDock::onActiveParticipantChanged(int)
{
    auto participantId = ui->participantComboBox->currentData().toString();
    auto participant = apiClient->getParticipants().find([participantId](const PartyEventParticipant &_participant) {
        return _participant.getId() == participantId;
    });

    // Apply default picture first
    ui->participantPictureLabel->setProperty("pictureId", "");
    ui->participantPictureLabel->setPixmap(QPixmap::fromImage(defaultStagePicture));

    if (!participant.isEmpty()) {
        auto stage = participant.getStageView();
        if (!stage.getPictureId().isEmpty()) {
            // Override with stage picture
            ui->participantPictureLabel->setProperty("pictureId", stage.getPictureId());
            apiClient->getPicture(stage.getPictureId());
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
        ui->seatAllocationSeatName->setText(uplink.getAllocation().getSeatName());
        ui->seatAllocationStatus->setText(QTStr("Ready"));
        setThemeID(ui->seatAllocationSeatName, "good", "text-success");
        setThemeID(ui->seatAllocationStatus, "good", "text-success");
    } else if (!uplink.getStage().isEmpty()) {
        ui->seatAllocationSeatName->setText(QTStr("NoSlot"));
        ui->seatAllocationStatus->setText(QTStr("Ready"));
        setThemeID(ui->seatAllocationSeatName, "error", "text-danger");
        setThemeID(ui->seatAllocationStatus, "good", "text-success");
    } else {
        ui->seatAllocationSeatName->setText("");
        ui->seatAllocationStatus->setText(QTStr("NotReady"));
        setThemeID(ui->seatAllocationStatus, "error", "text-danger");
    }

    errorText = "";
    updateGuidance();
}

void EgressLinkDock::onUplinkFailed(const QString &)
{
    ui->seatAllocationSeatName->setText("");
    ui->seatAllocationStatus->setText(QTStr("Error"));
    setThemeID(ui->seatAllocationStatus, "error", "text-danger");

    qDeleteAll(connectionWidgets);
    connectionWidgets.clear();

    updateGuidance();
}

void EgressLinkDock::onPutUplinkFailed(const QString &, QNetworkReply::NetworkError error)
{
    if (error == QNetworkReply::ContentConflictError) {
        errorText = QTStr("UuidConflictErrorDueToSecurity");
    } else {
        errorText = QTStr("PutUplinkFailed");
    }
    updateGuidance();
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
            auto widget = new EgressLinkConnectionWidget(source, apiClient, this);
            connectionWidgets.append(widget);
            ui->connectionsLayout->addWidget(widget);
        }
    }
}

void EgressLinkDock::clearConnections()
{
    foreach (const auto widget, connectionWidgets) {
        ui->connectionsLayout->removeWidget(widget);
        widget->deleteLater();
        connectionWidgets.removeOne(widget);
    }
}

void EgressLinkDock::onInterlockTypeChanged(int)
{
    auto interlockType = ui->interlockTypeComboBox->currentData().toString();
    apiClient->getSettings()->setValue("interlock_type", interlockType);

    updateGuidance();
}

void EgressLinkDock::onConnectionButtonClicked()
{
    if (!apiClient->isLoggedIn()) {
        apiClient->login();
    } else {
        int ret = QMessageBox::warning(
            // "Are you sure you want to logout?"
            this, QTStr("Logout"), QTStr("LogoutConfirmation"), QMessageBox::Yes | QMessageBox::Cancel
        );

        if (ret == QMessageBox::Yes) {
            apiClient->logout();
        }
    }
}

void EgressLinkDock::onLogoutSucceeded()
{
    setClientActive(false);
    errorText = "";
}

void EgressLinkDock::onControlPanelButtonClicked()
{
    apiClient->openControlPanelPage();
}

void EgressLinkDock::onMembershipsButtonClicked()
{
    apiClient->openMembershipsPage();
}

void EgressLinkDock::onSignupButtonClicked()
{
    apiClient->openSignupPage();
}

void EgressLinkDock::onRedeemInviteCodeAccepted(const QString &inviteCode)
{
    connect(
        apiClient->redeemInviteCode(inviteCode), &RequestInvoker::finished, this,
        [this](QNetworkReply::NetworkError error) {
            if (error != QNetworkReply::NoError) {
                QMessageBox::warning(this, QTStr("RedeemInvitationCode"), QTStr("RedeemInvitationCodeFailed"));
                return;
            }
        }
    );
}
