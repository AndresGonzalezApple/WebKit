/*
 * Copyright (C) 2021 Igalia S.L. All rights reserved.
 * Copyright (C) 2022 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
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
 */

#include "config.h"

#if USE(AVIF)

#include "AVIFImageReader.h"
#include "AVIFImageDecoder.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(AVIFImageReader);

AVIFImageReader::AVIFImageReader(RefPtr<AVIFImageDecoder>&& decoder)
    : m_decoder(WTFMove(decoder))
    , m_avifDecoder(avifDecoderCreate())
{
    // Allow the PixelInformationProperty ('pixi') to be missing in AV1 image
    // items. libheif v1.11.0 or older does not add the 'pixi' item property to
    // AV1 image items. (This issue has been corrected in libheif v1.12.0.).
    // See the definition of AVIF_STRICT_PIXI_REQUIRED in avif.h.
    m_avifDecoder->strictFlags &= ~AVIF_STRICT_PIXI_REQUIRED;
}

AVIFImageReader::~AVIFImageReader() = default;

bool AVIFImageReader::parseHeader(const SharedBuffer& data, bool allDataReceived)
{
    auto dataSpan = data.span();
    if (avifDecoderSetIOMemory(m_avifDecoder.get(), dataSpan.data(), dataSpan.size()) != AVIF_RESULT_OK)
        return allDataReceived ? m_decoder->setFailed() : false;

    if (avifDecoderParse(m_avifDecoder.get()) != AVIF_RESULT_OK
        || avifDecoderNextImage(m_avifDecoder.get()) != AVIF_RESULT_OK)
        return allDataReceived ? m_decoder->setFailed() : false;

    if (!m_dataParsed && allDataReceived)
        m_dataParsed = true;

    const avifImage* firstImage = m_avifDecoder->image;
    m_decoder->setSize(IntSize(firstImage->width, firstImage->height));
    return true;
}

void AVIFImageReader::decodeFrame(size_t frameIndex, ScalableImageDecoderFrame& buffer, const SharedBuffer& data)
{
    if (m_decoder->failed())
        return;

    if (!m_dataParsed) {
        auto dataSpan = data.span();
        if (avifDecoderSetIOMemory(m_avifDecoder.get(), dataSpan.data(), dataSpan.size()) != AVIF_RESULT_OK) {
            m_decoder->setFailed();
            return;
        }

        if (avifDecoderParse(m_avifDecoder.get()) != AVIF_RESULT_OK) {
            m_decoder->setFailed();
            return;
        }

        if (m_decoder->isAllDataReceived())
            m_dataParsed = true;
    }

    if (avifDecoderNthImage(m_avifDecoder.get(), frameIndex) != AVIF_RESULT_OK) {
        m_decoder->setFailed();
        return;
    }

    IntSize imageSize(m_decoder->size());
    if (buffer.isInvalid() && !buffer.initialize(imageSize, m_decoder->premultiplyAlpha())) {
        m_decoder->setFailed();
        return;
    }

    buffer.setDecodingStatus(DecodingStatus::Partial);

    avifRGBImage decodedRGBImage;
    avifRGBImageSetDefaults(&decodedRGBImage, m_avifDecoder->image);

    decodedRGBImage.depth = 8;
    decodedRGBImage.alphaPremultiplied = m_decoder->premultiplyAlpha();
    decodedRGBImage.format = AVIF_RGB_FORMAT_BGRA;
    decodedRGBImage.rowBytes = imageSize.width() * sizeof(uint32_t);
    decodedRGBImage.pixels = reinterpret_cast<uint8_t*>(buffer.backingStore()->pixelsStartingAt(0, 0).data());

    if (avifImageYUVToRGB(m_avifDecoder->image, &decodedRGBImage) != AVIF_RESULT_OK) {
        m_decoder->setFailed();
        return;
    }

    buffer.setHasAlpha(avifRGBFormatHasAlpha(decodedRGBImage.format));
    buffer.setDuration(Seconds(m_avifDecoder->imageTiming.duration));
    buffer.setDecodingStatus(DecodingStatus::Complete);
}

size_t AVIFImageReader::imageCount() const
{
    return m_avifDecoder->imageCount;
}

} // namespace WebCore

#endif // USE(AVIF)
