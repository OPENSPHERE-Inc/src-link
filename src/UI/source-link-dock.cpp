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

#include <obs-module.h>
#include <qt-wrappers.hpp>

#include <QmessageBox>

#include "../outputs/egress-link-output.hpp"
#include "source-link-dock.hpp"
#include "source-link-connection-widget.hpp"

//--- SouceLinkDock class ---//

SourceLinkDock::SourceLinkDock(SourceLinkApiClient *_apiClient, QWidget *parent)
    : QFrame(parent),
      ui(new Ui::SourceLinkDock),
      apiClient(_apiClient),
      defaultAccountPicture(":/source-link/images/unknownaccount.png"),
      defaultPartyPicture(":/source-link/images/unknownparty.png"),
      defaultPartyEventPicture(":/source-link/images/unknownevent.png")
{
    ui->setupUi(this);

    ui->accountPictureLabel->setPixmap(QPixmap::fromImage(defaultAccountPicture));
    ui->partyPictureLabel->setPixmap(QPixmap::fromImage(defaultPartyPicture));
    ui->partyEventPictureLabel->setPixmap(QPixmap::fromImage(defaultPartyEventPicture));

    ui->interlockTypeComboBox->addItem("Streaming", "streaming");
    ui->interlockTypeComboBox->addItem("Recording", "recording");
    ui->interlockTypeComboBox->addItem("Streaming or Recording", "streaming_recording");
    ui->interlockTypeComboBox->addItem("Virtual Cam", "virtual_cam");
    ui->interlockTypeComboBox->addItem("Always ON", "always_on");

    ui->interlockTypeComboBox->setCurrentIndex(
        ui->interlockTypeComboBox->findData(apiClient->getSettings()->value("interlock_type", "streaming"))
    );

    connect(
        apiClient, SIGNAL(accountInfoReady(const AccountInfo &)), this, SLOT(onAccountInfoReady(const AccountInfo &))
    );
    connect(apiClient, SIGNAL(partiesReady(const QList<Party> &)), this, SLOT(onPartiesReady(const QList<Party> &)));
    connect(
        apiClient, SIGNAL(partyEventsReady(const QList<PartyEvent> &)), this,
        SLOT(onPartyEventsReady(const QList<PartyEvent> &))
    );
    connect(
        apiClient, SIGNAL(pictureGetSucceeded(const QString &, const QImage &)), this,
        SLOT(onPictureReady(const QString &, const QImage &))
    );
    connect(apiClient, SIGNAL(pictureGetFailed(const QString &)), this, SLOT(onPictureFailed(const QString &)));
    connect(
        apiClient, SIGNAL(seatAllocationReady(const StageSeatInfo &)), this,
        SLOT(onSeatAllocationReady(const StageSeatInfo &))
    );
    connect(apiClient, SIGNAL(seatAllocationFailed()), this, SLOT(onSeatAllocationFailed()));

    connect(ui->partyComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onActivePartyChanged(int)));
    connect(ui->partyEventComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onActivePartyEventChanged(int)));
    connect(ui->interlockTypeComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onInterlockTypeChanged(int)));
    connect(ui->logoutButton, SIGNAL(clicked()), this, SLOT(onLogoutButtonClicked()));

    if (!apiClient->getAccountInfo().isEmpty()) {
        onAccountInfoReady(apiClient->getAccountInfo());
    }
    if (!apiClient->getParties().isEmpty()) {
        onPartiesReady(apiClient->getParties());
    }
    if (!apiClient->getPartyEvents().isEmpty()) {
        onPartyEventsReady(apiClient->getPartyEvents());
    }

    obs_log(LOG_DEBUG, "SourceLinkDock created");
}

SourceLinkDock::~SourceLinkDock()
{
    disconnect(this);

    obs_log(LOG_DEBUG, "SourceLinkDock destroyed");
}

void SourceLinkDock::onAccountInfoReady(const AccountInfo &accountInfo)
{
    ui->accountNameLabel->setText(accountInfo.getDisplayName());
    ui->accountPictureLabel->setProperty("pictureId", accountInfo.getPictureId());

    if (!accountInfo.getPictureId().isEmpty()) {
        apiClient->getPicture(accountInfo.getPictureId());
    } else {
        ui->accountPictureLabel->setPixmap(QPixmap::fromImage(defaultAccountPicture));
    }
}

void SourceLinkDock::onPartiesReady(const QList<Party> &parties)
{
    auto selected = ui->partyComboBox->currentData().toString();
    ui->partyComboBox->clear();

    foreach (const auto &party, parties) {
        ui->partyComboBox->addItem(party.getName(), party.getId());
    }

    // Restore selection (or apply default)
    if (selected.isEmpty()) {
        selected = apiClient->getPartyId();
    }
    if (!selected.isEmpty()) {
        ui->partyComboBox->setCurrentIndex(ui->partyComboBox->findData(selected));
    } else {
        ui->partyComboBox->setCurrentIndex(0);
    }

    // Reset picture for no parties
    if (!parties.size()) {
        ui->partyPictureLabel->setProperty("pictureId", "");
        ui->partyPictureLabel->setPixmap(QPixmap::fromImage(defaultPartyPicture));
    }
}

