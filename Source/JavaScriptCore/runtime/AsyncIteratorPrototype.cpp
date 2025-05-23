/*
 * Copyright (C) 2017 Oleksandr Skachkov <gskackhov@gmail.com>.
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
#include "AsyncIteratorPrototype.h"

#include "JSCBuiltins.h"
#include "JSCInlines.h"

namespace JSC {

const ClassInfo AsyncIteratorPrototype::s_info = { "AsyncIterator"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(AsyncIteratorPrototype) };

static JSC_DECLARE_HOST_FUNCTION(asyncIteratorProtoFuncAsyncIterator);

void AsyncIteratorPrototype::finishCreation(VM& vm, JSGlobalObject* globalObject)
{
    Base::finishCreation(vm);
    ASSERT(inherits(info()));
    JSFunction* asyncIteratorFunction = JSFunction::create(vm, globalObject, 0, "[Symbol.asyncIterator]"_s, asyncIteratorProtoFuncAsyncIterator, ImplementationVisibility::Public, AsyncIteratorIntrinsic);
    putDirectWithoutTransition(vm, vm.propertyNames->asyncIteratorSymbol, asyncIteratorFunction, static_cast<unsigned>(PropertyAttribute::DontEnum));

    if (Options::useExplicitResourceManagement())
        JSC_BUILTIN_FUNCTION_WITHOUT_TRANSITION(vm.propertyNames->asyncDisposeSymbol, asyncIteratorPrototypeAsyncDisposeCodeGenerator, static_cast<unsigned>(PropertyAttribute::DontEnum));
}

JSC_DEFINE_HOST_FUNCTION(asyncIteratorProtoFuncAsyncIterator, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    return JSValue::encode(callFrame->thisValue().toThis(globalObject, ECMAMode::strict()));
}

} // namespace JSC
