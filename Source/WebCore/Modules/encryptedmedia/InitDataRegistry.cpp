/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "InitDataRegistry.h"

#if ENABLE(ENCRYPTED_MEDIA)

#include "ISOProtectionSystemSpecificHeaderBox.h"
#include <JavaScriptCore/DataView.h>
#include "NotImplemented.h"
#include "SharedBuffer.h"
#include <wtf/JSONValues.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/Base64.h>

#if USE(GSTREAMER)
#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <wtf/Scope.h>
#endif

#if HAVE(FAIRPLAYSTREAMING_CENC_INITDATA)
#include "CDMFairPlayStreaming.h"
#include "ISOFairPlayStreamingPsshBox.h"
#endif


namespace WebCore {

namespace {
    const uint32_t kCencMaxBoxSize = 64 * KB;
    // ContentEncKeyID has this EBML code [47][E2] in WebM,
    // as per spec the size of the ContentEncKeyID is encoded on 16 bits.
    // https://matroska.org/technical/specs/index.html#ContentEncKeyID/
    const uint32_t kWebMMaxContentEncKeyIDSize = 64 * KB; // 2^16
    const uint32_t kKeyIdsMinKeyIdSizeInBytes = 1;
    const uint32_t kKeyIdsMaxKeyIdSizeInBytes = 512;
}

static std::optional<Vector<Ref<SharedBuffer>>> extractKeyIDsKeyids(const SharedBuffer& buffer)
{
    // 1. Format
    // https://w3c.github.io/encrypted-media/format-registry/initdata/keyids.html#format
    if (buffer.size() > std::numeric_limits<unsigned>::max())
        return std::nullopt;
    String json { buffer.span() };

    auto value = JSON::Value::parseJSON(json);
    if (!value)
        return std::nullopt;

    auto object = value->asObject();
    if (!object)
        return std::nullopt;

    auto kidsArray = object->getArray("kids"_s);
    if (!kidsArray)
        return std::nullopt;

    Vector<Ref<SharedBuffer>> keyIDs;
    for (auto& value : *kidsArray) {
        auto keyID = value->asString();
        if (!keyID)
            continue;

        auto keyIDData = base64URLDecode(keyID);
        if (!keyIDData)
            continue;

        if (keyIDData->size() < kKeyIdsMinKeyIdSizeInBytes || keyIDData->size() > kKeyIdsMaxKeyIdSizeInBytes)
            return std::nullopt;

        keyIDs.append(SharedBuffer::create(WTFMove(*keyIDData)));
    }

    return keyIDs;
}

static RefPtr<SharedBuffer> sanitizeKeyids(const SharedBuffer& buffer)
{
    // 1. Format
    // https://w3c.github.io/encrypted-media/format-registry/initdata/keyids.html#format
    auto keyIDBuffer = extractKeyIDsKeyids(buffer);
    if (!keyIDBuffer)
        return nullptr;

    auto object = JSON::Object::create();
    auto kidsArray = JSON::Array::create();
    for (auto& buffer : keyIDBuffer.value())
        kidsArray->pushString(base64URLEncodeToString(buffer->span()));
    object->setArray("kids"_s, WTFMove(kidsArray));

    return SharedBuffer::create(object->toJSONString().utf8().span());
}

std::optional<Vector<std::unique_ptr<ISOProtectionSystemSpecificHeaderBox>>> InitDataRegistry::extractPsshBoxesFromCenc(const SharedBuffer& buffer)
{
    // 4. Common SystemID and PSSH Box Format
    // https://w3c.github.io/encrypted-media/format-registry/initdata/cenc.html#common-system
    if (buffer.size() >= kCencMaxBoxSize)
        return std::nullopt;

    unsigned offset = 0;
    Vector<std::unique_ptr<ISOProtectionSystemSpecificHeaderBox>> psshBoxes;

    auto view = JSC::DataView::create(buffer.tryCreateArrayBuffer(), offset, buffer.size());
    while (auto optionalBoxType = ISOBox::peekBox(view, offset)) {
        auto& boxTypeName = optionalBoxType.value().first;
        auto& boxSize = optionalBoxType.value().second;

        if (boxTypeName != ISOProtectionSystemSpecificHeaderBox::boxTypeName() || boxSize > buffer.size())
            return std::nullopt;

        auto systemID = ISOProtectionSystemSpecificHeaderBox::peekSystemID(view, offset);
#if HAVE(FAIRPLAYSTREAMING_CENC_INITDATA)
        if (systemID == ISOFairPlayStreamingPsshBox::fairPlaySystemID()) {
            auto fpsPssh = makeUnique<ISOFairPlayStreamingPsshBox>();
            if (!fpsPssh->read(view, offset))
                return std::nullopt;
            psshBoxes.append(WTFMove(fpsPssh));
            continue;
        }
#else
        UNUSED_PARAM(systemID);
#endif
        auto psshBox = makeUnique<ISOProtectionSystemSpecificHeaderBox>();
        if (!psshBox->read(view, offset))
            return std::nullopt;

        psshBoxes.append(WTFMove(psshBox));
    }

    return psshBoxes;
}

std::optional<Vector<Ref<SharedBuffer>>> InitDataRegistry::extractKeyIDsCenc(const SharedBuffer& buffer)
{
    Vector<Ref<SharedBuffer>> keyIDs;

    auto psshBoxes = extractPsshBoxesFromCenc(buffer);
    if (!psshBoxes)
        return std::nullopt;

    for (auto& psshBox : psshBoxes.value()) {
        ASSERT(psshBox);
        if (!psshBox)
            return std::nullopt;

#if HAVE(FAIRPLAYSTREAMING_CENC_INITDATA)
        if (auto* fpsPssh = dynamicDowncast<ISOFairPlayStreamingPsshBox>(*psshBox)) {
            FourCC scheme = fpsPssh->initDataBox().info().scheme();
            if (CDMPrivateFairPlayStreaming::validFairPlayStreamingSchemes().contains(scheme)) {
                for (const auto& request : fpsPssh->initDataBox().requests()) {
                    auto& keyID = request.requestInfo().keyID();
                    keyIDs.append(SharedBuffer::create(keyID.span()));
                }
            }
        }
#endif

        for (auto& value : psshBox->keyIDs())
            keyIDs.append(SharedBuffer::create(Vector<uint8_t> { value }));
    }

    return keyIDs;
}

#if USE(GSTREAMER)
bool isPlayReadySanitizedInitializationData(const SharedBuffer& buffer)
{
    // The protection data starts with a 10-byte PlayReady version
    // header that needs to be skipped over to avoid XML parsing
    // errors.
    auto view = StringView::fromLatin1(byteCast<char>(buffer.span().data()));
    auto startTag = view.find('<');
    if (startTag == notFound)
        return false;

    auto xmlPayload = buffer.span().subspan(startTag);
    xmlDocPtr protectionDataXML = xmlReadMemory(reinterpret_cast<const char*>(xmlPayload.data()), xmlPayload.size_bytes(), "protectionData", "utf-16", 0);
    if (!protectionDataXML)
        return false;

    xmlXPathContextPtr xpathContext = nullptr;
    xmlXPathObjectPtr xpathObject = nullptr;
    auto exitFunction = makeScopeExit([&] {
        if (xpathContext)
            xmlXPathFreeContext(xpathContext);
        if (xpathObject)
            xmlXPathFreeObject(xpathObject);
        xmlFreeDoc(protectionDataXML);
    });

    xmlNode* protectionDataRootElement = xmlDocGetRootElement(protectionDataXML);
    if (!protectionDataRootElement || protectionDataRootElement->type != XML_ELEMENT_NODE
        || xmlStrcmp(protectionDataRootElement->name, reinterpret_cast<const xmlChar*>("WRMHEADER"))
        || !protectionDataRootElement->ns)
        return false;

    xpathContext = xmlXPathNewContext(protectionDataXML);
    if (!xpathContext)
        return false;

    const xmlChar* protectionDataNamespace = protectionDataRootElement->ns->href;
    if (xmlXPathRegisterNs(xpathContext, reinterpret_cast<const xmlChar*>("prhdr"), protectionDataNamespace) < 0)
        return false;

    xpathObject = xmlXPathEvalExpression(reinterpret_cast<const xmlChar*>("//prhdr:KID"), xpathContext);
    if (!xpathObject)
        return false;

    xmlNodeSetPtr keyIDNode = xpathObject->nodesetval;
    int numberOfKeyIDs = keyIDNode ? keyIDNode->nodeNr : 0;
    if (!numberOfKeyIDs || keyIDNode->nodeTab[0]->type != XML_ELEMENT_NODE)
        return false;

    xmlChar* encodedKeyID = xmlNodeGetContent(keyIDNode->nodeTab[0]);
    auto encodedKeyIDSpan = unsafeMakeSpan(reinterpret_cast<const uint8_t*>(encodedKeyID), static_cast<size_t>(xmlStrlen(encodedKeyID)));
    std::optional<Vector<uint8_t>> decodedKeyID = base64Decode(encodedKeyIDSpan);
    xmlFree(encodedKeyID);
    if (!decodedKeyID)
        return false;

    return true;
}
#endif

RefPtr<SharedBuffer> InitDataRegistry::sanitizeCenc(const SharedBuffer& buffer)
{
    // 4. Common SystemID and PSSH Box Format
    // https://w3c.github.io/encrypted-media/format-registry/initdata/cenc.html#common-system
    if (!extractKeyIDsCenc(buffer)) {
#if USE(GSTREAMER)
        if (!isPlayReadySanitizedInitializationData(buffer))
#endif
            return nullptr;
    }

    return buffer.makeContiguous();
}

static RefPtr<SharedBuffer> sanitizeWebM(const SharedBuffer& buffer)
{
    // Check if the buffer is a valid WebM initData.
    // The WebM initData is the ContentEncKeyID, so should be less than kWebMMaxContentEncKeyIDSize.
    if (buffer.isEmpty() || buffer.size() > kWebMMaxContentEncKeyIDSize)
        return nullptr;

    return buffer.makeContiguous();
}

static std::optional<Vector<Ref<SharedBuffer>>> extractKeyIDsWebM(const SharedBuffer& buffer)
{
    Vector<Ref<SharedBuffer>> keyIDs;
    RefPtr<SharedBuffer> sanitizedBuffer = sanitizeWebM(buffer);
    if (!sanitizedBuffer)
        return std::nullopt;

    // 1. Format
    // https://w3c.github.io/encrypted-media/format-registry/initdata/webm.html#format
    keyIDs.append(sanitizedBuffer.releaseNonNull());
    return keyIDs;
}

InitDataRegistry& InitDataRegistry::shared()
{
    static NeverDestroyed<InitDataRegistry> registry;
    return registry.get();
}

InitDataRegistry::InitDataRegistry()
{
    registerInitDataType("keyids"_s, { sanitizeKeyids, extractKeyIDsKeyids });
    registerInitDataType("cenc"_s, { sanitizeCenc, extractKeyIDsCenc });
    registerInitDataType("webm"_s, { sanitizeWebM, extractKeyIDsWebM });
}

InitDataRegistry::~InitDataRegistry() = default;

RefPtr<SharedBuffer> InitDataRegistry::sanitizeInitData(const AtomString& initDataType, const SharedBuffer& buffer)
{
    auto iter = m_types.find(initDataType);
    if (iter == m_types.end() || !iter->value.sanitizeInitData)
        return nullptr;
    return iter->value.sanitizeInitData(buffer);
}

std::optional<Vector<Ref<SharedBuffer>>> InitDataRegistry::extractKeyIDs(const AtomString& initDataType, const SharedBuffer& buffer)
{
    auto iter = m_types.find(initDataType);
    if (iter == m_types.end() || !iter->value.sanitizeInitData)
        return std::nullopt;
    return iter->value.extractKeyIDs(buffer);
}

void InitDataRegistry::registerInitDataType(const AtomString& initDataType, InitDataTypeCallbacks&& callbacks)
{
    ASSERT(!m_types.contains(initDataType));
    m_types.set(initDataType, WTFMove(callbacks));
}

const AtomString& InitDataRegistry::cencName()
{
    static MainThreadNeverDestroyed<const AtomString> sinf { MAKE_STATIC_STRING_IMPL("cenc") };
    return sinf;
}

const AtomString& InitDataRegistry::keyidsName()
{
    static MainThreadNeverDestroyed<const AtomString> sinf { MAKE_STATIC_STRING_IMPL("keyids") };
    return sinf;
}

const AtomString& InitDataRegistry::webmName()
{
    static MainThreadNeverDestroyed<const AtomString> sinf { MAKE_STATIC_STRING_IMPL("webm") };
    return sinf;
}

}

#endif // ENABLE(ENCRYPTED_MEDIA)
