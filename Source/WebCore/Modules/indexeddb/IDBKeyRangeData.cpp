/*
 * Copyright (C) 2014 Apple Inc. All rights reserved.
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
#include "IDBKeyRangeData.h"

#include "IDBKey.h"
#include <wtf/text/MakeString.h>

namespace WebCore {

IDBKeyRangeData::IDBKeyRangeData(IDBKey* key)
    : lowerKey(key)
    , upperKey(key)
{
}

IDBKeyRangeData::IDBKeyRangeData(const IDBKeyData& keyData)
    : lowerKey(keyData)
    , upperKey(keyData)
{
}

IDBKeyRangeData IDBKeyRangeData::isolatedCopy() const
{
    IDBKeyRangeData result;

    result.lowerKey = lowerKey.isolatedCopy();
    result.upperKey = upperKey.isolatedCopy();
    result.lowerOpen = lowerOpen;
    result.upperOpen = upperOpen;

    return result;
}

bool IDBKeyRangeData::isExactlyOneKey() const
{
    if (isNull() || lowerOpen || upperOpen || !upperKey.isValid() || !lowerKey.isValid())
        return false;

    return lowerKey == upperKey;
}

bool IDBKeyRangeData::containsKey(const IDBKeyData& key) const
{
    if (lowerKey.isValid()) {
        auto compare = lowerKey <=> key;
        if (is_gt(compare))
            return false;
        if (lowerOpen && is_eq(compare))
            return false;
    }
    if (upperKey.isValid()) {
        auto compare = upperKey <=> key;
        if (is_lt(compare))
            return false;
        if (upperOpen && is_eq(compare))
            return false;
    }

    return true;
}

bool IDBKeyRangeData::isValid() const
{
    if (isNull())
        return false;

    if (!lowerKey.isValid() && !lowerKey.isNull())
        return false;

    if (!upperKey.isValid() && !upperKey.isNull())
        return false;

    return true;
}

#if !LOG_DISABLED
String IDBKeyRangeData::loggingString() const
{
    auto result = makeString(lowerOpen ? "( "_s : "[ "_s, lowerKey.loggingString(), ", "_s, upperKey.loggingString(), upperOpen ? " )"_s : " ]"_s);
    if (result.length() > 400)
        result = makeString(StringView(result).left(397), "..."_s);

    return result;
}
#endif

} // namespace WebCore
