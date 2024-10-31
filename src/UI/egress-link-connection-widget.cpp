/*
SR Link
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

#include <qt-wrappers.hpp>

#include "egress-link-connection-widget.hpp"

//--- EgressLinkConnectionWidget class ---//

EgressLinkConnectionWidget::EgressLinkConnectionWidget(
    const StageSource &_source, const QString &interlockType, SRLinkApiClient *_apiClient, QWidget *parent
)
    : QWidget(parent),
      ui(new Ui::EgressLinkConnectionWidget),
      source(_source)
{
    ui->setupUi(this);

    output = new EgressLinkOutput(source.getName(), _apiClient);
    outputDialog = new OutputDialog(output, this);

    // Must be called after output and outputDialog initialization
    setSource(_source);

    ui->settingsButton->setProperty("themeID", "cogsIcon");
    ui->visibilityCheckBox->setProperty("visibilityCheckBox", true);
    ui->visibilityCheckBox->setChecked(output->getVisible());

    onOutputStatusChanged(LINKED_OUTPUT_STATUS_INACTIVE);
    updateSourceList();

    if (output->getSourceUuid().isEmpty()) {
        ui->videoSourceComboBox->setCurrentIndex(1);
    } else {
        ui->videoSourceComboBox->setCurrentIndex(ui->videoSourceComboBox->findData(output->getSourceUuid()));
    }

    connect(
        output, SIGNAL(statusChanged(EgressLinkOutputStatus)), this, SLOT(onOutputStatusChanged(EgressLinkOutputStatus))
    );
    connect(ui->settingsButton, SIGNAL(clicked()), this, SLOT(onSettingsButtonClick()));
    connect(ui->visibilityCheckBox, SIGNAL(clicked(bool)), this, SLOT(onVisibilityChanged(bool)));
    setEnableVideoSourceChangeEvent(true);

    sourceCreateSignal.Connect(obs_get_signal_handler(), "source_create", onOBSSourcesChanged, this);
    sourceRemoveSignal.Connect(obs_get_signal_handler(), "source_remove", onOBSSourcesChanged, this);

    obs_log(LOG_DEBUG, "EgressLinkConnectionWidget created");
}

EgressLinkConnectionWidget::~EgressLinkConnectionWidget()
{
    disconnect(this);

    sourceCreateSignal.Disconnect();
    sourceRemoveSignal.Disconnect();

    delete output;

    obs_log(LOG_DEBUG, "EgressLinkConnectionWidget destroyed");
}

void EgressLinkConnectionWidget::setEnableVideoSourceChangeEvent(bool enabled)
{
    if (enabled) {
        connect(ui->videoSourceComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onVideoSourceChanged(int)));
    } else {
        disconnect(ui->videoSourceComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onVideoSourceChanged(int)));
    }
}

void EgressLinkConnectionWidget::onSettingsButtonClick()
{
    outputDialog->show();
}

void EgressLinkConnectionWidget::onVideoSourceChanged(int)
{
    output->setSourceUuid(ui->videoSourceComboBox->currentData().toString());
}

void EgressLinkConnectionWidget::onOutputStatusChanged(EgressLinkOutputStatus status)
{
    switch (status) {
    case LINKED_OUTPUT_STATUS_ACTIVE:
        ui->statusValueLabel->setText(obs_module_text("Active"));
        setThemeID(ui->statusValueLabel, "good");
        break;
    case LINKED_OUTPUT_STATUS_STAND_BY:
        ui->statusValueLabel->setText(obs_module_text("StandBy"));
        setThemeID(ui->statusValueLabel, "good");
        break;
    case LINKED_OUTPUT_STATUS_ERROR:
        ui->statusValueLabel->setText(obs_module_text("Error"));
        setThemeID(ui->statusValueLabel, "error");
        break;
    case LINKED_OUTPUT_STATUS_INACTIVE:
        ui->statusValueLabel->setText(obs_module_text("Inactive"));
        setThemeID(ui->statusValueLabel, "");
        break;
    case LINKED_OUTPUT_STATUS_DISABLED:
        ui->statusValueLabel->setText(obs_module_text("Disabled"));
        setThemeID(ui->statusValueLabel, "");
        break;
    }
}

void EgressLinkConnectionWidget::updateSourceList()
{
    // Prevent event triggering during changing combo box items
    setEnableVideoSourceChangeEvent(false);
    {
        auto selected = output->getSourceUuid(); // The output keeps current value
        ui->videoSourceComboBox->clear();
        ui->videoSourceComboBox->addItem(obs_module_text("None"), "disabled");
        ui->videoSourceComboBox->addItem(obs_module_text("ProgramOut"), "");

        obs_enum_sources(
            [](void *param, obs_source_t *source) {
                auto widget = (EgressLinkConnectionWidget *)param;
                auto type = obs_source_get_type(source);
                auto flags = obs_source_get_output_flags(source);

                if (flags & OBS_SOURCE_VIDEO && (type == OBS_SOURCE_TYPE_INPUT || type == OBS_SOURCE_TYPE_SCENE)) {
                    widget->ui->videoSourceComboBox->addItem(obs_source_get_name(source), obs_source_get_uuid(source));
                }
                return true;
            },
            this
        );

        auto index = ui->videoSourceComboBox->findData(selected);
        if (!selected.isEmpty() && index != -1) {
            ui->videoSourceComboBox->setCurrentIndex(index);
        } else {
            // Select "Program out" defaultly
            ui->videoSourceComboBox->setCurrentIndex(1);
            // Trigger event manually
            onVideoSourceChanged(1);
        }
    }
    setEnableVideoSourceChangeEvent(true);
}

void EgressLinkConnectionWidget::setSource(const StageSource &_source)
{
    source = _source;

    ui->headerLabel->setText(source.getDisplayName());
    ui->descriptionLabel->setText(source.getDescription());
    if (source.getDescription().isEmpty()) {
        ui->descriptionLabel->hide();
    } else {
        ui->descriptionLabel->show();
    }

    outputDialog->setWindowTitle(source.getDisplayName());
    output->setName(source.getName());
}

void EgressLinkConnectionWidget::onOBSSourcesChanged(void *data, calldata_t *cd)
{
    auto widget = (EgressLinkConnectionWidget *)data;
    widget->updateSourceList();
}

void EgressLinkConnectionWidget::onVisibilityChanged(bool value)
{
    output->setVisible(value);
}
