/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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

#import <WebKit/WKFoundation.h>

#if !TARGET_OS_IPHONE

NS_ASSUME_NONNULL_BEGIN

@class _WKHitTestResult;

WK_CLASS_AVAILABLE(macos(10.12))
@interface _WKContextMenuElementInfo : NSObject <NSCopying>

@property (nonatomic, readonly, copy) _WKHitTestResult *hitTestResult WK_API_AVAILABLE(macos(13.3));
@property (nonatomic, readonly, copy, nullable) NSString *qrCodePayloadString WK_API_AVAILABLE(macos(14.0));
@property (nonatomic, readonly) BOOL hasEntireImage WK_API_AVAILABLE(macos(14.0));
@property (nonatomic, readonly) BOOL allowsFollowingLink WK_API_AVAILABLE(macos(WK_MAC_TBA));
@property (nonatomic, readonly) BOOL allowsFollowingImageURL WK_API_AVAILABLE(macos(WK_MAC_TBA));

@end

NS_ASSUME_NONNULL_END

#endif
