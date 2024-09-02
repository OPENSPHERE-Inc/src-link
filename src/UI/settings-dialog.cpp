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

    connect(apiClient, SIGNAL(accountInfoReady(AccountInfo *)), this, SLOT(onAccountInfoReady(AccountInfo *)));
    connect(
        apiClient, SIGNAL(partyEventsReady(QList<PartyEvent *>)), this, SLOT(onPartyEventsReady(QList<PartyEvent *>))
    );

    setClientActive(apiClient->isLoggedIn());
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
        ui->accountName->setText(QString("Connected: %1").arg(apiClient->getAccountDisplayName()));
        connect(ui->connectButton, SIGNAL(clicked()), this, SLOT(onDisconnect()));
        disconnect(ui->connectButton, SIGNAL(clicked()), this, SLOT(onConnect()));
    }
}

void SettingsDialog::onConnect()
{
    ui->connectButton->setEnabled(false);

    connect(apiClient, SIGNAL(linkingSucceeded()), this, SLOT(onLinkingSucceeded()));
    connect(apiClient, SIGNAL(linkingFailed()), this, SLOT(onLinkingFailed()));

    apiClient->login();
}

void SettingsDialog::onDisconnect()
{
    apiClient->logout();

    ui->activeEventComboBox->clear();
    ui->connectButton->setEnabled(true);

    setClientActive(false);
}

void SettingsDialog::onLinkingSucceeded()
{
}

void SettingsDialog::onLinkingFailed()
{
    ui->connectButton->setEnabled(true);

    setClientActive(false);
}

void SettingsDialog::onAccountInfoReady(AccountInfo *)
{
    setClientActive(true);
}

void SettingsDialog::onPartyEventsReady(QList<PartyEvent *> events)
{
    ui->activeEventComboBox->clear();

    foreach(const auto partyEvent, events)
    {
        if (!partyEvent->getParty()) {
            continue;
        }
        ui->activeEventComboBox->addItem(
            QString("%1 - %2").arg(partyEvent->getParty()->getName()).arg(partyEvent->getName())
        );
    }
}
