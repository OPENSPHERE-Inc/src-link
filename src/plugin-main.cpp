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

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QDesktopServices>
#include <QUrl>
#include <QMainWindow>
#include <QDir>

#include "plugin-support.h"
#include "UI/settings-dialog.hpp"
#include "UI/output-dialog.hpp"
#include "UI/egress-link-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define SR_LINK_EGRESS_DOCK_ID "SRLinkDock"

extern obs_source_info createLinkedSourceInfo();

SettingsDialog *settingsDialog = nullptr;
SRLinkSettingsStore *settingsStore = nullptr;
SRLinkApiClient *apiClient = nullptr;
EgressLinkDock *sourceLinkDock = nullptr;

obs_source_info linkedSourceInfo;

void registerSRLinkDock()
{
    auto mainWindow = (QMainWindow *)obs_frontend_get_main_window();
    if (mainWindow) {
        if (!sourceLinkDock) {
            sourceLinkDock = new EgressLinkDock(apiClient, mainWindow);
            obs_frontend_add_dock_by_id(SR_LINK_EGRESS_DOCK_ID, obs_module_text("SRLinkUplinkDock"), sourceLinkDock);
        }
    }
}

void unregisterSRLinkDock()
{
    if (sourceLinkDock) {
        // The instance will be deleted by OBS (Do not call delete manually!)
        obs_frontend_remove_dock(SR_LINK_EGRESS_DOCK_ID);
        sourceLinkDock = nullptr;
    }
}

void frontendEventCallback(enum obs_frontend_event event, void *)
{
    switch (event) {
    case OBS_FRONTEND_EVENT_EXIT:
        if (apiClient) {
            apiClient->terminate();
        }
    }
}

bool obs_module_load(void)
{
    apiClient = new SRLinkApiClient();

    obs_frontend_add_event_callback(frontendEventCallback, nullptr);

    // Register "linked_source" source
    linkedSourceInfo = createLinkedSourceInfo();
    obs_register_source(&linkedSourceInfo);

    // Register menu action
    auto mainWindow = (QMainWindow *)obs_frontend_get_main_window();
    if (mainWindow) {
        // Settings menu item
        settingsDialog = new SettingsDialog(apiClient, mainWindow);
        QAction *settingsMenuAction =
            (QAction *)obs_frontend_add_tools_menu_qaction(obs_module_text("SourceLinkSettings"));
        settingsMenuAction->connect(settingsMenuAction, &QAction::triggered, [] { settingsDialog->show(); });

        // Dock
        if (apiClient->isLoggedIn()) {
            registerSRLinkDock();
        }

        QObject::connect(apiClient, &SRLinkApiClient::loginSucceeded, []() { registerSRLinkDock(); });
        QObject::connect(apiClient, &SRLinkApiClient::logoutSucceeded, []() { unregisterSRLinkDock(); });
    }

    obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
    return true;
}

void obs_module_unload(void)
{
    delete apiClient;
    apiClient = nullptr;
    obs_log(LOG_INFO, "plugin unloaded");
}
