/*
 *  Copyright (C) 2022 Igalia, S.L
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "GStreamerVideoSinkCommon.h"

#if ENABLE(VIDEO)

#include "MediaPlayerPrivateGStreamer.h"
#include <gst/app/gstappsink.h>
#include <wtf/TZoneMallocInlines.h>

using namespace WebCore;

GST_DEBUG_CATEGORY(webkit_gst_video_sink_common_debug);
#define GST_CAT_DEFAULT webkit_gst_video_sink_common_debug

class WebKitVideoSinkProbe {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(WebKitVideoSinkProbe);
public:

    WebKitVideoSinkProbe(MediaPlayerPrivateGStreamer* player)
        : m_player(player)
    {
    }

    static void deleteUserData(void* userData)
    {
        delete static_cast<WebKitVideoSinkProbe*>(userData);
    }

    void handleFlushEvent([[maybe_unused]] GstPad* pad, GstPadProbeInfo* info)
    {
        // We need to operate from the main thread, this is a requirement for usage of
        // AbortableTaskQueue start/finishAborting.
        ASSERT(isMainThread());

        if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_FLUSH_START) {
            if (!m_isFlushing) {
                GST_DEBUG_OBJECT(pad, "FLUSH_START received, aborting all pending tasks in the player sinkTaskQueue.");
                m_isFlushing = true;
                m_player->sinkTaskQueue().startAborting();
#if USE(GSTREAMER_GL)
                    GST_DEBUG_OBJECT(pad, "Flushing current buffer in response to %" GST_PTR_FORMAT, info->data);
                    m_player->flushCurrentBuffer();
#endif
            } else
                GST_DEBUG_OBJECT(pad, "Received FLUSH_START while already flushing, ignoring.");
            return;
        }

        RELEASE_ASSERT(GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_FLUSH_STOP);
        if (m_isFlushing) {
            GST_DEBUG_OBJECT(pad, "FLUSH_STOP received, allowing operation in the player sinkTaskQueue again.");
            m_isFlushing = false;
            m_player->sinkTaskQueue().finishAborting();
            return;
        }

        GST_DEBUG_OBJECT(pad, "Received FLUSH_STOP without a FLUSH_START, ignoring.");
    }

    static GstPadProbeReturn doProbe([[maybe_unused]] GstPad* pad, GstPadProbeInfo* info, gpointer userData)
    {
        auto* self = static_cast<WebKitVideoSinkProbe*>(userData);
        auto* player = self->m_player;

        // Usually flushes propagate in the main thread as a synchronous consequence of a seek.
        // However, this doesn't have to be the case:
        //
        // As a notable example, when matroskademux receives a seek before it has parsed the
        // entire file header, it stores the event and returns without flushing anything.
        // Later, the streaming thread finishes parsing the file header and handles the stored
        // seek event from that same thread. This sends a flush from the streaming thread.
        if (info->type & GST_PAD_PROBE_TYPE_EVENT_FLUSH) {
            callOnMainThreadAndWait([&] {
                self->handleFlushEvent(pad, info);
            });
        }
        if (self->m_isFlushing)
            return GST_PAD_PROBE_OK; // do not process regular (non-flush) events during a flush

        if (info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM && GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_TAG) {
            GstTagList* tagList;
            gst_event_parse_tag(GST_PAD_PROBE_INFO_EVENT(info), &tagList);
            GST_DEBUG_OBJECT(pad, "Tag event received, video orientation may need to be updated. %" GST_PTR_FORMAT, tagList);
            player->updateVideoOrientation(tagList);
        }

        if (info->type & GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM && GST_QUERY_TYPE(GST_PAD_PROBE_INFO_QUERY(info)) == GST_QUERY_ALLOCATION) {
            auto query = GST_PAD_PROBE_INFO_QUERY(info);
            gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, nullptr);

            GstCaps* caps;
            gboolean needPool;
            gst_query_parse_allocation(query, &caps, &needPool);
            if (!caps) [[unlikely]]
                return GST_PAD_PROBE_OK;
            if (!needPool)
                return GST_PAD_PROBE_OK;

            unsigned size;
#if GST_CHECK_VERSION(1, 24, 0)
            if (gst_video_is_dma_drm_caps(caps)) {
                GstVideoInfoDmaDrm drmInfo;
                if (!gst_video_info_dma_drm_from_caps(&drmInfo, caps))
                    return GST_PAD_PROBE_OK;
                size = GST_VIDEO_INFO_SIZE(&drmInfo.vinfo);
            } else
#endif
            {
                GstVideoInfo info;
                if (!gst_video_info_from_caps(&info, caps))
                    return GST_PAD_PROBE_OK;
                size = GST_VIDEO_INFO_SIZE(&info);
            }
            gst_query_add_allocation_pool(query, nullptr, size, 3, 0);
        }

#if USE(GSTREAMER_GL)
        // In some platforms (e.g. OpenMAX on the Raspberry Pi) when a resolution change occurs the
        // pipeline has to be drained before a frame with the new resolution can be decoded.
        // In this context, it's important that we don't hold references to any previous frame
        // (e.g. m_sample) so that decoding can continue.
        // We are also not supposed to keep the original frame after a flush.
        if (info->type & GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM && GST_QUERY_TYPE(GST_PAD_PROBE_INFO_QUERY(info)) == GST_QUERY_DRAIN) {
            GST_DEBUG_OBJECT(pad, "Flushing current buffer in response to %" GST_PTR_FORMAT, info->data);
            player->flushCurrentBuffer();
        }
#endif

        return GST_PAD_PROBE_OK;
    }

private:
    MediaPlayerPrivateGStreamer* m_player;
    bool m_isFlushing { false };
};

void webKitVideoSinkSetMediaPlayerPrivate(GstElement* appSink, MediaPlayerPrivateGStreamer* player)
{
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_gst_video_sink_common_debug, "webkitvideosinkcommon", 0, "WebKit Video Sink Common utilities");
    });

    g_signal_connect(appSink, "new-sample", G_CALLBACK(+[](GstElement* sink, MediaPlayerPrivateGStreamer* player) -> GstFlowReturn {
        GRefPtr<GstSample> sample = adoptGRef(gst_app_sink_pull_sample(GST_APP_SINK(sink)));
#ifndef GST_DISABLE_GST_DEBUG
        GstBuffer* buffer = gst_sample_get_buffer(sample.get());
        GST_TRACE_OBJECT(sink, "new-sample with PTS=%" GST_TIME_FORMAT, GST_TIME_ARGS(GST_BUFFER_PTS(buffer)));
#endif
        player->triggerRepaint(WTFMove(sample));
        return GST_FLOW_OK;
    }), player);
    g_signal_connect(appSink, "new-preroll", G_CALLBACK(+[](GstElement* sink, MediaPlayerPrivateGStreamer* player) -> GstFlowReturn {
        GRefPtr<GstSample> sample = adoptGRef(gst_app_sink_pull_preroll(GST_APP_SINK(sink)));
#ifndef GST_DISABLE_GST_DEBUG
        GstBuffer* buffer = gst_sample_get_buffer(sample.get());
        GST_DEBUG_OBJECT(sink, "new-preroll with PTS=%" GST_TIME_FORMAT, GST_TIME_ARGS(GST_BUFFER_PTS(buffer)));
#endif
        player->triggerRepaint(WTFMove(sample));
        return GST_FLOW_OK;
    }), player);

    GRefPtr<GstPad> pad = adoptGRef(gst_element_get_static_pad(appSink, "sink"));

    gst_pad_add_probe(pad.get(), static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_PUSH | GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM
        | GST_PAD_PROBE_TYPE_EVENT_FLUSH | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
        WebKitVideoSinkProbe::doProbe, new WebKitVideoSinkProbe(player), WebKitVideoSinkProbe::deleteUserData);

    if (!player->requiresVideoSinkCapsNotifications())
        return;

    g_signal_connect(pad.get(), "notify::caps", G_CALLBACK(+[](GstPad* videoSinkPad, GParamSpec*, MediaPlayerPrivateGStreamer* player) {
        player->videoSinkCapsChanged(videoSinkPad);
    }), player);
}

#undef GST_CAT_DEFAULT

#endif // ENABLE(VIDEO)
