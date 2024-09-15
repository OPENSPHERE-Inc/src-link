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
#include <QGraphicsScene>

#include "ui_settings-dialog.h"
#include "../api-client.hpp"

class SettingsDialog : public QDialog {
    Q_OBJECT

    Ui::SettingsDialog *ui;

    SourceLinkApiClient* apiClient;

    void setClientActive(bool active);

private slots:
    void onConnectionButtonClick();
    void onAccept();

    void onLinkingFailed();
    void onAccountInfoReady(const AccountInfo &accountInfo);
    void saveSettings();
    void loadSettings();

protected:
    void showEvent(QShowEvent *event) override;

public:
    SettingsDialog(SourceLinkApiClient* _apiClient, QWidget *parent = nullptr);
    ~SettingsDialog();

};