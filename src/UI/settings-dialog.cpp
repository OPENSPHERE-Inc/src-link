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

#include <QUrl>
#include <QDesktopServices>
#include <QGraphicsPixmapItem>
#include <QImageReader>
#include <QMessageBox>
#include <QNetworkInterface>

#include "../utils.hpp"
#include "../plugin-support.h"
#include "settings-dialog.hpp"

SettingsDialog::SettingsDialog(SRCLinkApiClient *_apiClient, QWidget *parent)
    : QDialog(parent),
      ui(new Ui::SettingsDialog),
      apiClient(_apiClient)
{
    ui->setupUi(this);

    ui->protocolComboBox->addItem(QTStr("SRT"), "srt");

    ui->pbkeylenComboBox->addItem("16", 16);
    ui->pbkeylenComboBox->addItem("24", 24);
    ui->pbkeylenComboBox->addItem("32", 32);

    ui->ssIntervalComboBox->addItem(QTStr("5secs"), 5);
    ui->ssIntervalComboBox->addItem(QTStr("10secs"), 10);
    ui->ssIntervalComboBox->addItem(QTStr("15secs"), 15);
    ui->ssIntervalComboBox->addItem(QTStr("30secs"), 30);
    ui->ssIntervalComboBox->addItem(QTStr("60secs"), 60);

    auto addresses = getPrivateIPv4Addresses();
    foreach (auto &address, addresses) {
        ui->privateIpComboBox->addItem(address, address);
    }

    connect(
        apiClient, SIGNAL(accountInfoReady(const AccountInfo &)), this, SLOT(onAccountInfoReady(const AccountInfo &))
    );
    connect(ui->connectionButton, SIGNAL(clicked()), this, SLOT(onConnectionButtonClick()));
    connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(onAccept()));
    connect(ui->advancedSettingsCheckBox, &QCheckBox::toggled, [this](bool checked) {
        ui->reconnectDelayTimeWidget->setVisible(checked);
        ui->networkBufferWidget->setVisible(checked);
        ui->protocolWidget->setVisible(checked);
        ui->pbkeylenWidget->setVisible(checked);
    });

    loadSettings();

    setClientActive(apiClient->isLoggedIn());
    onAccountInfoReady(apiClient->getAccountInfo());

    // Translations
    ui->forceConnectionCheckBox->setText(QTStr("ForceDisconnectOtherClients"));
    ui->ingressLinkSettingsLabel->setText(QTStr("DownlinkSettings"));
    ui->advancedSettingsCheckBox->setText(QTStr("AdvancedSettings"));
    ui->portRangeLabel->setText(QTStr("UDPListenPortRange"));
    ui->portRangeNoteLabel->setText(QTStr("UDPListenPortRangeNote"));
    ui->reconnectDelayTimeLabel->setText(QTStr("ReconnectDelayTime"));
    ui->reconnectDelayTimeSpinBox->setSuffix(QTStr("Secs"));
    ui->networkBufferLabel->setText(QTStr("NetworkBuffer"));
    ui->networkBufferSpinBox->setSuffix(QTStr("MB"));
    ui->protocolLabel->setText(QTStr("Protocol"));
    ui->latencyLabel->setText(QTStr("Latency"));
    ui->latencySpinBox->setSuffix(QTStr("ms"));
    ui->pbkeylenLabel->setText(QTStr("PBKeyLen"));
    ui->egressLinkSettingsLabel->setText(QTStr("UplinkSettings"));
    ui->ssIntervalLabel->setText(QTStr("ScreenshotInterval"));
    ui->privateIpLabel->setText(QTStr("PrivateIPForLAN"));

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
        ui->accountName->setText(QTStr("NotLoggedInYet"));
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
            // "Are you sure you want to logout?"
            this, QTStr("Logout"), QTStr("LogoutConfirmation"), QMessageBox::Yes | QMessageBox::Cancel
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
    // "Logged in: %1"
    ui->accountName->setText(QTStr("LoggedInAccount").arg(accountInfo.getAccount().getDisplayName()));
}

