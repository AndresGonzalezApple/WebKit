/*
 * Copyright (C) 2006-2017 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
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

#pragma once

#include "CanvasGradient.h"
#include "CanvasPattern.h"
#include "Color.h"
#include "ColorSerialization.h"
#include <optional>

namespace WebCore {

class CanvasBase;
class Document;
class GraphicsContext;
class ScriptExecutionContext;

class CanvasStyle {
public:
    CanvasStyle(Color);
    CanvasStyle(Ref<CanvasGradient>&&);
    CanvasStyle(Ref<CanvasPattern>&&);

    Color color() const;
    String colorString() const;
    RefPtr<CanvasGradient> canvasGradient() const;
    RefPtr<CanvasPattern> canvasPattern() const;

    template<typename... F>
    decltype(auto) visit(F&&... f) const
    {
        auto visitor = WTF::makeVisitor(std::forward<F>(f)...);
        return WTF::switchOn(m_style,
            [&](const Color& color) {
                return visitor(serializationForHTML(color));
            },
            [&](const Ref<CanvasGradient>& gradient) {
                return visitor(gradient);
            },
            [&](const Ref<CanvasPattern>& pattern) {
                return visitor(pattern);
            }
        );
    }

private:
    Variant<Color, Ref<CanvasGradient>, Ref<CanvasPattern>> m_style;
};

Color parseColor(const String& colorString, CanvasBase&);
Color parseColor(const String& colorString, ScriptExecutionContext&);

inline CanvasStyle::CanvasStyle(Color color)
    : m_style(WTFMove(color))
{
}

inline CanvasStyle::CanvasStyle(Ref<CanvasGradient>&& gradient)
    : m_style(WTFMove(gradient))
{
}

inline CanvasStyle::CanvasStyle(Ref<CanvasPattern>&& pattern)
    : m_style(WTFMove(pattern))
{
}

inline RefPtr<CanvasGradient> CanvasStyle::canvasGradient() const
{
    if (!std::holds_alternative<Ref<CanvasGradient>>(m_style))
        return nullptr;
    return std::get<Ref<CanvasGradient>>(m_style).ptr();
}

inline RefPtr<CanvasPattern> CanvasStyle::canvasPattern() const
{
    if (!std::holds_alternative<Ref<CanvasPattern>>(m_style))
        return nullptr;
    return std::get<Ref<CanvasPattern>>(m_style).ptr();
}

inline String CanvasStyle::colorString() const
{
    if (!std::holds_alternative<Color>(m_style))
        return String();
    return serializationForHTML(std::get<Color>(m_style));
}

inline Color CanvasStyle::color() const
{
    if (auto* color = std::get_if<Color>(&m_style))
        return *color;
    return { };
}

} // namespace WebCore
