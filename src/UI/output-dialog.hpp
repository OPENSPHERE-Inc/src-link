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
#include <properties-view.hpp>

#include <QDialog>

#include "ui_output-dialog.h"
#include "../api-client.hpp"
#include "../outputs/egress-link-output.hpp"

class OutputDialog : public QDialog {
    Q_OBJECT

    Ui::OutputDialog *ui;

    EgressLinkOutput *output;
    OBSPropertiesView *propsView;
    OBSSignal sourceCreateSignal;
    OBSSignal sourceRemoveSignal;

    static void onOBSSourcesChanged(void *data, calldata_t *cd);

private slots:
    void onAccept();

protected:
    void showEvent(QShowEvent *event) override;

public:
    explicit OutputDialog(EgressLinkOutput *_output, QWidget *parent = nullptr);
    ~OutputDialog();

};
