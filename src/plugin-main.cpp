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
#include <obs-frontend-api.h>

#include <QDesktopServices>
#include <QUrl>
#include <QMainWindow>

#include "plugin-support.h"
#include "UI/settings-dialog.hpp"
#include "UI/output-dialog.hpp"
#include "UI/source-link-dock.hpp"
#include "outputs/linked-output.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

extern obs_source_info createLinkedSourceInfo();

SettingsDialog *settingsDialog = nullptr;
SourceLinkApiClient *apiClient = nullptr;
SourceLinkDock *sourceLinkDock = nullptr;

obs_source_info linkedSourceInfo;

void registerLinkedSourceDock()
{
    auto mainWindow = (QMainWindow *)obs_frontend_get_main_window();
    if (mainWindow) {
        if (!sourceLinkDock) {
            sourceLinkDock = new SourceLinkDock(apiClient, mainWindow);
            obs_frontend_add_dock_by_id("SourceLinkDock", obs_module_text("SourceLinkDock"), sourceLinkDock);
        }
    }
}

void unregisterLinkedSourceDock()
{
    if (sourceLinkDock) {
        obs_frontend_remove_dock("SourceLinkDock");
        sourceLinkDock->deleteLater();
        sourceLinkDock = nullptr;
    }
}

bool obs_module_load(void)
{
    apiClient = new SourceLinkApiClient();

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
            registerLinkedSourceDock();
        }

        QObject::connect(apiClient, &SourceLinkApiClient::loginSucceeded, []() { registerLinkedSourceDock(); });
        QObject::connect(apiClient, &SourceLinkApiClient::logoutSucceeded, []() { unregisterLinkedSourceDock(); });
    }

    obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
    return true;
}

void obs_module_unload(void)
{
    delete apiClient;
    obs_log(LOG_INFO, "plugin unloaded");
}
