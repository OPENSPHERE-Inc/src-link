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

#include <QWidget>

#include "../api-client.hpp"
#include "output-dialog.hpp"
#include "ui_source-link-connection-widget.h"

class SourceLinkConnectionWidget : public QWidget {
    Q_OBJECT

    friend class SourceLinkDock;

    Ui::SourceLinkConnectionWidget *ui;

    StageSource source;

    EgressLinkOutput *output;
    OutputDialog *outputDialog;
    OBSSignal sourceCreateSignal;
    OBSSignal sourceRemoveSignal;

    static void onOBSSourcesChanged(void *data, calldata_t *cd);

    void setEnableVideoSourceChangeEvent(bool enabled);

private slots:
    void onSettingsButtonClick();
    void onVideoSourceChanged(int index);
    void onOutputStatusChanged(EgressLinkOutputStatus status);
    void updateSourceList();
    void onVisibilityChanged(bool value);

public:
    explicit SourceLinkConnectionWidget(
        const StageSource &_source, const QString &interlockType, SourceLinkApiClient *_client,
        QWidget *parent = nullptr
    );
    ~SourceLinkConnectionWidget();

    void setSource(const StageSource &_source);
};