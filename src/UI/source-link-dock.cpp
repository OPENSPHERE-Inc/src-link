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

#include <obs-module.h>
#include <qt-wrappers.hpp>

#include "../outputs/egress-link-output.hpp"
#include "source-link-dock.hpp"

//--- SouceLinkDock class ---//

SourceLinkDock::SourceLinkDock(SourceLinkApiClient *_apiClient, QWidget *parent)
    : QFrame(parent),
      ui(new Ui::SourceLinkDock),
      apiClient(_apiClient),
      defaultPartyPicture(":/source-link/images/unknownparty.png"),
      defaultPartyEventPicture(":/source-link/images/unknownevent.png")
{
    ui->setupUi(this);

    ui->partyPictureLabel->setPixmap(QPixmap::fromImage(defaultPartyPicture));
    ui->partyEventPictureLabel->setPixmap(QPixmap::fromImage(defaultPartyEventPicture));

    ui->interlockTypeComboBox->addItem("Streaming", "streaming");
    ui->interlockTypeComboBox->addItem("Recording", "recording");
    ui->interlockTypeComboBox->addItem("Streaming or Recording", "streaming_recording");
    ui->interlockTypeComboBox->addItem("Virtual Cam", "virtual_cam");
    ui->interlockTypeComboBox->addItem("Always ON", "always_on");

    ui->interlockTypeComboBox->setCurrentIndex(
        ui->interlockTypeComboBox->findData(apiClient->getSettings()->value("interlock_type", "streaming"))
    );

    connect(apiClient, SIGNAL(partiesReady(const QList<Party> &)), this, SLOT(onPartiesReady(const QList<Party> &)));
    connect(
        apiClient, SIGNAL(partyEventsReady(const QList<PartyEvent> &)), this,
        SLOT(onPartyEventsReady(const QList<PartyEvent> &))
    );
    connect(
        apiClient, SIGNAL(pictureGetSucceeded(const QString &, const QImage &)), this,
        SLOT(onPictureReady(const QString &, const QImage &))
    );
    connect(apiClient, SIGNAL(pictureGetFailed(const QString &)), this, SLOT(onPictureFailed(const QString &)));
    connect(
        apiClient, SIGNAL(seatAllocationReady(const StageSeatInfo &)), this,
        SLOT(onSeatAllocationReady(const StageSeatInfo &))
    );
    connect(apiClient, SIGNAL(seatAllocationFailed()), this, SLOT(onSeatAllocationFailed()));

    connect(ui->partyComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onActivePartyChanged(int)));
    connect(ui->partyEventComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onActivePartyEventChanged(int)));
    connect(ui->interlockTypeComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onInterlockTypeChanged(int)));

    obs_log(LOG_DEBUG, "SourceLinkDock created");
}

SourceLinkDock::~SourceLinkDock()
{
    disconnect(this);

    obs_log(LOG_DEBUG, "SourceLinkDock destroyed");
}

void SourceLinkDock::onPartiesReady(const QList<Party> &parties)
{
    auto selected = ui->partyComboBox->currentData().toString();
    ui->partyComboBox->clear();

    foreach (const auto &party, parties) {
        ui->partyComboBox->addItem(party.getName(), party.getId());
    }

    // Restore selection (or apply default)
    if (selected.isEmpty()) {
        selected = apiClient->getPartyId();
    }
    if (!selected.isEmpty()) {
        ui->partyComboBox->setCurrentIndex(ui->partyComboBox->findData(selected));
    } else {
        ui->partyComboBox->setCurrentIndex(0);
    }

    // Reset picture for no parties
    if (!parties.size()) {
        ui->partyPictureLabel->setProperty("pictureId", "");
        ui->partyPictureLabel->setPixmap(QPixmap::fromImage(defaultPartyPicture));
    }
}

