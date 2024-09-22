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
#include <QMessageBox>

#include "../utils.hpp"
#include "../plugin-support.h"
#include "settings-dialog.hpp"

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
    connect(ui->connectionButton, SIGNAL(clicked()), this, SLOT(onConnectionButtonClick()));
    connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(onAccept()));

    setClientActive(apiClient->isLoggedIn());
    onAccountInfoReady(apiClient->getAccountInfo());

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
        ui->connectionButton->setText(QTStr("Login"));
        ui->accountName->setText(QTStr("Not logged in yet"));
    } else {
        ui->connectionButton->setText(QTStr("Logout"));
    }
}

void SettingsDialog::onConnectionButtonClick()
{
    if (!apiClient->isLoggedIn()) {
        apiClient->login();
    } else {
        int ret = QMessageBox::warning(
            this, "Logout", "Are you sure you want to logout?", QMessageBox::Yes | QMessageBox::Cancel
        );
        if (ret == QMessageBox::Yes) {
            apiClient->logout();
            setClientActive(false);
        }
    }
}

void SettingsDialog::onAccept()
{
    saveSettings();
}

void SettingsDialog::onLinkingFailed()
{
    setClientActive(false);
}

void SettingsDialog::onAccountInfoReady(const AccountInfo &accountInfo)
{
    setClientActive(true);
    ui->accountName->setText(QString("Logged in: %1").arg(accountInfo.getDisplayName()));
}

void SettingsDialog::saveSettings()
{
    apiClient->setPortMin(ui->portMinSpinBox->value());
    apiClient->setPortMax(ui->portMaxSpinBox->value());
    apiClient->setForceConnection(ui->forceConnectionCheckBox->isChecked());
    apiClient->putSeatAllocation();
}

void SettingsDialog::loadSettings()
{
    ui->portMinSpinBox->setValue(apiClient->getPortMin());
    ui->portMaxSpinBox->setValue(apiClient->getPortMax());
    ui->forceConnectionCheckBox->setChecked(apiClient->getForceConnection());
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    setClientActive(apiClient->isLoggedIn());
}
