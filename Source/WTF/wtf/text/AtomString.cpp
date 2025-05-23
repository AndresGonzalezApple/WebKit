/*
 * Copyright (C) 2004-2017 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Patrick Gansterer <paroga@paroga.com>
 * Copyright (C) 2012 Google Inc. All rights reserved.
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
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"
#include <wtf/text/AtomString.h>

#include <mutex>
#include <wtf/dtoa.h>
#include <wtf/text/IntegerToStringConversion.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/unicode/CharacterNames.h>

namespace WTF {

const StaticAtomString nullAtomData { nullptr };
const StaticAtomString emptyAtomData { &StringImpl::s_emptyAtomString };

template<AtomString::CaseConvertType type>
ALWAYS_INLINE AtomString AtomString::convertASCIICase() const
{
    StringImpl* impl = this->impl();
    if (!impl) [[unlikely]]
        return nullAtom();

    // Convert short strings without allocating a new StringImpl, since
    // there's a good chance these strings are already in the atom
    // string table and so no memory allocation will be required.
    unsigned length;
    const unsigned localBufferSize = 100;
    if (impl->is8Bit() && (length = impl->length()) <= localBufferSize) {
        auto characters = impl->span8();
        unsigned failingIndex;
        for (unsigned i = 0; i < length; ++i) {
            if constexpr (type == CaseConvertType::Lower) {
                if (isASCIIUpper(characters[i])) [[unlikely]] {
                    failingIndex = i;
                    goto SlowPath;
                }
            } else {
                if (isASCIILower(characters[i])) [[likely]] {
                    failingIndex = i;
                    goto SlowPath;
                }
            }
        }
        return *this;
SlowPath:
        std::array<LChar, localBufferSize> localBuffer;
        for (unsigned i = 0; i < failingIndex; ++i)
            localBuffer[i] = characters[i];
        for (unsigned i = failingIndex; i < length; ++i)
            localBuffer[i] = type == CaseConvertType::Lower ? toASCIILower(characters[i]) : toASCIIUpper(characters[i]);
        return std::span<const LChar> { localBuffer }.first(length);
    }

    Ref<StringImpl> convertedString = type == CaseConvertType::Lower ? impl->convertToASCIILowercase() : impl->convertToASCIIUppercase();
    if (convertedString.ptr() == impl) [[likely]]
        return *this;

    AtomString result;
    result.m_string = AtomStringImpl::add(convertedString.ptr());
    return result;
}

AtomString AtomString::convertToASCIILowercase() const
{
    return convertASCIICase<CaseConvertType::Lower>();
}

AtomString AtomString::convertToASCIIUppercase() const
{
    return convertASCIICase<CaseConvertType::Upper>();
}

AtomString AtomString::number(int number)
{
    return numberToStringSigned<AtomString>(number);
}

AtomString AtomString::number(unsigned number)
{
    return numberToStringUnsigned<AtomString>(number);
}

AtomString AtomString::number(unsigned long number)
{
    return numberToStringUnsigned<AtomString>(number);
}

AtomString AtomString::number(unsigned long long number)
{
    return numberToStringUnsigned<AtomString>(number);
}

AtomString AtomString::number(float number)
{
    NumberToStringBuffer buffer;
    auto span = numberToStringAndSize(number, buffer);
    return AtomString { byteCast<LChar>(span) };
}

AtomString AtomString::number(double number)
{
    NumberToStringBuffer buffer;
    auto span = numberToStringAndSize(number, buffer);
    return AtomString { byteCast<LChar>(span) };
}

AtomString AtomString::fromUTF8Internal(std::span<const char> characters)
{
    ASSERT(!characters.empty());
    return AtomStringImpl::add(byteCast<char8_t>(characters));
}

#ifndef NDEBUG

void AtomString::show() const
{
    m_string.show();
}

#endif

static inline StringBuilder replaceUnpairedSurrogatesWithReplacementCharacterInternal(StringView view)
{
    // Slow path: https://infra.spec.whatwg.org/#javascript-string-convert
    // Replaces unpaired surrogates with the replacement character.
    StringBuilder result;
    result.reserveCapacity(view.length());
    for (auto codePoint : view.codePoints()) {
        if (U_IS_SURROGATE(codePoint))
            result.append(replacementCharacter);
        else
            result.append(codePoint);
    }
    return result;
}

AtomString replaceUnpairedSurrogatesWithReplacementCharacter(AtomString&& string)
{
    // Fast path for the case where there are no unpaired surrogates.
    if (!hasUnpairedSurrogate(string)) [[likely]]
        return WTFMove(string);
    return replaceUnpairedSurrogatesWithReplacementCharacterInternal(string).toAtomString();
}

String replaceUnpairedSurrogatesWithReplacementCharacter(String&& string)
{
    // Fast path for the case where there are no unpaired surrogates.
    if (!hasUnpairedSurrogate(string)) [[likely]]
        return WTFMove(string);
    return replaceUnpairedSurrogatesWithReplacementCharacterInternal(string).toString();
}

} // namespace WTF