void SourceLinkDock::onPartyEventsReady(const QList<PartyEvent> &partyEvents)
{
    auto selected = ui->partyEventComboBox->currentData().toString();
    ui->partyEventComboBox->clear();
    auto partyId = ui->partyComboBox->currentData().toString();

    // Fiter out party events that are not related to the selected party
    foreach (const auto &partyEvent, partyEvents) {
        if (partyEvent.getParty().isEmpty() || partyEvent.getParty().getId() != partyId) {
            continue;
        }
        ui->partyEventComboBox->addItem(partyEvent.getName(), partyEvent.getId());
    }

    // Restore selection (or apply default)
    if (selected.isEmpty()) {
        selected = apiClient->getPartyEventId();
    }
    if (!selected.isEmpty()) {
        ui->partyEventComboBox->setCurrentIndex(ui->partyEventComboBox->findData(selected));
    } else {
        ui->partyEventComboBox->setCurrentIndex(0);
    }

    // Reset picture for no party events
    if (!partyEvents.size()) {
        ui->partyEventPictureLabel->setProperty("pictureId", "");
        ui->partyEventPictureLabel->setPixmap(QPixmap::fromImage(defaultPartyEventPicture));
    }
}

void SourceLinkDock::onActivePartyChanged(int index)
{
    auto partyId = ui->partyComboBox->currentData().toString();
    foreach (const auto &party, apiClient->getParties()) {
        if (party.getId() == partyId) {
            if (!party.getPictureId().isEmpty()) {
                ui->partyPictureLabel->setProperty("pictureId", party.getPictureId());
                apiClient->getPicture(party.getPictureId());
            }
            break;
        }
    }

    // Refresh party events combo box
    onPartyEventsReady(apiClient->getPartyEvents());
}

void SourceLinkDock::onActivePartyEventChanged(int index)
{
    auto partyEventId = ui->partyEventComboBox->currentData().toString();
    foreach (const auto &partyEvent, apiClient->getPartyEvents()) {
        if (partyEvent.getId() == partyEventId) {
            if (!partyEvent.getPictureId().isEmpty()) {
                ui->partyEventPictureLabel->setProperty("pictureId", partyEvent.getPictureId());
                apiClient->getPicture(partyEvent.getPictureId());
            }
            break;
        }
    }

    apiClient->setPartyId(ui->partyComboBox->currentData().toString());
    apiClient->setPartyEventId(ui->partyEventComboBox->currentData().toString());
    apiClient->putSeatAllocation();
}

void SourceLinkDock::onPictureReady(const QString &pictureId, const QImage &picture)
{
    if (pictureId == ui->partyPictureLabel->property("pictureId").toString()) {
        // Update party picture with received image
        ui->partyPictureLabel->setPixmap(QPixmap::fromImage(picture));
    } else if (pictureId == ui->partyEventPictureLabel->property("pictureId").toString()) {
        // Update party event picture received image
        ui->partyEventPictureLabel->setPixmap(QPixmap::fromImage(picture));
    }
}

void SourceLinkDock::onPictureFailed(const QString &pictureId)
{
    if (pictureId == ui->partyPictureLabel->property("pictureId").toString()) {
        // Reset party picture to default
        ui->partyPictureLabel->setPixmap(QPixmap::fromImage(defaultPartyPicture));
    } else if (pictureId == ui->partyEventPictureLabel->property("pictureId").toString()) {
        // Reset party event picture to default
        ui->partyEventPictureLabel->setPixmap(QPixmap::fromImage(defaultPartyEventPicture));
    }
}

void SourceLinkDock::onSeatAllocationReady(const StageSeatInfo &seat)
{
    if (!seat.getAllocation().isEmpty()) {
        ui->seatAllocationStatus->setText(QString("Ready"));
        setThemeID(ui->seatAllocationStatus, "good");
        updateConnections(seat.getStage());
    } else {
        onSeatAllocationFailed();
    }
}

void SourceLinkDock::onSeatAllocationFailed()
{
    ui->seatAllocationStatus->setText(QString("No seat"));
    setThemeID(ui->seatAllocationStatus, "error");

    qDeleteAll(connectionWidgets);
    connectionWidgets.clear();
}

void SourceLinkDock::updateConnections(const Stage &stage)
{
    // Update connection widgets
    foreach (const auto widget, connectionWidgets) {
        auto found = false;
        foreach (const auto &source, stage.getSources()) {
            if (widget->source.getName() == source.getName()) {
                found = true;
                break;
            }
        }
        if (!found) {
            ui->connectionsLayout->removeWidget(widget);
            widget->deleteLater();
            connectionWidgets.removeOne(widget);
        }
    }

    foreach (const auto &source, stage.getSources()) {
        auto newcommer = true;
        foreach (const auto widget, connectionWidgets) {
            if (widget->source.getName() == source.getName()) {
                newcommer = false;
                break;
            }
        }
        if (newcommer) {
            auto interlockType = ui->interlockTypeComboBox->currentData().toString();
            auto widget = new SourceLinkConnectionWidget(source, interlockType, apiClient, this);
            connectionWidgets.append(widget);
            ui->connectionsLayout->addWidget(widget);
        }
    }
}

