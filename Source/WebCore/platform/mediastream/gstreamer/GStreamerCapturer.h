/*
 * Copyright (C) 2018 Metrological Group B.V.
 * Copyright (C) 2020 Igalia S.L.
 * Author: Thibault Saunier <tsaunier@igalia.com>
 * Author: Alejandro G. Castro <alex@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "GStreamerCaptureDevice.h"
#include "GStreamerCommon.h"
#include "PipeWireCaptureDevice.h"

#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/WeakHashSet.h>

namespace WebCore {
class GStreamerCapturerObserver;
}

namespace WTF {
template<typename T> struct IsDeprecatedWeakRefSmartPointerException;
template<> struct IsDeprecatedWeakRefSmartPointerException<WebCore::GStreamerCapturerObserver> : std::true_type { };
}

namespace WebCore {

class GStreamerCapturerObserver : public CanMakeWeakPtr<GStreamerCapturerObserver> {
public:
    virtual ~GStreamerCapturerObserver();

    virtual void sourceCapsChanged(const GstCaps*) { }
    virtual void captureEnded() { }
    virtual void captureDeviceUpdated(const GStreamerCaptureDevice&) { }
};

class GStreamerCapturer : public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<GStreamerCapturer> {
public:
    GStreamerCapturer(GStreamerCaptureDevice&&, GRefPtr<GstCaps>&&);
    GStreamerCapturer(const PipeWireCaptureDevice&);
    virtual ~GStreamerCapturer();

    void tearDown(bool disconnectSignals = true);

    void setDevice(std::optional<GStreamerCaptureDevice>&&);

    void addObserver(GStreamerCapturerObserver&);
    void removeObserver(GStreamerCapturerObserver&);
    void forEachObserver(NOESCAPE const Function<void(GStreamerCapturerObserver&)>&);

    virtual void setupPipeline();
    void start();
    void stop();
    bool isStopped() const;
    WARN_UNUSED_RETURN GRefPtr<GstCaps> caps();

    std::pair<GstClockTime, GstClockTime> queryLatency();

    GstElement* makeElement(ASCIILiteral factoryName);
    virtual GstElement* createSource();
    GstElement* source() { return m_src.get();  }
    virtual const char* name() = 0;

    GstElement* sink() const { return m_sink.get(); }

    GstElement* pipeline() const { return m_pipeline.get(); }
    virtual GstElement* createConverter() = 0;

    bool isInterrupted() const;
    void setInterrupted(bool);

    CaptureDevice::DeviceType deviceType() const { return m_deviceType; }
    const String& devicePersistentId() const { return m_device ? m_device->persistentId() : emptyString(); }

    void stopDevice(bool disconnectSignals);

protected:
    GRefPtr<GstElement> m_sink;
    GRefPtr<GstElement> m_src;
    GRefPtr<GstElement> m_valve;
    GRefPtr<GstElement> m_capsfilter;
    std::optional<GStreamerCaptureDevice> m_device { };
    std::optional<PipeWireCaptureDevice> m_pipewireDevice { };
    GRefPtr<GstCaps> m_caps;
    GRefPtr<GstElement> m_pipeline;

private:
    CaptureDevice::DeviceType m_deviceType;
    WeakHashSet<GStreamerCapturerObserver> m_observers;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
