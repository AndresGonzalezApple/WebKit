/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
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
#include <wtf/Forward.h>
#include <wtf/RefCountedAndCanMakeWeakPtr.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

class SWServerRegistration;
class ServiceWorkerRegistrationKey;

struct ServiceWorkerContextData;

class SWRegistrationStore : public RefCountedAndCanMakeWeakPtr<SWRegistrationStore> {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(SWRegistrationStore);
public:
    virtual ~SWRegistrationStore() = default;
    virtual void clearAll(CompletionHandler<void()>&&) = 0;
    virtual void flushChanges(CompletionHandler<void()>&&) = 0;
    virtual void closeFiles(CompletionHandler<void()>&&) = 0;
    virtual void importRegistrations(CompletionHandler<void(std::optional<Vector<ServiceWorkerContextData>>&&)>&&) = 0;
    virtual void updateRegistration(const ServiceWorkerContextData&) = 0;
    virtual void removeRegistration(const ServiceWorkerRegistrationKey&) = 0;

protected:
    SWRegistrationStore() = default;
};

} // namespace WebCore
