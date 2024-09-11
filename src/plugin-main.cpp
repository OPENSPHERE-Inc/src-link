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
#include "outputs/linked-output.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

extern obs_source_info createLinkedSourceInfo();

SettingsDialog *settingsDialog = nullptr;
SourceLinkApiClient *apiClient = nullptr;
OutputDialog *outputDialog = nullptr;
LinkedOutput *mainOutput = nullptr;

obs_source_info linkedSourceInfo;

bool obs_module_load(void)
{
    apiClient = new SourceLinkApiClient();

    // Register "linked_source" source
    linkedSourceInfo = createLinkedSourceInfo();
    obs_register_source(&linkedSourceInfo);

    // Create main output
    mainOutput = new LinkedOutput(QString("main"), apiClient);

    // Register menu action
    QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
    if (mainWindow) {
        settingsDialog = new SettingsDialog(apiClient, mainWindow);

        outputDialog = new OutputDialog(apiClient, mainOutput, mainWindow);

        // Settings menu item
        QAction *settingsMenuAction =
            (QAction *)obs_frontend_add_tools_menu_qaction(obs_module_text("Source Link Settings"));
        settingsMenuAction->connect(settingsMenuAction, &QAction::triggered, [] {
            settingsDialog->setVisible(!settingsDialog->isVisible());
        });

        // Output menu item
        QAction *outputMenuAction =
            (QAction *)obs_frontend_add_tools_menu_qaction(obs_module_text("Source Link Output"));
        outputMenuAction->connect(outputMenuAction, &QAction::triggered, [] {
            outputDialog->setVisible(!outputDialog->isVisible());
        });
    }

    obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
    return true;
}

void obs_module_unload(void)
{
    delete mainOutput;
    delete apiClient;
    obs_log(LOG_INFO, "plugin unloaded");
}
