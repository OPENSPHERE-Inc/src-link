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

#pragma once

#include <obs.hpp>
#include <graphics/image-file.h>

#include <QObject>

class ImageRenderer : public QObject {
    Q_OBJECT

    gs_image_file4_t if4;

public:
    explicit ImageRenderer(bool linearAlpha, QString file, QObject *parent = nullptr);
    ~ImageRenderer();

    void render(gs_effect_t *effect);
    void render(gs_effect_t *effect, uint32_t width, uint32_t height);
};