void SettingsDialog::saveSettings()
{
    auto ingressPortMin = ui->portMinSpinBox->value();
    auto ingressPortMax = ui->portMaxSpinBox->value();
    auto ingressReconnectDelayTime = ui->reconnectDelayTimeSpinBox->value();
    auto ingressNetworkBuffer = ui->networkBufferSpinBox->value();
    auto ingressProtocol = ui->protocolComboBox->currentData().toString();
    auto ingressSrtLatecy = ui->latencySpinBox->value();
    auto ingressSrtPbkeylen = ui->pbkeylenComboBox->currentData().toInt();
    auto ingressPrivateIp = ui->privateIpComboBox->currentData().toString();
    auto ingressRefreshNeeded = ingressPortMin != apiClient->getSettings()->getIngressPortMin() ||
                                ingressPortMax != apiClient->getSettings()->getIngressPortMax() ||
                                ingressReconnectDelayTime != apiClient->getSettings()->getIngressReconnectDelayTime() ||
                                ingressNetworkBuffer != apiClient->getSettings()->getIngressNetworkBufferSize() ||
                                ingressProtocol != apiClient->getSettings()->getIngressProtocol() ||
                                ingressSrtLatecy != apiClient->getSettings()->getIngressSrtLatency() ||
                                ingressSrtPbkeylen != apiClient->getSettings()->getIngressSrtPbkeylen() ||
                                ingressPrivateIp != apiClient->getSettings()->getIngressPrivateIpValue();

    auto egressScreenshotInterval = ui->ssIntervalComboBox->currentData().toInt();
    auto egressRefreshNeeded = egressScreenshotInterval != apiClient->getSettings()->getEgressScreenshotInterval();

    auto settings = apiClient->getSettings();
    settings->setForceConnection(ui->forceConnectionCheckBox->isChecked());
    settings->setIngressPortMin(ingressPortMin);
    settings->setIngressPortMax(ingressPortMax);
    settings->setIngressReconnectDelayTime(ingressReconnectDelayTime);
    settings->setIngressNetworkBufferSize(ingressNetworkBuffer);
    settings->setIngressProtocol(ingressProtocol);
    settings->setIngressSrtLatency(ingressSrtLatecy);
    settings->setIngressSrtPbkeylen(ingressSrtPbkeylen);
    settings->setIngressAdvancedSettings(ui->advancedSettingsCheckBox->isChecked());
    settings->setIngressPrivateIpIndex(ui->privateIpComboBox->currentIndex());
    settings->setIngressPrivateIpValue(ingressPrivateIp);
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
    ui->latencySpinBox->setValue(settings->getIngressSrtLatency());
    ui->pbkeylenComboBox->setCurrentIndex(ui->pbkeylenComboBox->findData(settings->getIngressSrtPbkeylen()));
    ui->advancedSettingsCheckBox->setChecked(settings->getIngressAdvancedSettings());
    ui->ssIntervalComboBox->setCurrentIndex(ui->ssIntervalComboBox->findData(settings->getEgressScreenshotInterval()));

    auto privateIpIndex = ui->privateIpComboBox->findData(settings->getIngressPrivateIpValue());
    if (privateIpIndex < 0) {
        // Private IP had been changed -> Fallback saved index.
        privateIpIndex = settings->getIngressPrivateIpIndex();
    }
    ui->privateIpComboBox->setCurrentIndex(privateIpIndex);

    bool advanced = ui->advancedSettingsCheckBox->isChecked();
    ui->reconnectDelayTimeWidget->setVisible(advanced);
    ui->networkBufferWidget->setVisible(advanced);
    ui->protocolWidget->setVisible(advanced);
    ui->pbkeylenWidget->setVisible(advanced);
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    setClientActive(apiClient->isLoggedIn());
}
