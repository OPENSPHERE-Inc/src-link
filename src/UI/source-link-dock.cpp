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

#include "source-link-dock.hpp"

SourceLinkDock::SourceLinkDock(SourceLinkApiClient *_apiClient, QWidget *parent)
    : QFrame(parent),
      ui(new Ui::SourceLinkDock),
      apiClient(_apiClient)
{
    ui->setupUi(this);

    partyPictureScene = new QGraphicsScene(this);
    partyPictureScene->addPixmap(QPixmap::fromImage(defaultPartyPicture));
    ui->partyPictureView->setScene(partyPictureScene);

    partyEventPictureScene = new QGraphicsScene(this);
    partyEventPictureScene->addPixmap(QPixmap::fromImage(defaultPartyEventPicture));
    ui->partyEventPictureView->setScene(partyEventPictureScene);

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

    connect(ui->partyComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onActivePartyChanged(int)));
    connect(ui->partyEventComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onActivePartyEventChanged(int)));

    obs_log(LOG_DEBUG, "SourceLinkDock created");
}

SourceLinkDock::~SourceLinkDock()
{
    disconnect(this);

    obs_log(LOG_DEBUG, "SourceLinkDock destroyed");
}

void SourceLinkDock::onPartiesReady(const QList<Party> &parties)
{
    auto initial = ui->partyComboBox->count() == 0;
    ui->partyComboBox->clear();

    foreach(const auto &party, parties)
    {
        ui->partyComboBox->addItem(party.getName(), party.getId());
    }

    if (!initial) {
        return;
    }

    if (!apiClient->getPartyId().isEmpty()) {
        ui->partyComboBox->setCurrentIndex(ui->partyComboBox->findData(apiClient->getPartyId()));
    } else {
        ui->partyComboBox->setCurrentIndex(0);
    }
}

void SourceLinkDock::onPartyEventsReady(const QList<PartyEvent> &partyEvents)
{
    auto initial = ui->partyEventComboBox->count() == 0;
    ui->partyEventComboBox->clear();
    auto partyId = ui->partyComboBox->currentData().toString();

    // Fiter out party events that are not related to the selected party
    foreach(const auto &partyEvent, partyEvents)
    {
        if (partyEvent.getParty().isEmpty() || partyEvent.getParty().getId() != partyId) {
            continue;
        }
        ui->partyEventComboBox->addItem(partyEvent.getName(), partyEvent.getId());
    }

    if (!initial) {
        return;
    }

    if (!apiClient->getPartyEventId().isEmpty()) {
        ui->partyEventComboBox->setCurrentIndex(ui->partyEventComboBox->findData(apiClient->getPartyEventId()));
    } else {
        ui->partyEventComboBox->setCurrentIndex(0);
    }
}

void SourceLinkDock::onActivePartyChanged(int index)
{
    auto partyId = ui->partyComboBox->currentData().toString();
    foreach(const auto &party, apiClient->getParties())
    {
        if (party.getId() == partyId) {
            if (!party.getPictureId().isEmpty()) {
                ui->partyPictureView->setProperty("pictureId", party.getPictureId());
                apiClient->getPicture(party.getPictureId());
            }
            break;
        }
    }

    // Refresh party events combo box
    QMetaObject::invokeMethod(
        this, "onPartyEventsReady", Qt::QueuedConnection, Q_ARG(QList<PartyEvent>, apiClient->getPartyEvents())
    );
}

void SourceLinkDock::onActivePartyEventChanged(int index)
{
    auto partyEventId = ui->partyEventComboBox->currentData().toString();
    foreach(const auto &partyEvent, apiClient->getPartyEvents())
    {
        if (partyEvent.getId() == partyEventId) {
            if (!partyEvent.getPictureId().isEmpty()) {
                ui->partyEventPictureView->setProperty("pictureId", partyEvent.getPictureId());
                apiClient->getPicture(partyEvent.getPictureId());
            }
            break;
        }
    }
}

void SourceLinkDock::onPictureReady(const QString &pictureId, const QImage &picture)
{
    if (pictureId == ui->partyPictureView->property("pictureId").toString()) {
        // Update party picture
        partyPictureScene->addPixmap(QPixmap::fromImage(picture));
    } else if (pictureId == ui->partyEventPictureView->property("pictureId").toString()) {
        // Update party event picture
        partyEventPictureScene->addPixmap(QPixmap::fromImage(picture));
    }
}

void SourceLinkDock::onPictureFailed(const QString &pictureId)
{
    if (pictureId == ui->partyPictureView->property("pictureId").toString()) {
        // Update party picture
        partyPictureScene->addPixmap(QPixmap::fromImage(defaultPartyPicture));
    } else if (pictureId == ui->partyEventPictureView->property("pictureId").toString()) {
        // Update party event picture
        partyEventPictureScene->addPixmap(QPixmap::fromImage(defaultPartyEventPicture));
    }
}