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

#pragma once

#include <obs-module.h>

#include <QDialog>

#include "ui_settings-dialog.h"
#include "../api-client.hpp"
#include "../objects.hpp"

class SettingsDialog : public QDialog {
    Q_OBJECT

    SourceLinkApiClient* apiClient;

    void setClientActive(bool active);

public:
    SettingsDialog(SourceLinkApiClient* _apiClient, QWidget *parent = nullptr);
    ~SettingsDialog();

private slots:
    void onConnect();
    void onDisconnect();
    void onAccept();

    void onLinkingFailed();
    void onAccountInfoReady(const AccountInfo *accountInfo);
    void onPartiesReady(const QList<Party *> &parties);
    void onPartyEventsReady(const QList<PartyEvent *> &events);
    void saveSettings();
    void loadSettings();
    void onActivePartyChanged(int index);

private:
    Ui::SettingsDialog *ui;
};