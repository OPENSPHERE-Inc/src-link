/*
SRC-Link
Copyright (C) 2025 OPENSPHERE Inc. info@opensphere.co.jp

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
#include <QApplication>
#include <QClipboard>

#include "../utils.hpp"
#include "ws-portal-dock.hpp"

//--- WsPortalDock class ---//

WsPortalDock::WsPortalDock(SRCLinkApiClient *_apiClient, QWidget *parent)
    : QFrame(parent),
      ui(new Ui::WsPortalDock),
      apiClient(_apiClient),
      defaultAccountPicture(":/src-link/images/unknownman.png"),
      defaultWsPortalPicture(":/src-link/images/unknownportal.png")
{
    ui->setupUi(this);

    wsPortalClient = new WsPortalClient(apiClient, this);

    ui->accountPictureLabel->setPixmap(QPixmap::fromImage(defaultAccountPicture));
    ui->wsPortalPictureLabel->setPixmap(QPixmap::fromImage(defaultWsPortalPicture));
    ui->connectionInfoWidget->setVisible(false);
    ui->connectionInfoTabs->setTabVisible(0, false);
    ui->connectionInfoTabs->setTabVisible(1, false);

    connect(
        apiClient, SIGNAL(accountInfoReady(const AccountInfo &)), this, SLOT(onAccountInfoReady(const AccountInfo &))
    );
    connect(apiClient, SIGNAL(logoutSucceeded()), this, SLOT(onLogoutSucceeded()));
    connect(
        apiClient, SIGNAL(wsPortalsReady(const WsPortalArray &)), this, SLOT(onWsPortalsReady(const WsPortalArray &))
    );
    connect(
        apiClient, SIGNAL(getPictureSucceeded(const QString &, const QImage &)), this,
        SLOT(onPictureReady(const QString &, const QImage &))
    );
    connect(apiClient, SIGNAL(getPictureFailed(const QString &)), this, SLOT(onPictureFailed(const QString &)));

    connect(wsPortalClient, SIGNAL(connected()), this, SLOT(onConnected()));
    connect(wsPortalClient, SIGNAL(disconnected()), this, SLOT(onDisconnected()));
    connect(wsPortalClient, SIGNAL(reconnecting()), this, SLOT(onReconnecting()));

    connect(ui->connectionButton, SIGNAL(clicked()), this, SLOT(onConnectionButtonClicked()));
    connect(ui->wsPortalComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onActiveWsPortalChanged(int)));
    connect(ui->wsPortalsButton, SIGNAL(clicked()), this, SLOT(onWsPortalsButtonClicked()));
    connect(ui->controlPanelButton, SIGNAL(clicked()), this, SLOT(onControlPanelButtonClicked()));
    connect(ui->showConnectionInfoCheckBox, &QCheckBox::toggled, this, [this](bool value) {
        ui->connectionInfoTabs->setVisible(value);
    });
    connect(ui->tlsAddressCopyButton, &QToolButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(ui->tlsAddressValueLabel->text());
    });
    connect(ui->tlsPortCopyButton, &QToolButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(ui->tlsPortValueLabel->text());
    });
    connect(ui->tlsUrlCopyButton, &QToolButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(ui->tlsUrlValueLabel->text());
    });
    connect(ui->nonTlsAddressCopyButton, &QToolButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(ui->nonTlsAddressValueLabel->text());
    });
    connect(ui->nonTlsPortCopyButton, &QToolButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(ui->nonTlsPortValueLabel->text());
    });
    connect(ui->nonTlsUrlCopyButton, &QToolButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(ui->nonTlsUrlValueLabel->text());
    });

    setClientActive(apiClient->isLoggedIn());
    if (!apiClient->getAccountInfo().isEmpty()) {
        onAccountInfoReady(apiClient->getAccountInfo());
    }
    if (!apiClient->getWsPortals().isEmpty()) {
        onWsPortalsReady(apiClient->getWsPortals());
    }

    // Translations
    ui->wsPortalLabel->setText(QTStr("OBSWebSocketPortal"));
    ui->wsPortalComboBox->setPlaceholderText(QTStr("NoPortal"));
    ui->wsPortalsButton->setText(QTStr("Manage"));
    ui->signupButton->setText(QTStr("SignupSRCLinkControlPanel"));
    ui->controlPanelButton->setText(QTStr("SRCLinkControlPanel"));
    ui->wsPortalStatus->setText(QTStr("Unlinked"));
    setThemeID(ui->wsPortalStatus, "error", "text-danger");
    ui->showConnectionInfoCheckBox->setText(QTStr("ShowConnectionInfo"));
    ui->connectionInfoTabs->setTabText(0, QTStr("TLS"));
    ui->connectionInfoTabs->setTabText(1, QTStr("NonTLS"));
    ui->tlsAddressLabel->setText(QTStr("Address"));
    ui->tlsPortLabel->setText(QTStr("Port"));
    ui->tlsUrlLabel->setText(QTStr("URL"));
    ui->nonTlsAddressLabel->setText(QTStr("Address"));
    ui->nonTlsPortLabel->setText(QTStr("Port"));
    ui->nonTlsUrlLabel->setText(QTStr("URL"));
    ui->nonTlsNoticeLabel->setText(QTStr("NonTLSNotice"));

    obs_log(LOG_DEBUG, "WsPortalDock created");
}

WsPortalDock::~WsPortalDock()
{
    disconnect(this);

    obs_log(LOG_DEBUG, "WsPortalDock destroyed");
}

void WsPortalDock::setClientActive(bool active)
{
    if (!active) {
        ui->connectionButton->setText(QTStr("Login"));
        ui->accountNameLabel->setText(QTStr("NotLoggedInYet"));
        ui->wsPortalWidget->setVisible(false);
        ui->signupWidget->setVisible(true);
        ui->wsPortalComboBox->clear();
        ui->guidanceWidget->setVisible(false);
    } else {
        ui->connectionButton->setText(QTStr("Logout"));
        ui->wsPortalWidget->setVisible(true);
        ui->signupWidget->setVisible(false);
        ui->guidanceWidget->setVisible(true);
        updateGuidance();
    }
}

void WsPortalDock::updateGuidance()
{
    if (ui->wsPortalComboBox->count() > 1) {
        auto portalId = ui->wsPortalComboBox->currentData().toString();
        if (portalId.isEmpty() || portalId == WS_PORTAL_SELECTION_NONE) {
            ui->guidanceLabel->setText(QTStr("Guidance.SelectPortal"));
        } else {
            ui->guidanceLabel->setText(QTStr("Guidance.ConnectPortal"));
        }
    } else {
        ui->guidanceLabel->setText(QTStr("Guidance.CreatePortal"));
    }
    setThemeID(ui->guidanceLabel, "", "");
}

const WsPortal WsPortalDock::getActiveWsPortal() const
{
    auto portalId = ui->wsPortalComboBox->currentData().toString();
    return apiClient->getWsPortals().find([portalId](const WsPortal &_portal) { return _portal.getId() == portalId; });
}

void WsPortalDock::updateConnectionInfo()
{
    auto portal = getActiveWsPortal();

    auto facility = portal.getFacilityView();
    if (portal.isEmpty() || facility.isEmpty()) {
        ui->connectionInfoWidget->setVisible(false);
        ui->showConnectionInfoCheckBox->setChecked(false);
        return;

    } else {
        if (portal.getFacilityView().getTlsPort()) {
            ui->connectionInfoTabs->setTabVisible(0, true);
            ui->tlsAddressValueLabel->setText(facility.getHost(fancyId(portal.getId())));
            ui->tlsPortValueLabel->setText(QString::number(facility.getTlsPort()));
            ui->tlsUrlValueLabel->setText(facility.getTlsUrl(fancyId(portal.getId())));
        }
        ui->connectionInfoTabs->setTabVisible(1, true);
        ui->nonTlsAddressValueLabel->setText(facility.getHost(fancyId(portal.getId())));
        ui->nonTlsPortValueLabel->setText(QString::number(facility.getPort()));
        ui->nonTlsUrlValueLabel->setText(facility.getNonTlsUrl(fancyId(portal.getId())));
        ui->connectionInfoTabs->setVisible(ui->showConnectionInfoCheckBox->isChecked());
        ui->connectionInfoWidget->setVisible(true);
    }
}

void WsPortalDock::onAccountInfoReady(const AccountInfo &accountInfo)
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

void WsPortalDock::onLogoutSucceeded()
{
    setClientActive(false);
}

void WsPortalDock::onConnectionButtonClicked()
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

void WsPortalDock::onPictureReady(const QString &pictureId, const QImage &picture)
{
    if (pictureId == ui->wsPortalPictureLabel->property("pictureId").toString()) {
        // Update party event picture received image
        ui->wsPortalPictureLabel->setPixmap(QPixmap::fromImage(picture));
    } else if (pictureId == ui->accountPictureLabel->property("pictureId").toString()) {
        // Update account picture with received image
        ui->accountPictureLabel->setPixmap(QPixmap::fromImage(picture));
    }
}

void WsPortalDock::onPictureFailed(const QString &pictureId)
{
    if (pictureId == ui->wsPortalPictureLabel->property("pictureId").toString()) {
        // Reset party event picture to default
        ui->wsPortalPictureLabel->setPixmap(QPixmap::fromImage(defaultWsPortalPicture));
    } else if (pictureId == ui->accountPictureLabel->property("pictureId").toString()) {
        // Reset account picture to default
        ui->accountPictureLabel->setPixmap(QPixmap::fromImage(defaultAccountPicture));
    }
}

void WsPortalDock::onActiveWsPortalChanged(int)
{
    auto portal = getActiveWsPortal();

    // Apply default picture first
    ui->wsPortalPictureLabel->setProperty("pictureId", "");
    ui->wsPortalPictureLabel->setPixmap(QPixmap::fromImage(defaultWsPortalPicture));

    if (!portal.isEmpty()) {
        if (!portal.getPictureId().isEmpty()) {
            // Override with stage picture
            ui->wsPortalPictureLabel->setProperty("pictureId", portal.getPictureId());
            apiClient->getPicture(portal.getPictureId());
        }
    }

    if (apiClient->getSettings()->getWsPortalId() != portal.getId()) {
        apiClient->getSettings()->setWsPortalId(portal.getId());
        // Stop when portalId is empty
        wsPortalClient->restart();
    }

    updateConnectionInfo();
    updateGuidance();
}

void WsPortalDock::onWsPortalsReady(const WsPortalArray &portals)
{
    auto prev = ui->wsPortalComboBox->currentData().toString();
    auto selected = prev;

    ui->wsPortalComboBox->blockSignals(true);
    {
        ui->wsPortalComboBox->clear();

        // Display stage's names instead of party event
        if (portals.size()) {
            ui->wsPortalComboBox->addItem("", PARTICIPANT_SEELCTION_NONE); // No selection (id == "none")
            foreach (const auto &portal, portals.values()) {
                ui->wsPortalComboBox->addItem(
                    portal.getOwnerAccountView().isEmpty()
                        ? portal.getName()
                        : QString("%1 (%2)").arg(portal.getName()).arg(portal.getOwnerAccountView().getDisplayName()),
                    portal.getId()
                );
            }
        }

        // Restore selection (or apply default)
        if (selected.isEmpty()) {
            selected = apiClient->getSettings()->getWsPortalId();
        }
        if (!selected.isEmpty()) {
            ui->wsPortalComboBox->setCurrentIndex(std::max(ui->wsPortalComboBox->findData(selected), 0));
        } else {
            ui->wsPortalComboBox->setCurrentIndex(0);
        }
        // Ensure selected with current data
        selected = ui->wsPortalComboBox->currentData().toString();

        // Reset picture for no party events
        if (!portals.size()) {
            ui->wsPortalPictureLabel->setProperty("pictureId", "");
            ui->wsPortalPictureLabel->setPixmap(QPixmap::fromImage(defaultWsPortalPicture));
        }
    }
    ui->wsPortalComboBox->blockSignals(false);

    if (prev != selected) {
        onActiveWsPortalChanged(ui->wsPortalComboBox->currentIndex());
    }
}

void WsPortalDock::onWsPortalsButtonClicked()
{
    apiClient->openWsPortalsPage();
}

void WsPortalDock::onControlPanelButtonClicked()
{
    apiClient->openControlPanelPage();
}

void WsPortalDock::onConnected()
{
    ui->wsPortalStatus->setText(QTStr("Linked"));
    setThemeID(ui->wsPortalStatus, "good", "text-success");

    updateGuidance();
}

void WsPortalDock::onDisconnected()
{
    ui->wsPortalStatus->setText(QTStr("Unlinked"));
    setThemeID(ui->wsPortalStatus, "error", "text-danger");

    updateGuidance();
}

void WsPortalDock::onReconnecting()
{
    ui->wsPortalStatus->setText(QTStr("Retrying"));
    setThemeID(ui->wsPortalStatus, "warning", "text-warning");

    ui->guidanceLabel->setText(QTStr("Guidance.ReconnectingPortal"));
    setThemeID(ui->guidanceLabel, "error", "text-danger");
}
