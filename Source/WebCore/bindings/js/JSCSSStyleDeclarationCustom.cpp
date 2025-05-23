/*
 * Copyright (C) 2007-2021 Apple Inc. All rights reserved.
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
#include "JSCSSStyleDeclaration.h"

#include "CSSFontFaceDescriptors.h"
#include "CSSPageDescriptors.h"
#include "CSSPositionTryDescriptors.h"
#include "CSSStyleProperties.h"
#include "DOMWrapperWorld.h"
#include "JSCSSFontFaceDescriptors.h"
#include "JSCSSPageDescriptors.h"
#include "JSCSSPositionTryDescriptors.h"
#include "JSCSSRuleCustom.h"
#include "JSCSSStyleProperties.h"
#include "JSDOMConvertInterface.h"
#include "JSDOMConvertStrings.h"
#include "JSDeprecatedCSSOMValue.h"
#include "JSNodeCustom.h"
#include "JSStyleSheetCustom.h"
#include "StyledElement.h"
#include "WebCoreOpaqueRootInlines.h"

namespace WebCore {
using namespace JSC;

WebCoreOpaqueRoot root(CSSStyleDeclaration* style)
{
    ASSERT(style);
    if (auto* parentRule = style->parentRule())
        return root(parentRule);
    if (auto* styleSheet = style->parentStyleSheet())
        return root(styleSheet);
    if (auto* parentElement = style->parentElement())
        return root(parentElement);
    return WebCoreOpaqueRoot { style };
}

template<typename Visitor>
void JSCSSStyleDeclaration::visitAdditionalChildren(Visitor& visitor)
{
    addWebCoreOpaqueRoot(visitor, wrapped());
}

DEFINE_VISIT_ADDITIONAL_CHILDREN(JSCSSStyleDeclaration);

JSValue toJSNewlyCreated(JSGlobalObject*, JSDOMGlobalObject* globalObject, Ref<CSSStyleDeclaration>&& declaration)
{
    switch (declaration->styleDeclarationType()) {
    case StyleDeclarationType::Style:
        return createWrapper<CSSStyleProperties>(globalObject, WTFMove(declaration));
    case StyleDeclarationType::FontFace:
        return createWrapper<CSSFontFaceDescriptors>(globalObject, WTFMove(declaration));
    case StyleDeclarationType::Page:
        return createWrapper<CSSPageDescriptors>(globalObject, WTFMove(declaration));
    case StyleDeclarationType::PositionTry:
        return createWrapper<CSSPositionTryDescriptors>(globalObject, WTFMove(declaration));
    }
    RELEASE_ASSERT_NOT_REACHED();
}

JSValue toJS(JSGlobalObject* lexicalGlobalObject, JSDOMGlobalObject* globalObject, CSSStyleDeclaration& object)
{
    return wrap(lexicalGlobalObject, globalObject, object);
}

} // namespace WebCore
