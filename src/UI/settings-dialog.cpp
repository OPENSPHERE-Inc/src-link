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

    connect(ui->authButton, &QPushButton::clicked, this, [this]() {
        auth.login(this);
        QObject::connect(&auth, SIGNAL(linkingSucceeded()), this, SLOT(onLinkingSucceeded()));
    });

    connect(ui->revokeButton, &QPushButton::clicked, this, [this]() { auth.logout(); });

    QObject::connect(&auth, SIGNAL(accountInfoReceived()), this, SLOT(onAccountInfoReceived()));
}

SettingsDialog::~SettingsDialog() {}

void SettingsDialog::onLinkingSucceeded()
{
    auth.getAccountInfo();
}

void SettingsDialog::onAccountInfoReceived()
{
    obs_log(LOG_DEBUG, "Account info received");
}