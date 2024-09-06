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

    view = new OBSPropertiesView(
        output->getSettings(), output,
        [](void *data) {
            auto output = static_cast<LinkedOutput *>(data);
            return output->getProperties();
        },
        nullptr,
        [](void *data, obs_data_t *settings) {
            auto output = static_cast<LinkedOutput *>(data);
            output->update(settings);
        }
    );

    view->setMinimumHeight(150);

    ui->propertiesLayout->addWidget(view);
    view->show();

    obs_log(LOG_DEBUG, "OutputDialog created");
}

OutputDialog::~OutputDialog() 
{
    obs_log(LOG_DEBUG, "OutputDialog destroyed");
}
