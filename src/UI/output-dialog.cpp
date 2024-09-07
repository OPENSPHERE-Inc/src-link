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

    propsView = new OBSPropertiesView(
        output->getSettings(), output,
        [](void *data) {
            auto output = static_cast<LinkedOutput *>(data);
            return output->getProperties();
        },
        nullptr,
        nullptr
    );

    propsView->setMinimumHeight(150);
    propsView->SetDeferrable(true); // Always deferrable

    ui->propertiesLayout->addWidget(propsView);
    propsView->show();

    connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(onAccept()));
    connect(
        apiClient, SIGNAL(seatAllocationReady(const SeatAllocation *)), this,
        SLOT(onSeatAllocationReady(const SeatAllocation *))
    );

    obs_log(LOG_DEBUG, "OutputDialog created");
}

OutputDialog::~OutputDialog()
{
    disconnect(
        apiClient, SIGNAL(seatAllocationReady(const SeatAllocation *)), this,
        SLOT(onSeatAllocationReady(const SeatAllocation *))
    );

    obs_log(LOG_DEBUG, "OutputDialog destroyed");
}

void OutputDialog::onAccept()
{
    // Apply encoder settings to output
    output->update(propsView->GetSettings());   

    if (ui->enableCheckBox->isChecked()) {
        // Start output
        output->startOutput(obs_get_video(), obs_get_audio());
    } else {
        // Stop output
        output->stopOutput();
    }
}

void OutputDialog::onSeatAllocationReady(const StageSeatAllocation *seatAllocation)
{
    // Refresh properties view
    propsView->ReloadProperties();
}
