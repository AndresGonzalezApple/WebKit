/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
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
#include "WindowOrWorkerGlobalScopeFetch.h"

#include "CachedResourceRequestInitiatorTypes.h"
#include "DocumentInlines.h"
#include "EventLoop.h"
#include "FetchResponse.h"
#include "JSDOMPromiseDeferred.h"
#include "JSFetchResponse.h"
#include "LocalDOMWindow.h"
#include "Quirks.h"
#include "UserGestureIndicator.h"
#include "WorkerGlobalScope.h"

namespace WebCore {

using FetchResponsePromise = DOMPromiseDeferred<IDLInterface<FetchResponse>>;

// https://fetch.spec.whatwg.org/#dom-global-fetch
static void doFetch(ScriptExecutionContext& scope, FetchRequest::Info&& input, FetchRequest::Init&& init, FetchResponsePromise&& promise)
{
    auto requestOrException = FetchRequest::create(scope, WTFMove(input), WTFMove(init));
    if (requestOrException.hasException()) {
        promise.reject(requestOrException.releaseException());
        return;
    }

    auto request = requestOrException.releaseReturnValue();
    if (request->signal().aborted()) {
        auto reason = request->signal().reason().getValue();
        if (reason.isUndefined())
            promise.reject(Exception { ExceptionCode::AbortError, "Request signal is aborted"_s });
        else
            promise.rejectType<IDLAny>(reason);

        return;
    }

    FetchResponse::fetch(scope, request.get(), [promise = WTFMove(promise), scope = Ref { scope }, userGestureToken = UserGestureIndicator::currentUserGesture()]<typename Result> (Result&& result) mutable {
        scope->eventLoop().queueTask(TaskSource::Networking, [promise = WTFMove(promise), userGestureToken = WTFMove(userGestureToken), result = std::forward<Result>(result)] () mutable {
            if (!userGestureToken || userGestureToken->hasExpired(UserGestureToken::maximumIntervalForUserGestureForwardingForFetch()) || !userGestureToken->processingUserGesture()) {
                promise.settle(WTFMove(result));
                return;
            }
            UserGestureIndicator gestureIndicator(userGestureToken, UserGestureToken::GestureScope::MediaOnly, UserGestureToken::ShouldPropagateToMicroTask::Yes);
            promise.settle(WTFMove(result));
        });
    }, cachedResourceRequestInitiatorTypes().fetch);
}

void WindowOrWorkerGlobalScopeFetch::fetch(DOMWindow& window, FetchRequest::Info&& input, FetchRequest::Init&& init, Ref<DeferredPromise>&& promise)
{
    if (RefPtr document = window.documentIfLocal(); document && document->quirks().shouldBlockFetchWithNewlineAndLessThan()) {
        if (auto* string = std::get_if<String>(&input); string && string->contains('\n') && string->contains('<'))
            return promise->reject(ExceptionCode::InvalidStateError);
    }

    RefPtr localWindow = dynamicDowncast<LocalDOMWindow>(window);
    if (!localWindow) {
        promise->reject(ExceptionCode::InvalidStateError);
        return;
    }
    RefPtr document = localWindow->document();
    if (!document) {
        promise->reject(ExceptionCode::InvalidStateError);
        return;
    }
    doFetch(*document, WTFMove(input), WTFMove(init), WTFMove(promise));
}

void WindowOrWorkerGlobalScopeFetch::fetch(WorkerGlobalScope& scope, FetchRequest::Info&& input, FetchRequest::Init&& init, Ref<DeferredPromise>&& promise)
{
    doFetch(scope, WTFMove(input), WTFMove(init), WTFMove(promise));
}

}
