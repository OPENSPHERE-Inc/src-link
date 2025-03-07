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

#include "output-dialog.hpp"

//--- OutputDialog class ---//

OutputDialog::OutputDialog(EgressLinkOutput *_output, QWidget *parent)
    : QDialog(parent),
      ui(new Ui::OutputDialog),
      output(_output)
{
    ui->setupUi(this);

    // Create dedicated data instance
    OBSDataAutoRelease settings = obs_data_create();
    output->getDefaults(settings);
    obs_data_apply(settings, output->getSettings());

    // First arg must has non-null reference
    propsView = new OBSPropertiesView(
        settings.Get(), output,
        [](void *_data) {
            auto egressOutput = static_cast<EgressLinkOutput *>(_data);
            auto properties = egressOutput->getProperties();
            // Neccessary to apply default settings
            obs_properties_apply_settings(properties, egressOutput->getSettings());
            return properties;
        },
        nullptr, nullptr
    );

    propsView->setMinimumHeight(150);
    propsView->SetDeferrable(true); // Always deferrable

    ui->propertiesLayout->addWidget(propsView);
    propsView->show();

    connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(onAccept()));

    sourceCreateSignal.Connect(obs_get_signal_handler(), "source_create", onOBSSourcesChanged, this);
    sourceRemoveSignal.Connect(obs_get_signal_handler(), "source_destroy", onOBSSourcesChanged, this);

    obs_log(LOG_DEBUG, "OutputDialog created");
}

OutputDialog::~OutputDialog()
{
    disconnect(this);

    sourceCreateSignal.Disconnect();
    sourceRemoveSignal.Disconnect();

    obs_log(LOG_DEBUG, "OutputDialog destroyed");
}

void OutputDialog::onAccept()
{
    output->update(propsView->GetSettings());
}

void OutputDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    propsView->ReloadProperties();
    obs_data_apply(propsView->GetSettings(), output->getSettings());
}

void OutputDialog::onOBSSourcesChanged(void *_data, calldata_t *)
{
    auto dialog = static_cast<OutputDialog *>(_data);
    // Prevent crash
    QMetaObject::invokeMethod(dialog, "reloadProperties", Qt::QueuedConnection);
}

void OutputDialog::reloadProperties()
{
    propsView->ReloadProperties();
}