void SourceLinkDock::onPartyEventsReady(const QList<PartyEvent> &partyEvents)
{
    auto selected = ui->partyEventComboBox->currentData().toString();
    ui->partyEventComboBox->clear();
    auto partyId = ui->partyComboBox->currentData().toString();

    // Fiter out party events that are not related to the selected party
    foreach (const auto &partyEvent, partyEvents) {
        if (partyEvent.getParty().isEmpty() || partyEvent.getParty().getId() != partyId) {
            continue;
        }
        ui->partyEventComboBox->addItem(partyEvent.getName(), partyEvent.getId());
    }

    // Restore selection (or apply default)
    if (selected.isEmpty()) {
        selected = apiClient->getPartyEventId();
    }
    if (!selected.isEmpty()) {
        ui->partyEventComboBox->setCurrentIndex(ui->partyEventComboBox->findData(selected));
    } else {
        ui->partyEventComboBox->setCurrentIndex(0);
    }

    // Reset picture for no party events
    if (!partyEvents.size()) {
        ui->partyEventPictureLabel->setProperty("pictureId", "");
        ui->partyEventPictureLabel->setPixmap(QPixmap::fromImage(defaultPartyEventPicture));
    }
}

void SourceLinkDock::onActivePartyChanged(int index)
{
    auto partyId = ui->partyComboBox->currentData().toString();
    foreach (const auto &party, apiClient->getParties()) {
        if (party.getId() == partyId) {
            if (!party.getPictureId().isEmpty()) {
                ui->partyPictureLabel->setProperty("pictureId", party.getPictureId());
                apiClient->getPicture(party.getPictureId());
            }
            break;
        }
    }

    // Refresh party events combo box
    onPartyEventsReady(apiClient->getPartyEvents());
}

void SourceLinkDock::onActivePartyEventChanged(int index)
{
    auto partyEventId = ui->partyEventComboBox->currentData().toString();
    foreach (const auto &partyEvent, apiClient->getPartyEvents()) {
        if (partyEvent.getId() == partyEventId) {
            if (!partyEvent.getPictureId().isEmpty()) {
                ui->partyEventPictureLabel->setProperty("pictureId", partyEvent.getPictureId());
                apiClient->getPicture(partyEvent.getPictureId());
            }
            break;
        }
    }

    apiClient->setPartyId(ui->partyComboBox->currentData().toString());
    apiClient->setPartyEventId(ui->partyEventComboBox->currentData().toString());
    apiClient->putSeatAllocation();
}

void SourceLinkDock::onPictureReady(const QString &pictureId, const QImage &picture)
{
    if (pictureId == ui->partyPictureLabel->property("pictureId").toString()) {
        // Update party picture with received image
        ui->partyPictureLabel->setPixmap(QPixmap::fromImage(picture));
    } else if (pictureId == ui->partyEventPictureLabel->property("pictureId").toString()) {
        // Update party event picture received image
        ui->partyEventPictureLabel->setPixmap(QPixmap::fromImage(picture));
    } else if (pictureId == ui->accountPictureLabel->property("pictureId").toString()) {
        // Update account picture with received image
        ui->accountPictureLabel->setPixmap(QPixmap::fromImage(picture));
    }
}

void SourceLinkDock::onPictureFailed(const QString &pictureId)
{
    if (pictureId == ui->partyPictureLabel->property("pictureId").toString()) {
        // Reset party picture to default
        ui->partyPictureLabel->setPixmap(QPixmap::fromImage(defaultPartyPicture));
    } else if (pictureId == ui->partyEventPictureLabel->property("pictureId").toString()) {
        // Reset party event picture to default
        ui->partyEventPictureLabel->setPixmap(QPixmap::fromImage(defaultPartyEventPicture));
    } else if (pictureId == ui->accountPictureLabel->property("pictureId").toString()) {
        // Reset account picture to default
        ui->accountPictureLabel->setPixmap(QPixmap::fromImage(defaultAccountPicture));
    }
}

void SourceLinkDock::onSeatAllocationReady(const StageSeatInfo &seat)
{
    if (!seat.getAllocation().isEmpty()) {
        ui->seatAllocationStatus->setText(QString("Ready"));
        setThemeID(ui->seatAllocationStatus, "good");
        updateConnections(seat.getStage());
    } else {
        onSeatAllocationFailed();
    }
}

void SourceLinkDock::onSeatAllocationFailed()
{
    ui->seatAllocationStatus->setText(QString("No seat"));
    setThemeID(ui->seatAllocationStatus, "error");

    qDeleteAll(connectionWidgets);
    connectionWidgets.clear();
}

void SourceLinkDock::updateConnections(const Stage &stage)
{
    // Update connection widgets
    foreach (const auto widget, connectionWidgets) {
        auto found = false;
        foreach (const auto &source, stage.getSources()) {
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

    foreach (const auto &source, stage.getSources()) {
        auto newcommer = true;
        foreach (const auto widget, connectionWidgets) {
            if (widget->source.getName() == source.getName()) {
                newcommer = false;
                break;
            }
        }
        if (newcommer) {
            auto interlockType = ui->interlockTypeComboBox->currentData().toString();
            auto widget = new SourceLinkConnectionWidget(source, interlockType, apiClient, this);
            connectionWidgets.append(widget);
            ui->connectionsLayout->addWidget(widget);
        }
    }
}

void SourceLinkDock::onInterlockTypeChanged(int)
{
    auto interlockType = ui->interlockTypeComboBox->currentData().toString();
    apiClient->getSettings()->setValue("interlock_type", interlockType);
}

void SourceLinkDock::onLogoutButtonClicked()
{
    int ret =
        QMessageBox::warning(this, "Logout", "Are you sure you want to logout?", QMessageBox::Yes | QMessageBox::Cancel);

    if (ret == QMessageBox::Yes) {
        apiClient->logout();
    }
}
