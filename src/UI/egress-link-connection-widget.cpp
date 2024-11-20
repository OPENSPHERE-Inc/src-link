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

#include <qt-wrappers.hpp>

#include "egress-link-connection-widget.hpp"

//--- EgressLinkConnectionWidget class ---//

EgressLinkConnectionWidget::EgressLinkConnectionWidget(
    const StageSource &_source, SRCLinkApiClient *_apiClient, QWidget *parent
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

    onOutputStatusChanged(EGRESS_LINK_OUTPUT_STATUS_INACTIVE);
    updateSourceList();

    if (output->getSourceUuid().isEmpty()) {
        ui->videoSourceComboBox->setCurrentIndex(0);
    } else {
        ui->videoSourceComboBox->setCurrentIndex(ui->videoSourceComboBox->findData(output->getSourceUuid()));
    }

    connect(
        output, SIGNAL(statusChanged(EgressLinkOutputStatus)), this, SLOT(onOutputStatusChanged(EgressLinkOutputStatus))
    );
    connect(ui->settingsButton, SIGNAL(clicked()), this, SLOT(onSettingsButtonClick()));
    connect(ui->visibilityCheckBox, SIGNAL(clicked(bool)), this, SLOT(onVisibilityChanged(bool)));
    connect(ui->videoSourceComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onVideoSourceChanged(int)));

    sourceCreateSignal.Connect(obs_get_signal_handler(), "source_create", onOBSSourcesChanged, this);
    sourceRemoveSignal.Connect(obs_get_signal_handler(), "source_dstroy", onOBSSourcesChanged, this);

    obs_frontend_add_event_callback(onOBSFrontendEvent, this);

    // Translations
    ui->videoSourceLabel->setText(QTStr("LocalSource"));
    ui->statusLabel->setText(QTStr("Status"));

    obs_log(LOG_DEBUG, "EgressLinkConnectionWidget created");
}

EgressLinkConnectionWidget::~EgressLinkConnectionWidget()
{
    disconnect(this);

    sourceCreateSignal.Disconnect();
    sourceRemoveSignal.Disconnect();

    obs_frontend_remove_event_callback(onOBSFrontendEvent, this);

    delete output;

    obs_log(LOG_DEBUG, "EgressLinkConnectionWidget destroyed");
}

void EgressLinkConnectionWidget::onOBSSourcesChanged(void *data, calldata_t *)
{
    auto widget = (EgressLinkConnectionWidget *)data;
    widget->updateSourceList();
}

void EgressLinkConnectionWidget::onOBSFrontendEvent(enum obs_frontend_event event, void *param)
{
    auto widget = (EgressLinkConnectionWidget *)param;
    // Prevent to reset output config
    if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
        widget->sourceCreateSignal.Disconnect();
        widget->sourceRemoveSignal.Disconnect();
    }
}

void EgressLinkConnectionWidget::onSettingsButtonClick()
{
    outputDialog->show();
}

void EgressLinkConnectionWidget::onVideoSourceChanged(int)
{
    obs_log(LOG_DEBUG, "Video source changed: %s", qUtf8Printable(ui->videoSourceComboBox->currentText()));
    output->setSourceUuid(ui->videoSourceComboBox->currentData().toString());
}

void EgressLinkConnectionWidget::onOutputStatusChanged(EgressLinkOutputStatus status)
{
    switch (status) {
    case EGRESS_LINK_OUTPUT_STATUS_ACTIVE:
        ui->statusValueLabel->setText(QTStr("Active"));
        setThemeID(ui->statusValueLabel, "good");
        break;
    case EGRESS_LINK_OUTPUT_STATUS_STAND_BY:
        ui->statusValueLabel->setText(QTStr("StandBy"));
        setThemeID(ui->statusValueLabel, "good");
        break;
    case EGRESS_LINK_OUTPUT_STATUS_ERROR:
        ui->statusValueLabel->setText(QTStr("Error"));
        setThemeID(ui->statusValueLabel, "error");
        break;
    case EGRESS_LINK_OUTPUT_STATUS_INACTIVE:
        ui->statusValueLabel->setText(QTStr("Inactive"));
        setThemeID(ui->statusValueLabel, "");
        break;
    case EGRESS_LINK_OUTPUT_STATUS_DISABLED:
        ui->statusValueLabel->setText(QTStr("Disabled"));
        setThemeID(ui->statusValueLabel, "");
        break;
    }
}

void EgressLinkConnectionWidget::updateSourceList()
{
    // Prevent event triggering during changing combo box items
    ui->videoSourceComboBox->blockSignals(true);
    {
        auto selected = output->getSourceUuid(); // The output keeps current value
        ui->videoSourceComboBox->clear();
        ui->videoSourceComboBox->addItem(QTStr("None"), "");
        ui->videoSourceComboBox->addItem(QTStr("ProgramOut"), "program");

        obs_enum_sources(
            [](void *param, obs_source_t *_source) {
                auto widget = (EgressLinkConnectionWidget *)param;
                auto type = obs_source_get_type(_source);
                auto flags = obs_source_get_output_flags(_source);

                if (flags & OBS_SOURCE_VIDEO && (type == OBS_SOURCE_TYPE_INPUT || type == OBS_SOURCE_TYPE_SCENE)) {
                    widget->ui->videoSourceComboBox->addItem(obs_source_get_name(_source), obs_source_get_uuid(_source));
                }
                return true;
            },
            this
        );

        auto index = ui->videoSourceComboBox->findData(selected);
        if (index != -1) {
            ui->videoSourceComboBox->setCurrentIndex(index);
        } else {
            // Select "disabled" defaultly
            ui->videoSourceComboBox->setCurrentIndex(0);
            // Trigger event manually
            onVideoSourceChanged(0);
        }
    }
    ui->videoSourceComboBox->blockSignals(false);
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

void EgressLinkConnectionWidget::onVisibilityChanged(bool value)
{
    output->setVisible(value);
}
