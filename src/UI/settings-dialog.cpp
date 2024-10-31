/*
SR Link
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

SettingsDialog::SettingsDialog(SRLinkApiClient *_apiClient, QWidget *parent)
    : QDialog(parent),
      ui(new Ui::SettingsDialog),
      apiClient(_apiClient)
{
    ui->setupUi(this);

    ui->protocolComboBox->addItem("SRT", "srt");

    ui->srtModeComboBox->addItem("Listaner", "listener");
    ui->srtModeComboBox->addItem("Caller", "caller");
    ui->srtModeComboBox->addItem("Rendezvous", "rendezvous");

    ui->pbkeylenComboBox->addItem("16", 16);
    ui->pbkeylenComboBox->addItem("24", 24);
    ui->pbkeylenComboBox->addItem("32", 32);

    ui->ssIntervalComboBox->addItem("5 secs", 5);
    ui->ssIntervalComboBox->addItem("10 secs", 10);
    ui->ssIntervalComboBox->addItem("15 secs", 15);
    ui->ssIntervalComboBox->addItem("30 secs", 30);
    ui->ssIntervalComboBox->addItem("1 minutes", 60);
    ui->ssIntervalComboBox->addItem("2 minutes", 120);
    ui->ssIntervalComboBox->addItem("3 minutes", 180);
    ui->ssIntervalComboBox->addItem("5 minutes", 300);

    connect(
        apiClient, SIGNAL(accountInfoReady(const AccountInfo &)), this, SLOT(onAccountInfoReady(const AccountInfo &))
    );
    connect(ui->connectionButton, SIGNAL(clicked()), this, SLOT(onConnectionButtonClick()));
    connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(onAccept()));
    connect(ui->advancedSettingsCheckBox, &QCheckBox::toggled, [this](bool checked) {
        ui->reconnectDelayTimeWidget->setVisible(checked);
        ui->networkBufferWidget->setVisible(checked);
        ui->protocolWidget->setVisible(checked);
        ui->srtModeWidget->setVisible(checked);
        ui->pbkeylenWidget->setVisible(checked);
    });

    loadSettings();

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
    auto ingressPortMin = ui->portMinSpinBox->value();
    auto ingressPortMax = ui->portMaxSpinBox->value();
    auto ingressReconnectDelayTime = ui->reconnectDelayTimeSpinBox->value();
    auto ingressNetworkBuffer = ui->networkBufferSpinBox->value();
    auto ingressProtocol = ui->protocolComboBox->currentData().toString();
    auto ingressSrtMode = ui->srtModeComboBox->currentData().toString();
    auto ingressSrtLatecy = ui->latencySpinBox->value();
    auto ingressSrtPbkeylen = ui->pbkeylenComboBox->currentData().toInt();
    auto ingressRefreshNeeded = ingressPortMin != apiClient->getSettings()->getIngressPortMin() ||
                                ingressPortMax != apiClient->getSettings()->getIngressPortMax() ||
                                ingressReconnectDelayTime != apiClient->getSettings()->getIngressReconnectDelayTime() ||
                                ingressNetworkBuffer != apiClient->getSettings()->getIngressNetworkBufferSize() ||
                                ingressProtocol != apiClient->getSettings()->getIngressProtocol() ||
                                ingressSrtMode != apiClient->getSettings()->getIngressSrtMode() ||
                                ingressSrtLatecy != apiClient->getSettings()->getIngressSrtLatency() ||
                                ingressSrtPbkeylen != apiClient->getSettings()->getIngressSrtPbkeylen();
    
    auto egressScreenshotInterval = ui->ssIntervalComboBox->currentData().toInt();
    auto egressRefreshNeeded = egressScreenshotInterval != apiClient->getSettings()->getEgressScreenshotInterval();

    auto settings = apiClient->getSettings();
    settings->setForceConnection(ui->forceConnectionCheckBox->isChecked());
    settings->setIngressPortMin(ingressPortMin);
    settings->setIngressPortMax(ingressPortMax);
    settings->setIngressReconnectDelayTime(ingressReconnectDelayTime);
    settings->setIngressNetworkBufferSize(ingressNetworkBuffer);
    settings->setIngressProtocol(ingressProtocol);
    settings->setIngressSrtMode(ingressSrtMode);
    settings->setIngressSrtLatency(ingressSrtLatecy);
    settings->setIngressSrtPbkeylen(ingressSrtPbkeylen);
    settings->setIngressAdvancedSettings(ui->advancedSettingsCheckBox->isChecked());
    settings->setEgressScreenshotInterval(egressScreenshotInterval);

    apiClient->putUplink();
    if (ingressRefreshNeeded) {
        apiClient->refreshIngress();
    }
    if (egressRefreshNeeded) {
        apiClient->refreshEgress();
    }
}

void SettingsDialog::loadSettings()
{
    auto settings = apiClient->getSettings();
    ui->forceConnectionCheckBox->setChecked(settings->getForceConnection());
    ui->portMinSpinBox->setValue(settings->getIngressPortMin());
    ui->portMaxSpinBox->setValue(settings->getIngressPortMax());
    ui->reconnectDelayTimeSpinBox->setValue(settings->getIngressReconnectDelayTime());
    ui->networkBufferSpinBox->setValue(settings->getIngressNetworkBufferSize());
    ui->protocolComboBox->setCurrentIndex(ui->protocolComboBox->findData(settings->getIngressProtocol()));
    ui->srtModeComboBox->setCurrentIndex(ui->srtModeComboBox->findData(settings->getIngressSrtMode()));
    ui->latencySpinBox->setValue(settings->getIngressSrtLatency());
    ui->pbkeylenComboBox->setCurrentIndex(ui->pbkeylenComboBox->findData(settings->getIngressSrtPbkeylen()));
    ui->advancedSettingsCheckBox->setChecked(settings->getIngressAdvancedSettings());
    ui->ssIntervalComboBox->setCurrentIndex(ui->ssIntervalComboBox->findData(settings->getEgressScreenshotInterval()));

    bool advanced = ui->advancedSettingsCheckBox->isChecked();
    ui->reconnectDelayTimeWidget->setVisible(advanced);
    ui->networkBufferWidget->setVisible(advanced);
    ui->protocolWidget->setVisible(advanced);
    ui->srtModeWidget->setVisible(advanced);
    ui->pbkeylenWidget->setVisible(advanced);
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    setClientActive(apiClient->isLoggedIn());
}
