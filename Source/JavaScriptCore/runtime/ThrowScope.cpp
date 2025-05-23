/*
 * Copyright (C) 2016-2021 Apple Inc. All rights reserved.
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
#include "ThrowScope.h"

#include "Exception.h"
#include "JSCJSValueInlines.h"
#include "VM.h"
#include <wtf/StackTrace.h>

namespace JSC {
    
#if ENABLE(EXCEPTION_SCOPE_VERIFICATION)

ThrowScope::ThrowScope(VM& vm, ExceptionEventLocation location)
    : ExceptionScope(vm, location)
{
    m_vm.verifyExceptionCheckNeedIsSatisfied(m_recursionDepth, m_location);
}

ThrowScope::~ThrowScope()
{
    RELEASE_ASSERT(m_vm.m_topExceptionScope);

    if (!m_isReleased)
        m_vm.verifyExceptionCheckNeedIsSatisfied(m_recursionDepth, m_location);
    else {
        // If we released the scope, that means we're letting our callers do the
        // exception check. However, because our caller may be a LLInt or JIT
        // function (which always checks for exceptions but won't clear the
        // m_needExceptionCheck bit), we should clear m_needExceptionCheck here
        // and let code below decide if we need to simulate a re-throw.
        m_vm.m_needExceptionCheck = false;
    }

    bool willBeHandleByLLIntOrJIT = false;
    const void* previousScopeStackPosition = m_previousScope ? m_previousScope->stackPosition() : nullptr;
    void* topEntryFrame = m_vm.topEntryFrame;

    // If the topEntryFrame was pushed on the stack after the previousScope was instantiated,
    // then this throwScope will be returning to LLINT or JIT code that always do an exception
    // check. In that case, skip the simulated throw because the LLInt and JIT will be
    // checking for the exception their own way instead of calling ThrowScope::exception().
    if (topEntryFrame && previousScopeStackPosition > topEntryFrame)
        willBeHandleByLLIntOrJIT = true;

    if (!willBeHandleByLLIntOrJIT)
        simulateThrow();
}

Exception* ThrowScope::throwException(JSGlobalObject* globalObject, Exception* exception)
{
    if (m_vm.exception() && m_vm.exception() != exception)
        m_vm.verifyExceptionCheckNeedIsSatisfied(m_recursionDepth, m_location);
    
    return m_vm.throwException(globalObject, exception);
}

Exception* ThrowScope::throwException(JSGlobalObject* globalObject, JSValue error)
{
    if (!error.isCell() || error.isObject() || !jsDynamicCast<Exception*>(error.asCell()))
        m_vm.verifyExceptionCheckNeedIsSatisfied(m_recursionDepth, m_location);
    
    return m_vm.throwException(globalObject, error);
}

void ThrowScope::simulateThrow()
{
    RELEASE_ASSERT(m_vm.m_topExceptionScope);
    m_vm.m_simulatedThrowPointLocation = m_location;
    m_vm.m_simulatedThrowPointRecursionDepth = m_recursionDepth;
    m_vm.m_needExceptionCheck = true;
    if (Options::dumpSimulatedThrows()) [[unlikely]]
        m_vm.m_nativeStackTraceOfLastSimulatedThrow = StackTrace::captureStackTrace(Options::unexpectedExceptionStackTraceLimit());
}

#endif // ENABLE(EXCEPTION_SCOPE_VERIFICATION)
    
} // namespace JSC
