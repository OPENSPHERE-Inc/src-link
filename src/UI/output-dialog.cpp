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

#include "output-dialog.hpp"

//--- OutputDialog class ---//

OutputDialog::OutputDialog(SourceLinkApiClient *_apiClient, LinkedOutput *_output, QWidget *parent)
    : QDialog(parent),
      ui(new Ui::OutputDialog),
      apiClient(_apiClient),
      output(_output)
{
    ui->setupUi(this);

    // Create dedicated data instance
    OBSDataAutoRelease settings = obs_data_create();
    output->getDefault(settings);
    obs_data_apply(settings, output->getSettings());

    // First arg must has non-null reference    
    propsView = new OBSPropertiesView(
        settings.Get(), output,
        [](void *data) {
            auto output = static_cast<LinkedOutput *>(data);
            auto properties = output->getProperties();
            // Neccessary to apply default settings
            obs_properties_apply_settings(properties, output->getSettings());
            return properties;
        },
        nullptr, nullptr
    );

    propsView->setMinimumHeight(150);
    propsView->SetDeferrable(true); // Always deferrable

    ui->propertiesLayout->addWidget(propsView);
    propsView->show();

    connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(onAccept()));
    connect(
        apiClient, SIGNAL(seatAllocationReady(const StageSeatInfo &)), this,
        SLOT(onSeatAllocationReady(const StageSeatInfo &))
    );

    obs_log(LOG_DEBUG, "OutputDialog created");
}

OutputDialog::~OutputDialog()
{
    disconnect(this);

    obs_log(LOG_DEBUG, "OutputDialog destroyed");
}

void OutputDialog::onAccept()
{
    // Apply encoder settings to output
    propsView->UpdateSettings();

    output->update(propsView->GetSettings());
}

void OutputDialog::onSeatAllocationReady(const StageSeatInfo &seat)
{
    // Refresh properties view
    propsView->ReloadProperties();
}

void OutputDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    apiClient->putSeatAllocation();
}
