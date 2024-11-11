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

#include "../plugin-support.h"

#include "image-renderer.hpp"

//--- ImageRenderer class ---//

ImageRenderer::ImageRenderer(bool linearAlpha, QString file, QObject *parent) : QObject(parent)
{
    obs_log(LOG_DEBUG, "ImageRenderer creating: %s", qUtf8Printable(file));

    gs_image_file4_init(
        &if4, qUtf8Printable(file), linearAlpha ? GS_IMAGE_ALPHA_PREMULTIPLY_SRGB : GS_IMAGE_ALPHA_PREMULTIPLY
    );

    obs_enter_graphics();
    gs_image_file4_init_texture(&if4);
    obs_leave_graphics();

    if (!if4.image3.image2.image.loaded) {
        obs_log(LOG_WARNING, "Failed to load texture: %s", qUtf8Printable(file));
    }

    obs_log(LOG_DEBUG, "ImageRenderer created: %s", qUtf8Printable(file));
}

ImageRenderer::~ImageRenderer()
{
    obs_log(LOG_DEBUG, "ImageRenderer destroying");

    obs_enter_graphics();
    gs_image_file4_free(&if4);
    obs_leave_graphics();

    obs_log(LOG_DEBUG, "ImageRenderer destroyed");
}

void ImageRenderer::render(gs_effect_t *effect)
{
    render(effect, if4.image3.image2.image.cx, if4.image3.image2.image.cy);
}

void ImageRenderer::render(gs_effect_t *effect, uint32_t width, uint32_t height)
{
    gs_image_file *image = &if4.image3.image2.image;
    gs_texture_t *texture = image->texture;
    if (!texture) {
        return;
    }

    bool prev = gs_framebuffer_srgb_enabled();
    gs_enable_framebuffer_srgb(true);

    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

    gs_eparam_t *param = gs_effect_get_param_by_name(effect, "image");
    gs_effect_set_texture_srgb(param, texture);

    gs_draw_sprite(texture, 0, width, height);

    gs_blend_state_pop();
    gs_enable_framebuffer_srgb(prev);
}
