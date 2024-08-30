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
#include "settings-dialog.hpp"
#include "../plugin-support.h"

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent), ui(new Ui::SettingsDialog), auth(this)
{
    ui->setupUi(this);

    connect(ui->authButton, SIGNAL(clicked()), this, SLOT(onAuthButtonClicked()));
    connect(&auth, SIGNAL(accountInfoReady(AccountInfo *)), this, SLOT(onAccountInfoReady(AccountInfo *)));
    connect(&auth, SIGNAL(partyEventsReady(QList<PartyEvent *>)), this, SLOT(onPartyEventsReady(QList<PartyEvent *>)));
}

SettingsDialog::~SettingsDialog() {}

void SettingsDialog::onAuthButtonClicked()
{
    ui->authButton->setEnabled(false);

    connect(&auth, SIGNAL(linkingSucceeded()), this, SLOT(onLinkingSucceeded()));
    connect(&auth, SIGNAL(linkingFailed()), this, SLOT(onLinkingFailed()));

    auth.login(this);
}

void SettingsDialog::onRevokeButtonClicked()
{
    auth.logout();

    ui->accountName->setText("Not logged in yet");
    ui->authButton->setText("Connect");
    ui->authButton->setEnabled(true);
}

void SettingsDialog::onLinkingSucceeded()
{
    auth.getAccountInfo();
    auth.getPartyEvents();
}

void SettingsDialog::onLinkingFailed()
{
    ui->authButton->setEnabled(true);
}

void SettingsDialog::onAccountInfoReady(AccountInfo* accountInfo)
{
    ui->accountName->setText(accountInfo->getDisplayName());

    disconnect(ui->authButton, SIGNAL(clicked()), this, SLOT(onAuthButtonClicked()));
    connect(ui->authButton, SIGNAL(clicked()), this, SLOT(onRevokeButtonClicked()));

    ui->authButton->setText("Disconnect");
    ui->authButton->setEnabled(true);
}


void SettingsDialog::onPartyEventsReady(QList<PartyEvent *> events)
{
    ui->activeEventComboBox->clear();

    foreach (const auto partyEvent, events) {
        if (!partyEvent->getParty()) {
            continue;
        }
        ui->activeEventComboBox->addItem(
            QString("%1: %2")
                .arg(partyEvent->getParty()->getName())
                .arg(partyEvent->getName())
        );
    }
}