void SourceLinkDock::onInterlockTypeChanged(int)
{
    auto interlockType = ui->interlockTypeComboBox->currentData().toString();
    apiClient->getSettings()->setValue("interlock_type", interlockType);
}

//--- SourceLinkConnectionWidget class ---//

SourceLinkConnectionWidget::SourceLinkConnectionWidget(
    const StageSource &_source, const QString &interlockType, SourceLinkApiClient *_apiClient, QWidget *parent
)
    : QWidget(parent),
      ui(new Ui::SourceLinkConnectionWidget),
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
    connect(ui->videoSourceComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onVideoSourceChanged(int)));
    connect(ui->visibilityCheckBox, SIGNAL(clicked(bool)), this, SLOT(onVisibilityChanged(bool)));

    sourceCreateSignal.Connect(obs_get_signal_handler(), "source_create", onOBSSourcesChanged, this);
    sourceRemoveSignal.Connect(obs_get_signal_handler(), "source_remove", onOBSSourcesChanged, this);

    obs_log(LOG_DEBUG, "SourceLinkConnectionWidget created");
}

SourceLinkConnectionWidget::~SourceLinkConnectionWidget()
{
    disconnect(this);

    sourceCreateSignal.Disconnect();
    sourceRemoveSignal.Disconnect();

    delete output;

    obs_log(LOG_DEBUG, "SourceLinkConnectionWidget destroyed");
}

void SourceLinkConnectionWidget::onSettingsButtonClick()
{
    outputDialog->show();
}

void SourceLinkConnectionWidget::onVideoSourceChanged(int)
{
    output->setSourceUuid(ui->videoSourceComboBox->currentData().toString());
}

void SourceLinkConnectionWidget::onOutputStatusChanged(EgressLinkOutputStatus status)
{
    switch (status) {
    case LINKED_OUTPUT_STATUS_ACTIVE:
        ui->statusValueLabel->setText(obs_module_text("Active"));
        setThemeID(ui->statusValueLabel, "good");
        break;
    case LINKED_OUTPUT_STATUS_STAND_BY:
        ui->statusValueLabel->setText(obs_module_text("StandBy"));
        setThemeID(ui->statusValueLabel, "warning");
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

void SourceLinkConnectionWidget::updateSourceList()
{
    auto selected = ui->videoSourceComboBox->currentData().toString();
    ui->videoSourceComboBox->clear();
    ui->videoSourceComboBox->addItem(obs_module_text("None"), "disabled");
    ui->videoSourceComboBox->addItem(obs_module_text("ProgramOut"), "");

    obs_enum_sources(
        [](void *param, obs_source_t *source) {
            auto widget = (SourceLinkConnectionWidget *)param;
            auto type = obs_source_get_type(source);
            auto flags = obs_source_get_output_flags(source);

            if (flags & OBS_SOURCE_VIDEO && (type == OBS_SOURCE_TYPE_INPUT || type == OBS_SOURCE_TYPE_SCENE)) {
                widget->ui->videoSourceComboBox->addItem(obs_source_get_name(source), obs_source_get_uuid(source));
            }
            return true;
        },
        this
    );

    if (!selected.isEmpty()) {
        ui->videoSourceComboBox->setCurrentIndex(ui->videoSourceComboBox->findData(selected));
    } else {
        // Select "Program out" defaultly
        ui->videoSourceComboBox->setCurrentIndex(1);
    }
}

void SourceLinkConnectionWidget::setSource(const StageSource &_source)
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

void SourceLinkConnectionWidget::onOBSSourcesChanged(void *data, calldata_t *cd)
{
    auto widget = (SourceLinkConnectionWidget *)data;
    widget->updateSourceList();
}

void SourceLinkConnectionWidget::onVisibilityChanged(bool value)
{
    output->setVisible(value);
}
