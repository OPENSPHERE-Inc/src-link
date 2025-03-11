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

#include <QPushButton>

#include "redeem-invite-code-dialog.hpp"
#include "../utils.hpp"

//--- RedeemInviteCodeDialog class ---//

RedeemInviteCodeDialog::RedeemInviteCodeDialog(QWidget *parent) : QDialog(parent), ui(new Ui::RedeemInviteCodeDialog)
{
    ui->setupUi(this);

    // OK button is disabled defaultly
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    connect(ui->inviteCodeEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!text.isEmpty());
    });
    connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(onAccepted()));

    // Translations
    ui->inviteCodeLabel->setText(QTStr("InvitationCode"));
    setWindowTitle(QTStr("RedeemInvitationCode"));
}

RedeemInviteCodeDialog::~RedeemInviteCodeDialog()
{
    disconnect(this);
}

void RedeemInviteCodeDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    ui->inviteCodeEdit->clear();
}

void RedeemInviteCodeDialog::onAccepted()
{
    emit accepted(ui->inviteCodeEdit->text());
}
