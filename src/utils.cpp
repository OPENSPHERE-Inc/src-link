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

#include <util/platform.h>

#include "utils.hpp"
#include "plugin-support.h"

//#define API_DEBUG

// The code from obs-websocket plugin
// https://github.com/obsproject/obs-websocket/blob/a73c92788d70f08f91b8c0477b74f99c999beb09/src/requesthandler/RequestHandler_Sources.cpp#L28
QImage takeSourceScreenshot(obs_source_t *source, bool &success, uint32_t requestedWidth, uint32_t requestedHeight)
{
    // Get info about the requested source
    const uint32_t sourceWidth = obs_source_get_width(source);
    const uint32_t sourceHeight = obs_source_get_height(source);
    const double sourceAspectRatio = ((double)sourceWidth / (double)sourceHeight);

    uint32_t imgWidth = sourceWidth;
    uint32_t imgHeight = sourceHeight;

    // Determine suitable image width
    if (requestedWidth) {
        imgWidth = requestedWidth;

        if (!requestedHeight)
            imgHeight = (uint32_t)((double)imgWidth / sourceAspectRatio);
    }

    // Determine suitable image height
    if (requestedHeight) {
        imgHeight = requestedHeight;

        if (!requestedWidth)
            imgWidth = (uint32_t)((double)imgHeight * sourceAspectRatio);
    }

#ifdef API_DEBUG
    obs_log(LOG_DEBUG, "screenshot: width=%d, height=%d", imgWidth, imgHeight);
#endif

    // Create final image texture
    QImage ret(imgWidth, imgHeight, QImage::Format::Format_RGBA8888);
    ret.fill(0);

    // Video image buffer
    uint8_t *videoData = nullptr;
    uint32_t videoLinesize = 0;

    // Enter graphics context
    obs_enter_graphics();

    gs_texrender_t *texRender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    gs_stagesurf_t *stageSurface = gs_stagesurface_create(imgWidth, imgHeight, GS_RGBA);

    success = false;
    gs_texrender_reset(texRender);
    if (gs_texrender_begin(texRender, imgWidth, imgHeight)) {
        vec4 background;
        vec4_zero(&background);

        gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
        gs_ortho(0.0f, (float)sourceWidth, 0.0f, (float)sourceHeight, -100.0f, 100.0f);

        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

        obs_source_inc_showing(source);
        obs_source_video_render(source);
        obs_source_dec_showing(source);

        gs_blend_state_pop();
        gs_texrender_end(texRender);

        gs_stage_texture(stageSurface, gs_texrender_get_texture(texRender));
        if (gs_stagesurface_map(stageSurface, &videoData, &videoLinesize)) {
            auto lineSize = ret.bytesPerLine();
            for (uint y = 0; y < imgHeight; y++) {
                memcpy(ret.scanLine(y), videoData + (y * videoLinesize), lineSize);
            }
            gs_stagesurface_unmap(stageSurface);
            success = true;
        }
    }

    gs_stagesurface_destroy(stageSurface);
    gs_texrender_destroy(texRender);

    obs_leave_graphics();

    return ret;
}

// Origin: https://github.com/obsproject/obs-studio/blob/06642fdee48477ab85f89ff670f105affe402df7/UI/obs-app.cpp#L1871
QString getFormatExt(const char *container)
{
    QString ext = container;
    if (ext == "fragmented_mp4")
        ext = "mp4";
    if (ext == "hybrid_mp4")
        ext = "mp4";
    else if (ext == "fragmented_mov")
        ext = "mov";
    else if (ext == "hls")
        ext = "m3u8";
    else if (ext == "mpegts")
        ext = "ts";

    return ext;
}

// Origin: https://github.com/obsproject/obs-studio/blob/06642fdee48477ab85f89ff670f105affe402df7/UI/obs-app.cpp#L1771
QString generateSpecifiedFilename(const char *extension, bool noSpace, const char *format)
{
    OBSString filename = os_generate_formatted_filename(extension, !noSpace, format);
    return QString(filename);
}

// Origin: https://github.com/obsproject/obs-studio/blob/06642fdee48477ab85f89ff670f105affe402df7/UI/obs-app.cpp#L1809
void ensureDirectoryExists(QString path)
{
    path.replace('\\', '/');

    // Remove file part (also remove trailing slash)
    auto last = path.lastIndexOf('/');
    if (last < 0) {
        return;
    }

    QString directory = path.left(last);
    os_mkdirs(qUtf8Printable(directory));
}

// Origin: https://github.com/obsproject/obs-studio/blob/06642fdee48477ab85f89ff670f105affe402df7/UI/obs-app.cpp#L1779
void findBestFilename(QString &strPath, bool noSpace)
{
    int num = 2;

    if (!os_file_exists(qUtf8Printable(strPath))) {
        return;
    }

    size_t dotPos = strPath.lastIndexOf('.');
    for (;;) {
        QString testPath = strPath;
        QString numStr;

        if (noSpace) {
            numStr = QString("_%1").arg(num++);
        } else {
            numStr = QString(" (%1)").arg(num++);
        }

        testPath.insert(dotPos, numStr);

        if (!os_file_exists(qUtf8Printable(testPath))) {
            strPath = testPath;
            break;
        }
    }
}

// Origin: https://github.com/obsproject/obs-studio/blob/06642fdee48477ab85f89ff670f105affe402df7/UI/obs-app.cpp#L1888
QString getOutputFilename(const char *path, const char *container, bool noSpace, bool overwrite, const char *format)
{
    os_dir_t *dir = path && path[0] ? os_opendir(path) : nullptr;

    if (!dir) {
        return "";
    }

    os_closedir(dir);

    QString strPath;
    strPath += path;

    QChar lastChar = strPath.back();
    if (lastChar != '/' && lastChar != '\\')
        strPath += "/";

    QString ext = getFormatExt(container);
    strPath += generateSpecifiedFilename(qUtf8Printable(ext), noSpace, format);
    ensureDirectoryExists(strPath);
    if (!overwrite) {
        findBestFilename(strPath, noSpace);
    }

    return strPath;
}
