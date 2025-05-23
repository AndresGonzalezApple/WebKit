/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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

#pragma once

#include <optional>
#include <span>
#include <wtf/EnumTraits.h>

namespace IPC {

class Decoder;
class Encoder;

template<typename T, typename = void> struct ArgumentCoder;

template<>
struct ArgumentCoder<bool> {
    template<typename Encoder>
    static void encode(Encoder& encoder, bool value)
    {
        uint8_t data = value ? 1 : 0;
        encoder << data;
    }

    template<typename Decoder>
    static std::optional<bool> decode(Decoder& decoder)
    {
        auto data = decoder.template decode<uint8_t>();
        if (data && *data <= 1) // This ensures that only the lower bit is set in a boolean for IPC messages
            return !!*data;
        return std::nullopt;
    }
};

template<typename T>
struct ArgumentCoder<T, typename std::enable_if_t<std::is_arithmetic_v<T>>> {
    template<typename Encoder>
    static void encode(Encoder& encoder, T value)
    {
        encoder.encodeObject(value);
    }

    template<typename Decoder>
    static std::optional<T> decode(Decoder& decoder)
    {
        return decoder.template decodeObject<T>();
    }
};

template<typename T>
struct ArgumentCoder<T, typename std::enable_if_t<std::is_enum_v<T>>> {
    template<typename Encoder>
    static void encode(Encoder& encoder, T value)
    {
        ASSERT(WTF::isValidEnum<T>(WTF::enumToUnderlyingType<T>(value)));
        encoder << WTF::enumToUnderlyingType<T>(value);
    }

    template<typename Decoder>
    static std::optional<T> decode(Decoder& decoder)
    {
        auto value = decoder.template decode<std::underlying_type_t<T>>();
        if (value && WTF::isValidEnum<T>(*value))
            return static_cast<T>(*value);
        return std::nullopt;
    }
};

}
