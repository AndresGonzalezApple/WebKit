/*
 * Copyright (C) 2014 Igalia S.L.
 * Copyright (C) 2016-2018 Apple Inc. All rights reserved.
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
#include "UserMediaPermissionRequestManager.h"

#if ENABLE(MEDIA_STREAM)

#include "FrameInfoData.h"
#include "Logging.h"
#include "MessageSenderInlines.h"
#include "WebFrame.h"
#include "WebPage.h"
#include "WebPageProxyMessages.h"
#include <WebCore/CaptureDevice.h>
#include <WebCore/CaptureDeviceWithCapabilities.h>
#include <WebCore/DocumentInlines.h>
#include <WebCore/FrameDestructionObserverInlines.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/MediaConstraints.h>
#include <WebCore/Page.h>
#include <WebCore/SecurityOrigin.h>
#include <WebCore/SecurityOriginData.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {
using namespace WebCore;

WTF_MAKE_TZONE_ALLOCATED_IMPL(UserMediaPermissionRequestManager);

UserMediaPermissionRequestManager::UserMediaPermissionRequestManager(WebPage& page)
    : m_page(page)
{
}

void UserMediaPermissionRequestManager::ref() const
{
    m_page->ref();
}

void UserMediaPermissionRequestManager::deref() const
{
    m_page->deref();
}

Ref<WebPage> UserMediaPermissionRequestManager::protectedPage() const
{
    return m_page.get();
}

void UserMediaPermissionRequestManager::startUserMediaRequest(UserMediaRequest& request)
{
    RefPtr document = request.document();
    RefPtr frame = document ? document->frame() : nullptr;

    if (!frame || !document->page()) {
        request.deny(MediaAccessDenialReason::OtherFailure, emptyString());
        return;
    }

    if (document->page()->canStartMedia()) {
        sendUserMediaRequest(request);
        return;
    }

    auto& pendingRequests = m_pendingUserMediaRequests.add(document, Vector<Ref<UserMediaRequest>>()).iterator->value;
    if (pendingRequests.isEmpty())
        document->addMediaCanStartListener(*this);
    pendingRequests.append(request);
}

void UserMediaPermissionRequestManager::sendUserMediaRequest(UserMediaRequest& userRequest)
{
    RefPtr frame = userRequest.document() ? userRequest.document()->frame() : nullptr;
    if (!frame) {
        userRequest.deny(MediaAccessDenialReason::OtherFailure, emptyString());
        return;
    }

    m_ongoingUserMediaRequests.add(userRequest.identifier(), userRequest);

    auto webFrame = WebFrame::fromCoreFrame(*frame);
    ASSERT(webFrame);

    RefPtr topLevelDocumentOrigin = userRequest.topLevelDocumentOrigin();
    protectedPage()->send(Messages::WebPageProxy::RequestUserMediaPermissionForFrame(userRequest.identifier(), webFrame->info(), userRequest.userMediaDocumentOrigin()->data(), topLevelDocumentOrigin->data(), userRequest.request()));
}

void UserMediaPermissionRequestManager::cancelUserMediaRequest(UserMediaRequest& request)
{
    if (auto removedRequest = m_ongoingUserMediaRequests.take(request.identifier()))
        return;
        
    RefPtr document = request.document();
    if (!document)
        return;
    
    auto iterator = m_pendingUserMediaRequests.find(document);
    if (iterator == m_pendingUserMediaRequests.end())
        return;

    auto& pendingRequests = iterator->value;
    pendingRequests.removeFirstMatching([&request](auto& item) {
        return &request == item.ptr();
    });

    if (!pendingRequests.isEmpty())
        return;
    
    document->removeMediaCanStartListener(*this);
    m_pendingUserMediaRequests.remove(iterator);
}

void UserMediaPermissionRequestManager::mediaCanStart(Document& document)
{
    ASSERT(document.page()->canStartMedia());

    auto pendingRequests = m_pendingUserMediaRequests.take(&document);
    for (auto& pendingRequest : pendingRequests)
        sendUserMediaRequest(pendingRequest);
}

void UserMediaPermissionRequestManager::userMediaAccessWasGranted(UserMediaRequestIdentifier requestID, CaptureDevice&& audioDevice, CaptureDevice&& videoDevice, WebCore::MediaDeviceHashSalts&& deviceIdentifierHashSalts, CompletionHandler<void()>&& completionHandler)
{
    auto request = m_ongoingUserMediaRequests.take(requestID);
    if (!request) {
        completionHandler();
        return;
    }

    request->allow(WTFMove(audioDevice), WTFMove(videoDevice), WTFMove(deviceIdentifierHashSalts), WTFMove(completionHandler));
}

void UserMediaPermissionRequestManager::userMediaAccessWasDenied(UserMediaRequestIdentifier requestID, MediaAccessDenialReason reason, String&& message, MediaConstraintType invalidConstraint)
{
    auto request = m_ongoingUserMediaRequests.take(requestID);
    if (!request)
        return;

    request->deny(reason, WTFMove(message),  invalidConstraint);
}

void UserMediaPermissionRequestManager::enumerateMediaDevices(Document& document, CompletionHandler<void(Vector<CaptureDeviceWithCapabilities>&&, MediaDeviceHashSalts&&)>&& completionHandler)
{
    RefPtr frame = document.frame();
    if (!frame) {
        completionHandler({ }, { });
        return;
    }

    protectedPage()->sendWithAsyncReply(Messages::WebPageProxy::EnumerateMediaDevicesForFrame { WebFrame::fromCoreFrame(*frame)->frameID(), document.securityOrigin().data(), document.topOrigin().data() }, WTFMove(completionHandler));
}

#if USE(GSTREAMER)
void UserMediaPermissionRequestManager::updateCaptureDevices(ShouldNotify shouldNotify)
{
    WebCore::RealtimeMediaSourceCenter::singleton().getMediaStreamDevices([weakThis = WeakPtr { *this }, this, shouldNotify](auto&& newDevices) mutable {
        if (!weakThis)
            return;

        if (!haveDevicesChanged(m_captureDevices, newDevices))
            return;

        m_captureDevices = WTFMove(newDevices);
        if (shouldNotify == ShouldNotify::Yes)
            captureDevicesChanged();
    });
}

void UserMediaPermissionRequestManager::devicesChanged()
{
    updateCaptureDevices(ShouldNotify::Yes);
}
#endif

UserMediaClient::DeviceChangeObserverToken UserMediaPermissionRequestManager::addDeviceChangeObserver(Function<void()>&& observer)
{
    auto identifier = UserMediaClient::DeviceChangeObserverToken::generate();
    m_deviceChangeObserverMap.add(identifier, WTFMove(observer));

    if (!m_monitoringDeviceChange) {
        m_monitoringDeviceChange = true;
#if USE(GSTREAMER)
        updateCaptureDevices(ShouldNotify::No);
        WebCore::RealtimeMediaSourceCenter::singleton().addDevicesChangedObserver(*this);
#else
        protectedPage()->send(Messages::WebPageProxy::BeginMonitoringCaptureDevices());
#endif
    }
    return identifier;
}

void UserMediaPermissionRequestManager::removeDeviceChangeObserver(UserMediaClient::DeviceChangeObserverToken token)
{
    bool wasRemoved = m_deviceChangeObserverMap.remove(token);
    ASSERT_UNUSED(wasRemoved, wasRemoved);
}

void UserMediaPermissionRequestManager::updateCaptureState(const WebCore::Document& document, bool isActive, WebCore::MediaProducerMediaCaptureKind kind, CompletionHandler<void(std::optional<WebCore::Exception>&&)>&& completionHandler)
{
    protectedPage()->updateCaptureState(document, isActive, kind, WTFMove(completionHandler));
}

void UserMediaPermissionRequestManager::captureDevicesChanged()
{
    // When new media input and/or output devices are made available, or any available input and/or
    // output device becomes unavailable, the User Agent MUST run the following steps in browsing
    // contexts where at least one of the following criteria are met, but in no other contexts:

    // * The permission state of the "device-info" permission is "granted",
    // * any of the input devices are attached to an active MediaStream in the browsing context, or
    // * the active document is fully active and has focus.

    auto identifiers = m_deviceChangeObserverMap.keys();
    for (auto& identifier : identifiers) {
        auto iterator = m_deviceChangeObserverMap.find(identifier);
        if (iterator != m_deviceChangeObserverMap.end())
            (iterator->value)();
    }
}

} // namespace WebKit

#endif // ENABLE(MEDIA_STREAM)
