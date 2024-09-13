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
        apiClient, SIGNAL(accountInfoReady(const AccountInfo *)), this, SLOT(onAccountInfoReady(const AccountInfo *))
    );
    connect(apiClient, SIGNAL(partiesReady(QList<Party *>)), this, SLOT(onPartiesReady(QList<Party *>)));
    connect(
        apiClient, SIGNAL(partyEventsReady(QList<PartyEvent *>)), this, SLOT(onPartyEventsReady(QList<PartyEvent *>))
    );
    connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(onAccept()));
    connect(ui->activePartyComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onActivePartyChanged(int)));

    setClientActive(apiClient->isLoggedIn());
    onAccountInfoReady(apiClient->getAccountInfo());
    onPartiesReady(apiClient->getParties());
    onPartyEventsReady(apiClient->getPartyEvents());

    obs_log(LOG_DEBUG, "SettingsDialog created");
}

SettingsDialog::~SettingsDialog()
{
    obs_log(LOG_DEBUG, "SettingsDialog destroyed");
}

void SettingsDialog::setClientActive(bool active)
{
    if (!active) {
        ui->connectButton->setText(QTStr("Connect"));
        ui->accountName->setText(QTStr("Disconnected"));
        connect(ui->connectButton, SIGNAL(clicked()), this, SLOT(onConnect()));
        disconnect(ui->connectButton, SIGNAL(clicked()), this, SLOT(onDisconnect()));
    } else {
        ui->connectButton->setText(QTStr("Disconnect"));
        connect(ui->connectButton, SIGNAL(clicked()), this, SLOT(onDisconnect()));
        disconnect(ui->connectButton, SIGNAL(clicked()), this, SLOT(onConnect()));
    }
}

void SettingsDialog::onConnect()
{
    ui->connectButton->setEnabled(false);

    connect(apiClient, SIGNAL(linkingFailed()), this, SLOT(onLinkingFailed()));

    apiClient->login();
}

void SettingsDialog::onDisconnect()
{
    apiClient->logout();

    ui->activePartyComboBox->clear();
    ui->activePartyEventComboBox->clear();
    ui->connectButton->setEnabled(true);

    setClientActive(false);
}

void SettingsDialog::onAccept()
{
    saveSettings();
}

void SettingsDialog::onLinkingFailed()
{
    ui->connectButton->setEnabled(true);
    setClientActive(false);
}

void SettingsDialog::onAccountInfoReady(const AccountInfo *accountInfo)
{
    if (!accountInfo) {
        return;
    }

    setClientActive(true);
    ui->accountName->setText(QString("Connected: %1").arg(accountInfo->getDisplayName()));
    ui->connectButton->setEnabled(true);
}

void SettingsDialog::onPartiesReady(const QList<Party *> &parties)
{
    ui->activePartyComboBox->clear();

    foreach(const auto party, parties)
    {
        ui->activePartyComboBox->addItem(QString(party->getName()), party->getId());
    }

    if (!apiClient->getPartyId().isEmpty()) {
        ui->activePartyComboBox->setCurrentIndex(ui->activePartyComboBox->findData(apiClient->getPartyId()));
    } else {
        ui->activePartyComboBox->setCurrentIndex(0);
    }
}

void SettingsDialog::onPartyEventsReady(const QList<PartyEvent *> &partyEvents)
{
    ui->activePartyEventComboBox->clear();
    auto partyId = ui->activePartyComboBox->currentData().toString();

    // Fiter out party events that are not related to the selected party
    foreach(const auto partyEvent, partyEvents)
    {
        if (!partyEvent->getParty() || partyEvent->getParty()->getId() != partyId) {
            continue;
        }
        ui->activePartyEventComboBox->addItem(QString(partyEvent->getName()), partyEvent->getId());
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
    // Refresh party events combo box
    QMetaObject::invokeMethod(
        this, "onPartyEventsReady", Qt::QueuedConnection, Q_ARG(QList<PartyEvent *>, apiClient->getPartyEvents())
    );
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    apiClient->requestParties();
    apiClient->requestPartyEvents();
    setClientActive(apiClient->isLoggedIn());
}