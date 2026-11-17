/* Copyright (C) 2026 BrowserPS5 contributors

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

// ImageGStreamer with Cairo, the PlayStation port's 2D library (upstream
// only has ImageGStreamerSkia.cpp). build-webkit.sh copies it next to that
// one (docs/03-video-e-gpu.md, V2).
//
// The frame comes as BGRx or BGRA (convertSampleToImage() in
// VideoFrameGStreamer.cpp), which on this little-endian machine is Cairo's
// RGB24 or ARGB32 in memory. ARGB32 is premultiplied and GStreamer's BGRA
// is not, so BGRA is premultiplied on the way. Like the Skia one, the pixels
// are copied, and the mapped frame (and the buffer pool behind it) is let
// go at once.

#include "config.h"
#include "ImageGStreamer.h"

#if ENABLE(VIDEO) && USE(GSTREAMER) && USE(CAIRO)

#include "RefPtrCairo.h"
#include <cairo.h>
#include <wtf/StdLibExtras.h>

namespace WebCore {

ImageGStreamer::ImageGStreamer(GRefPtr<GstSample>&& sample)
    : m_sample(WTF::move(sample))
{
    GstBuffer* buffer = gst_sample_get_buffer(m_sample.get());
    if (!GST_IS_BUFFER(buffer)) [[unlikely]]
        return;

    GstMappedFrame videoFrame(m_sample, GST_MAP_READ);
    if (!videoFrame)
        return;

    cairo_format_t format;
    switch (videoFrame.format()) {
    case GST_VIDEO_FORMAT_BGRx:
        format = CAIRO_FORMAT_RGB24;
        break;
    case GST_VIDEO_FORMAT_BGRA:
        format = CAIRO_FORMAT_ARGB32;
        m_hasAlpha = true;
        break;
    default:
        ASSERT_NOT_REACHED();
        return;
    }

    size_t width = videoFrame.width();
    size_t height = videoFrame.height();
    RefPtr surface = adoptRef(cairo_image_surface_create(format, width, height));
    if (cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS)
        return;

    auto source = videoFrame.planeData(0);
    size_t sourceStride = videoFrame.planeStride(0);
    size_t stride = cairo_image_surface_get_stride(surface.get());
    auto destination = unsafeMakeSpan(cairo_image_surface_get_data(surface.get()), stride * height);
    for (size_t y = 0; y < height; y++) {
        auto from = source.subspan(y * sourceStride, width * 4);
        auto to = destination.subspan(y * stride, width * 4);
        if (!m_hasAlpha) {
            memcpySpan(to, from);
            continue;
        }
        for (size_t x = 0; x < width * 4; x += 4) {
            unsigned alpha = from[x + 3];
            for (size_t c = 0; c < 3; c++)
                to[x + c] = (from[x + c] * alpha + 127) / 255;
            to[x + 3] = alpha;
        }
    }
    cairo_surface_mark_dirty(surface.get());

    m_size = { static_cast<float>(width), static_cast<float>(height) };
    m_image = WTF::move(surface);

    if (auto* cropMeta = gst_buffer_get_video_crop_meta(buffer))
        m_cropRect = FloatRect(cropMeta->x, cropMeta->y, cropMeta->width, cropMeta->height);
}

ImageGStreamer::~ImageGStreamer() = default;

} // namespace WebCore

#endif // ENABLE(VIDEO) && USE(GSTREAMER) && USE(CAIRO)
