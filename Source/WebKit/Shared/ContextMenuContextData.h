/*
 * Copyright (C) 2014-2020 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#pragma once

#if ENABLE(CONTEXT_MENUS)

#include "WebContextMenuItemData.h"
#include "WebHitTestResultData.h"
#include <WebCore/ContextMenuContext.h>
#include <WebCore/ElementContext.h>

#if ENABLE(SERVICE_CONTROLS)
#include <WebCore/AttributedString.h>
#endif

namespace IPC {
class Decoder;
class Encoder;
}

namespace WebKit {

class ContextMenuContextData {
public:
    using Type = WebCore::ContextMenuContext::Type;

    ContextMenuContextData();
    ContextMenuContextData(const WebCore::IntPoint& menuLocation, const Vector<WebKit::WebContextMenuItemData>& menuItems, const WebCore::ContextMenuContext&);

    ContextMenuContextData(WebCore::ContextMenuContext::Type
        , WebCore::IntPoint&& menuLocation
        , Vector<WebContextMenuItemData>&& menuItems
        , std::optional<WebKit::WebHitTestResultData>&&
        , String&& selectedText
#if ENABLE(SERVICE_CONTROLS)
        , std::optional<WebCore::ShareableBitmapHandle>&& controlledImageHandle
        , WebCore::AttributedString&& controlledSelection
        , Vector<String>&& selectedTelephoneNumbers
        , bool selectionIsEditable
        , WebCore::IntRect&& controlledImageBounds
        , String&& controlledImageAttachmentID
        , std::optional<WebCore::ElementContext>&& controlledImageElementContext
        , String&& controlledImageMIMEType
#endif // ENABLE(SERVICE_CONTROLS)
#if ENABLE(CONTEXT_MENU_QR_CODE_DETECTION)
        , std::optional<WebCore::ShareableBitmapHandle>&& potentialQRCodeNodeSnapshotImageHandle
        , std::optional<WebCore::ShareableBitmapHandle>&& potentialQRCodeViewportSnapshotImageHandle
#endif // ENABLE(CONTEXT_MENU_QR_CODE_DETECTION)
        , bool hasEntireImage
        , bool allowsFollowingLink
        , bool allowsFollowingImageURL
    );

    Type type() const { return m_type; }
    const WebCore::IntPoint& menuLocation() const { return m_menuLocation; }
    void setMenuLocation(WebCore::IntPoint menuLocation) { m_menuLocation = menuLocation; }
    const Vector<WebKit::WebContextMenuItemData>& menuItems() const { return m_menuItems; }

    const std::optional<WebHitTestResultData>& webHitTestResultData() const { return m_webHitTestResultData; }
    const String& selectedText() const { return m_selectedText; }

    bool hasEntireImage() const { return m_hasEntireImage; }
    bool allowsFollowingLink() const { return m_allowsFollowingLink; }
    bool allowsFollowingImageURL() const { return m_allowsFollowingImageURL; }

#if ENABLE(SERVICE_CONTROLS)
    ContextMenuContextData(const WebCore::IntPoint& menuLocation, WebCore::AttributedString&& controlledSelection, const Vector<String>& selectedTelephoneNumbers, bool isEditable)
        : m_type(Type::ServicesMenu)
        , m_menuLocation(menuLocation)
        , m_controlledSelection(WTFMove(controlledSelection))
        , m_selectedTelephoneNumbers(selectedTelephoneNumbers)
        , m_selectionIsEditable(isEditable)
    {
    }

    ContextMenuContextData(const WebCore::IntPoint& menuLocation, bool isEditable, const WebCore::IntRect& imageRect, const String& attachmentID, const String& sourceImageMIMEType)
        : m_type(Type::ServicesMenu)
        , m_menuLocation(menuLocation)
        , m_selectionIsEditable(isEditable)
        , m_controlledImageBounds(imageRect)
        , m_controlledImageAttachmentID(attachmentID)
        , m_controlledImageMIMEType(sourceImageMIMEType)
    {
    }

    ContextMenuContextData(const WebCore::IntPoint& menuLocation, WebCore::Image&, bool isEditable, const WebCore::IntRect& imageRect, const String& attachmentID, std::optional<WebCore::ElementContext>&&, const String& sourceImageMIMEType);

    WebCore::ShareableBitmap* controlledImage() const { return m_controlledImage.get(); }
    std::optional<WebCore::ShareableBitmap::Handle> createControlledImageReadOnlyHandle() const;

    const WebCore::AttributedString& controlledSelection() const { return m_controlledSelection; }
    const Vector<String>& selectedTelephoneNumbers() const { return m_selectedTelephoneNumbers; }

    bool selectionIsEditable() const { return m_selectionIsEditable; }

    bool isServicesMenu() const { return m_type == ContextMenuContextData::Type::ServicesMenu; }
    bool controlledDataIsEditable() const;
    WebCore::IntRect controlledImageBounds() const { return m_controlledImageBounds; };
    String controlledImageAttachmentID() const { return m_controlledImageAttachmentID; };
    std::optional<WebCore::ElementContext> controlledImageElementContext() const { return m_controlledImageElementContext; }
    String controlledImageMIMEType() const { return m_controlledImageMIMEType; }
#endif // ENABLE(SERVICE_CONTROLS)

#if ENABLE(CONTEXT_MENU_QR_CODE_DETECTION)
    WebCore::ShareableBitmap* potentialQRCodeNodeSnapshotImage() const { return m_potentialQRCodeNodeSnapshotImage.get(); }
    std::optional<WebCore::ShareableBitmap::Handle> createPotentialQRCodeNodeSnapshotImageReadOnlyHandle() const;
    WebCore::ShareableBitmap* potentialQRCodeViewportSnapshotImage() const { return m_potentialQRCodeViewportSnapshotImage.get(); }
    std::optional<WebCore::ShareableBitmap::Handle> createPotentialQRCodeViewportSnapshotImageReadOnlyHandle() const;

    const String& qrCodePayloadString() const { return m_qrCodePayloadString; }
    void setQRCodePayloadString(const String& string) { m_qrCodePayloadString = string; }
#endif

private:
    Type m_type;

    WebCore::IntPoint m_menuLocation;
    Vector<WebKit::WebContextMenuItemData> m_menuItems;

    std::optional<WebHitTestResultData> m_webHitTestResultData;
    String m_selectedText;
    bool m_hasEntireImage { false };
    bool m_allowsFollowingLink { false };
    bool m_allowsFollowingImageURL { false };

#if ENABLE(SERVICE_CONTROLS)
    void setImage(WebCore::Image&);
    
    RefPtr<WebCore::ShareableBitmap> m_controlledImage;
    WebCore::AttributedString m_controlledSelection;
    Vector<String> m_selectedTelephoneNumbers;
    bool m_selectionIsEditable;
    WebCore::IntRect m_controlledImageBounds;
    String m_controlledImageAttachmentID;
    std::optional<WebCore::ElementContext> m_controlledImageElementContext;
    String m_controlledImageMIMEType;
#endif

#if ENABLE(CONTEXT_MENU_QR_CODE_DETECTION)
    void setPotentialQRCodeNodeSnapshotImage(WebCore::Image&);
    void setPotentialQRCodeViewportSnapshotImage(WebCore::Image&);

    RefPtr<WebCore::ShareableBitmap> m_potentialQRCodeNodeSnapshotImage;
    RefPtr<WebCore::ShareableBitmap> m_potentialQRCodeViewportSnapshotImage;

    String m_qrCodePayloadString;
#endif
};

} // namespace WebKit

#endif // ENABLE(CONTEXT_MENUS)
