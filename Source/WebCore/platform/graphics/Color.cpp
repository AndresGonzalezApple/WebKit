/*
 * Copyright (C) 2003-2021 Apple Inc. All rights reserved.
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
#include "Color.h"

#include "ColorLuminance.h"
#include "ColorSerialization.h"
#include <cmath>
#include <wtf/Assertions.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/TextStream.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(Color);

static constexpr auto lightenedBlack = SRGBA<uint8_t> { 84, 84, 84 };
static constexpr auto darkenedWhite = SRGBA<uint8_t> { 171, 171, 171 };

Color::Color(std::optional<ColorDataForIPC>&& colorData)
{
    if (colorData) {
        OptionSet<FlagsIncludingPrivate> flags;
        if (colorData->isSemantic)
            flags.add(FlagsIncludingPrivate::Semantic);
        if (colorData->usesFunctionSerialization)
            flags.add(FlagsIncludingPrivate::UseColorFunctionSerialization);

        WTF::switchOn(colorData->data,
            [&] (const PackedColor::RGBA& d) { setColor(asSRGBA(d), flags); },
            [&] (const OutOfLineColorDataForIPC& d) {
                setOutOfLineComponents(OutOfLineComponents::create({ d.c1, d.c2, d.c3, d.alpha }), d.colorSpace, flags);
            }
        );
    }
}

std::optional<ColorDataForIPC> Color::data() const
{
    if (!isValid())
        return std::nullopt;

    if (isOutOfLine()) {
        auto [c1, c2, c3, alpha] = asOutOfLine().unresolvedComponents();

        OutOfLineColorDataForIPC oolcd = {
            .colorSpace = colorSpace(),
            .c1 = c1,
            .c2 = c2,
            .c3 = c3,
            .alpha = alpha
        };

        return { {
            .isSemantic = flags().contains(FlagsIncludingPrivate::Semantic),
            .usesFunctionSerialization = flags().contains(FlagsIncludingPrivate::UseColorFunctionSerialization),
            .data = oolcd
        } };

    } else {
        return { {
            .isSemantic = flags().contains(FlagsIncludingPrivate::Semantic),
            .usesFunctionSerialization = flags().contains(FlagsIncludingPrivate::UseColorFunctionSerialization),
            .data = asPackedInline()
        } };
    };
};

Color Color::lightened() const
{
    // Hardcode this common case for speed.
    if (isInline() && asInline() == black)
        return lightenedBlack;

    auto [r, g, b, a] = toColorTypeLossy<SRGBA<float>>().resolved();
    float v = std::max({ r, g, b });

    if (v == 0.0f)
        return lightenedBlack.colorWithAlphaByte(alphaByte());

    float multiplier = std::min(1.0f, v + 0.33f) / v;

    return convertColor<SRGBA<uint8_t>>(SRGBA<float> { multiplier * r, multiplier * g, multiplier * b, a });
}

Color Color::darkened() const
{
    // Hardcode this common case for speed.
    if (isInline() && asInline() == white)
        return darkenedWhite;
    
    auto [r, g, b, a] = toColorTypeLossy<SRGBA<float>>().resolved();

    float v = std::max({ r, g, b });
    float multiplier = std::max(0.0f, (v - 0.33f) / v);

    return convertColor<SRGBA<uint8_t>>(SRGBA<float> { multiplier * r, multiplier * g, multiplier * b, a });
}

double Color::lightness() const
{
    // FIXME: Replace remaining uses with luminance.
    auto [r, g, b, a] = toColorTypeLossy<SRGBA<float>>().resolved();
    auto [min, max] = std::minmax({ r, g, b });
    return 0.5 * (max + min);
}

double Color::luminance() const
{
    return WebCore::relativeLuminance(*this);
}

bool Color::anyComponentIsNone() const
{
    return callOnUnderlyingType([&]<typename ColorType> (const ColorType& underlyingColor) {
        if constexpr (std::is_same_v<ColorType, SRGBA<uint8_t>>)
            return false;
        else
            return underlyingColor.unresolved().anyComponentIsNone();
    });
}

Color Color::colorWithAlpha(float alpha) const
{
    return callOnUnderlyingType([&] (const auto& underlyingColor) -> Color {
        auto result = colorWithOverriddenAlpha(underlyingColor, alpha);

        // FIXME: Why is preserving the semantic bit desired and/or correct here?
        if (isSemantic())
            return { result, Flags::Semantic };

        return { result };
    });
}

Color Color::invertedColorWithAlpha(float alpha) const
{
    return callOnUnderlyingType([&]<typename ColorType> (const ColorType& underlyingColor) -> Color {
        // FIXME: Determine if there is a meaningful understanding of inversion that works
        // better for non-invertible color types like Lab or consider removing this in favor
        // of alternatives.
        if constexpr (ColorType::Model::isInvertible)
            return invertedColorWithOverriddenAlpha(underlyingColor, alpha);
        else
            return invertedColorWithOverriddenAlpha(convertColor<SRGBA<float>>(underlyingColor), alpha);
    });
}

Color Color::semanticColor() const
{
    if (isSemantic())
        return *this;
    
    if (isOutOfLine())
        return { protectedAsOutOfLine(), colorSpace(), Flags::Semantic };
    return { asInline(), Flags::Semantic };
}

ColorComponents<float, 4> Color::toResolvedColorComponentsInColorSpace(ColorSpace outputColorSpace) const
{
    auto [inputColorSpace, components] = colorSpaceAndResolvedColorComponents();
    return convertAndResolveColorComponents(inputColorSpace, components, outputColorSpace);
}

ColorComponents<float, 4> Color::toResolvedColorComponentsInColorSpace(const DestinationColorSpace& outputColorSpace) const
{
    auto [inputColorSpace, components] = colorSpaceAndResolvedColorComponents();
    return convertAndResolveColorComponents(inputColorSpace, components, outputColorSpace);
}

std::pair<ColorSpace, ColorComponents<float, 4>> Color::colorSpaceAndResolvedColorComponents() const
{
    if (isOutOfLine())
        return { colorSpace(), resolveColorComponents(protectedAsOutOfLine()->resolvedComponents()) };
    return { ColorSpace::SRGB, asColorComponents(convertColor<SRGBA<float>>(asInline()).resolved()) };
}

bool Color::isBlackColor(const Color& color)
{
    return color.callOnUnderlyingType([] (const auto& underlyingColor) {
        return WebCore::isBlack(underlyingColor);
    });
}

bool Color::isWhiteColor(const Color& color)
{
    return color.callOnUnderlyingType([] (const auto& underlyingColor) {
        return WebCore::isWhite(underlyingColor);
    });
}

Color::DebugRGBA Color::debugRGBA() const
{
    auto [r, g, b, a] = toColorTypeLossy<SRGBA<uint8_t>>().resolved();
    return { r, g, b, a };
}

String Color::debugDescription() const
{
    return serializationForRenderTreeAsText(*this);
}

TextStream& operator<<(TextStream& ts, const Color& color)
{
    return ts << serializationForRenderTreeAsText(color);
}

} // namespace WebCore
