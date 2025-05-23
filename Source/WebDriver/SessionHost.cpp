/*
 * Copyright (C) 2017 Igalia S.L.
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
#include "SessionHost.h"

#include "Logging.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/Observer.h>
#include <wtf/WeakHashSet.h>
#include <wtf/text/StringBuilder.h>

namespace WebDriver {

#if ENABLE(WEBDRIVER_BIDI)
static WeakHashSet<SessionHost::BrowserTerminatedObserver>& browserTerminatedObservers()
{
    static NeverDestroyed<WeakHashSet<SessionHost::BrowserTerminatedObserver>> observers;
    return observers;
}
#endif

void SessionHost::inspectorDisconnected()
{
    Ref<SessionHost> protectedThis(*this);
    // Browser closed or crashed, finish all pending commands with error.
    RefPtr<JSON::Object> errorResponse = JSON::Object::create();
    errorResponse->setString("message"_s, "Session terminated without a reply"_s);
    for (auto messageID : copyToVector(m_commandRequests.keys())) {
        auto responseHandler = m_commandRequests.take(messageID);
        responseHandler({ errorResponse , true });
    }

#if ENABLE(WEBDRIVER_BIDI)
    for (auto& observer : browserTerminatedObservers())
        observer(m_sessionID);
#endif
}

long SessionHost::sendCommandToBackend(const String& command, RefPtr<JSON::Object>&& parameters, Function<void (CommandResponse&&)>&& responseHandler)
{
    if (!isConnected()) {
        responseHandler({ nullptr, true });
        return 0;
    }

    static long lastSequenceID = 0;
    long sequenceID = ++lastSequenceID;
    m_commandRequests.add(sequenceID, WTFMove(responseHandler));
    StringBuilder messageBuilder;
    messageBuilder.append("{\"id\":"_s, sequenceID, ",\"method\":\"Automation."_s, command, '"');
    if (parameters)
        messageBuilder.append(",\"params\":"_s, parameters->toJSONString());
    messageBuilder.append('}');
    sendMessageToBackend(messageBuilder.toString());

    return sequenceID;
}

void SessionHost::dispatchMessage(const String& message)
{
    LOG(SessionHost, "SessionHost::dispatchMessage: %s", message.utf8().data());
    auto messageValue = JSON::Value::parseJSON(message);
    if (!messageValue)
        return;

    auto messageObject = messageValue->asObject();
    if (!messageObject)
        return;

    auto sequenceID = messageObject->getInteger("id"_s);
    if (!sequenceID) {
        auto method = messageObject->getString("method"_s);
        if (method == "Automation.browsingContextCleared"_s)
            return;
#if ENABLE(WEBDRIVER_BIDI)
        if (method != "Automation.bidiMessageSent"_s)
            return;
        dispatchBidiMessage(WTFMove(messageObject));
#else
        RELEASE_LOG_ERROR(SessionHost, "Received from browser message without id: %s", message.utf8().data());
#endif
        return;
    }

    auto responseHandler = m_commandRequests.take(*sequenceID);
    ASSERT(responseHandler);

    CommandResponse response;
    if (auto errorObject = messageObject->getObject("error"_s)) {
        response.responseObject = WTFMove(errorObject);
        response.isError = true;
    } else if (auto resultObject = messageObject->getObject("result"_s)) {
        if (resultObject->size())
            response.responseObject = WTFMove(resultObject);
    }

    responseHandler(WTFMove(response));
}

bool SessionHost::isRemoteBrowser() const
{
    return m_isRemoteBrowser;
}

#if ENABLE(WEBDRIVER_BIDI)
void SessionHost::addBrowserTerminatedObserver(const BrowserTerminatedObserver& observer)
{
    ASSERT(!browserTerminatedObservers().contains(observer));
    browserTerminatedObservers().add(observer);
}

void SessionHost::removeBrowserTerminatedObserver(const BrowserTerminatedObserver& observer)
{
    browserTerminatedObservers().remove(observer);
}

void SessionHost::dispatchBidiMessage(RefPtr<JSON::Object>&& event)
{
    LOG(WebDriverBiDi, "SessionHost::dispatchBidiMessage: %s", event->toJSONString().utf8().data());
    if (m_bidiHandler)
        m_bidiHandler->dispatchBidiMessage(WTFMove(event));
    else
        RELEASE_LOG(SessionHost, "No bidi message handler to dispatch message %s", event->toJSONString().utf8().data());
}
#endif

} // namespace WebDriver
