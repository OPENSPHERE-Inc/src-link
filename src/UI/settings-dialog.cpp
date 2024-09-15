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

#include <QUrl>
#include <QDesktopServices>
#include <QGraphicsPixmapItem>
#include <QImageReader>

#include "common.hpp"
#include "settings-dialog.hpp"
#include "../plugin-support.h"

SettingsDialog::SettingsDialog(SourceLinkApiClient *_apiClient, QWidget *parent)
    : QDialog(parent),
      ui(new Ui::SettingsDialog),
      apiClient(_apiClient)
{
    ui->setupUi(this);

    loadSettings();

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

    connect(ui->connectionButton, SIGNAL(clicked()), this, SLOT(onConnectionButtonClick()));
    connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(onAccept()));
    connect(ui->activePartyComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onActivePartyChanged(int)));
    connect(ui->activePartyEventComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onActivePartyEventChanged(int)));

    setClientActive(apiClient->isLoggedIn());
    onAccountInfoReady(apiClient->getAccountInfo());
    onPartiesReady(apiClient->getParties());
    onPartyEventsReady(apiClient->getPartyEvents());

    obs_log(LOG_DEBUG, "SettingsDialog created");
}

SettingsDialog::~SettingsDialog()
{
    disconnect(this);

    obs_log(LOG_DEBUG, "SettingsDialog destroyed");
}

void SettingsDialog::setClientActive(bool active)
{
    if (!active) {
        ui->connectionButton->setText(QTStr("Connect"));
        ui->accountName->setText(QTStr("Disconnected"));
    } else {
        ui->connectionButton->setText(QTStr("Disconnect"));
    }
}

void SettingsDialog::onConnectionButtonClick()
{
    ui->connectionButton->setEnabled(false);

    if (!apiClient->isLoggedIn()) {
        apiClient->login();
    } else {
        apiClient->logout();
        ui->activePartyComboBox->clear();
        ui->activePartyEventComboBox->clear();
        ui->connectionButton->setEnabled(true);
        setClientActive(false);
    }
}

void SettingsDialog::onAccept()
{
    saveSettings();
}

void SettingsDialog::onLinkingFailed()
{
    ui->connectionButton->setEnabled(true);
    setClientActive(false);
}

void SettingsDialog::onAccountInfoReady(const AccountInfo &accountInfo)
{
    setClientActive(true);
    ui->accountName->setText(QString("Connected: %1").arg(accountInfo.getDisplayName()));
    ui->connectionButton->setEnabled(true);
}

void SettingsDialog::onPartiesReady(const QList<Party> &parties)
{
    ui->activePartyComboBox->clear();

    foreach(const auto &party, parties)
    {
        ui->activePartyComboBox->addItem(party.getName(), party.getId());
    }

    if (!apiClient->getPartyId().isEmpty()) {
        ui->activePartyComboBox->setCurrentIndex(ui->activePartyComboBox->findData(apiClient->getPartyId()));
    } else {
        ui->activePartyComboBox->setCurrentIndex(0);
    }
}

void SettingsDialog::onPartyEventsReady(const QList<PartyEvent> &partyEvents)
{
    ui->activePartyEventComboBox->clear();
    auto partyId = ui->activePartyComboBox->currentData().toString();

    // Fiter out party events that are not related to the selected party
    foreach(const auto &partyEvent, partyEvents)
    {
        if (partyEvent.getParty().isEmpty() || partyEvent.getParty().getId() != partyId) {
            continue;
        }
        ui->activePartyEventComboBox->addItem(partyEvent.getName(), partyEvent.getId());
    }

    if (!apiClient->getPartyEventId().isEmpty()) {
        ui->activePartyEventComboBox->setCurrentIndex(ui->activePartyEventComboBox->findData(apiClient->getPartyEventId(
        )));
    } else {
        ui->activePartyEventComboBox->setCurrentIndex(0);
    }
}

void SettingsDialog::saveSettings()
{
    apiClient->setPortMin(ui->portMinSpinBox->value());
    apiClient->setPortMax(ui->portMaxSpinBox->value());
    apiClient->setPartyId(ui->activePartyComboBox->currentData().toString());
    apiClient->setPartyEventId(ui->activePartyEventComboBox->currentData().toString());
    apiClient->setForceConnection(ui->forceConnectionCheckBox->isChecked());
    apiClient->putSeatAllocation();
}

void SettingsDialog::loadSettings()
{
    ui->portMinSpinBox->setValue(apiClient->getPortMin());
    ui->portMaxSpinBox->setValue(apiClient->getPortMax());
    ui->activePartyComboBox->setCurrentIndex(ui->activePartyComboBox->findData(apiClient->getPartyId()));
    ui->activePartyEventComboBox->setCurrentIndex(ui->activePartyEventComboBox->findData(apiClient->getPartyEventId()));
    ui->forceConnectionCheckBox->setChecked(apiClient->getForceConnection());
}

void SettingsDialog::onActivePartyChanged(int index)
{
    auto partyId = ui->activePartyComboBox->currentData().toString();
    foreach(const auto &party, apiClient->getParties())
    {
        if (party.getId() == partyId) {
            if (!party.getPictureId().isEmpty()) {
                ui->partyPictureLabel->setProperty("pictureId", party.getPictureId());
                apiClient->getPicture(party.getPictureId());
            }
            break;
        }
    }

    // Refresh party events combo box
    QMetaObject::invokeMethod(
        this, "onPartyEventsReady", Qt::QueuedConnection, Q_ARG(QList<PartyEvent>, apiClient->getPartyEvents())
    );
}

void SettingsDialog::onActivePartyEventChanged(int index)
{
    auto partyEventId = ui->activePartyEventComboBox->currentData().toString();
    foreach(const auto &partyEvent, apiClient->getPartyEvents())
    {
        if (partyEvent.getId() == partyEventId) {
            if (!partyEvent.getPictureId().isEmpty()) {
                ui->partyEventPictureLabel->setProperty("pictureId", partyEvent.getPictureId());
                apiClient->getPicture(partyEvent.getPictureId());
            }
            break;
        }
    }
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    apiClient->requestParties();
    apiClient->requestPartyEvents();
    setClientActive(apiClient->isLoggedIn());
}

void SettingsDialog::onPictureReady(const QString &pictureId, const QImage &picture)
{
    QImage scaled;
    if (picture.width() > picture.height()) {
        scaled = picture.scaledToHeight(180);
        scaled = scaled.copy((scaled.width() - 320) / 2, 0, 320, 180);
    } else {
        scaled = picture.scaledToWidth(320);
        scaled = scaled.copy(0, (scaled.height() - 180) / 2, 320, 180);
    }

    if (pictureId == ui->partyPictureLabel->property("pictureId").toString()) {
        // Update party picture
        ui->partyPictureLabel->setPixmap(QPixmap::fromImage(scaled));
    } else if (pictureId == ui->partyEventPictureLabel->property("pictureId").toString()) {
        // Update party event picture
        ui->partyEventPictureLabel->setPixmap(QPixmap::fromImage(scaled));
    }
}