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
#include <plugin-support.h>
#include "source-link-service.hpp"
#include "UI/settings-dialog.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

SettingsDialog *settingsDialog = nullptr;

bool obs_module_load(void)
{
	// Register menu action
    QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
    if (mainWindow) {
        QAction *menuAction = (QAction *)obs_frontend_add_tools_menu_qaction(obs_module_text("Source Link Settings"));

        settingsDialog = new SettingsDialog(mainWindow);

        menuAction->connect(menuAction, &QAction::triggered, [] {
            settingsDialog->setVisible(!settingsDialog->isVisible());
        });
    }

    obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
    return true;
}

void obs_module_unload(void)
{
    obs_log(LOG_INFO, "plugin unloaded");
}
