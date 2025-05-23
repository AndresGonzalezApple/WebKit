/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/graphite/dawn/DawnCaps.h"

#include <algorithm>
#include <string>

#include "include/core/SkTextureCompressionType.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/TextureInfo.h"
#include "include/gpu/graphite/dawn/DawnBackendContext.h"
#include "include/gpu/graphite/dawn/DawnGraphiteTypes.h"
#include "src/gpu/SwizzlePriv.h"
#include "src/gpu/graphite/ComputePipelineDesc.h"
#include "src/gpu/graphite/GraphicsPipelineDesc.h"
#include "src/gpu/graphite/GraphiteResourceKey.h"
#include "src/gpu/graphite/RenderPassDesc.h"
#include "src/gpu/graphite/RendererProvider.h"
#include "src/gpu/graphite/ResourceTypes.h"
#include "src/gpu/graphite/TextureInfoPriv.h"
#include "src/gpu/graphite/UniformManager.h"
#include "src/gpu/graphite/dawn/DawnGraphicsPipeline.h"
#include "src/gpu/graphite/dawn/DawnGraphiteUtils.h"
#include "src/sksl/SkSLUtil.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/version.h>
#endif

namespace {

skgpu::UniqueKey::Domain get_pipeline_domain() {
    static const skgpu::UniqueKey::Domain kDawnGraphicsPipelineDomain =
            skgpu::UniqueKey::GenerateDomain();

    return kDawnGraphicsPipelineDomain;
}

// These are all the valid wgpu::TextureFormat that we currently support in Skia.
// They are roughly ordered from most frequently used to least to improve lookup times in arrays.
static constexpr wgpu::TextureFormat kFormats[] = {
        wgpu::TextureFormat::RGBA8Unorm,
        wgpu::TextureFormat::R8Unorm,
#if !defined(__EMSCRIPTEN__)
        wgpu::TextureFormat::R16Unorm,
#endif
        wgpu::TextureFormat::BGRA8Unorm,
        wgpu::TextureFormat::RGBA16Float,
        wgpu::TextureFormat::R16Float,
        wgpu::TextureFormat::RG8Unorm,
#if !defined(__EMSCRIPTEN__)
        wgpu::TextureFormat::RG16Unorm,
#endif
        wgpu::TextureFormat::RGB10A2Unorm,
        wgpu::TextureFormat::RG16Float,

        wgpu::TextureFormat::Stencil8,
        wgpu::TextureFormat::Depth16Unorm,
        wgpu::TextureFormat::Depth32Float,
        wgpu::TextureFormat::Depth24PlusStencil8,

        wgpu::TextureFormat::BC1RGBAUnorm,
        wgpu::TextureFormat::ETC2RGB8Unorm,

#if !defined(__EMSCRIPTEN__)
        wgpu::TextureFormat::External,
#endif
};

#if !defined(__EMSCRIPTEN__)
bool IsMultiplanarFormat(wgpu::TextureFormat format) {
    switch (format) {
        case wgpu::TextureFormat::R8BG8Biplanar420Unorm:
        case wgpu::TextureFormat::R10X6BG10X6Biplanar420Unorm:
        case wgpu::TextureFormat::R8BG8A8Triplanar420Unorm:
            return true;
        default:
            return false;
    }
}
#endif
}  // anonymous namespace

namespace skgpu::graphite {

DawnCaps::DawnCaps(const DawnBackendContext& backendContext, const ContextOptions& options)
    : Caps() {
    this->initCaps(backendContext, options);
    this->initShaderCaps(backendContext.fDevice);
    this->initFormatTable(backendContext.fDevice);
    this->finishInitialization(options);
}

DawnCaps::~DawnCaps() = default;

bool DawnCaps::onIsTexturable(const TextureInfo& info) const {
    if (!info.isValid()) {
        return false;
    }

    const auto& dawnInfo = TextureInfoPriv::Get<DawnTextureInfo>(info);

    if (!(dawnInfo.fUsage & wgpu::TextureUsage::TextureBinding)) {
        return false;
    }

#if !defined(__EMSCRIPTEN__)
    switch (dawnInfo.fFormat) {
        case wgpu::TextureFormat::R8BG8Biplanar420Unorm: {
            if (dawnInfo.fAspect == wgpu::TextureAspect::Plane0Only &&
                dawnInfo.getViewFormat() != wgpu::TextureFormat::R8Unorm) {
                return false;
            }
            if (dawnInfo.fAspect == wgpu::TextureAspect::Plane1Only &&
                dawnInfo.getViewFormat() != wgpu::TextureFormat::RG8Unorm) {
                return false;
            }
            break;
        }
        case wgpu::TextureFormat::R10X6BG10X6Biplanar420Unorm: {
            if (dawnInfo.fAspect == wgpu::TextureAspect::Plane0Only &&
                dawnInfo.getViewFormat() != wgpu::TextureFormat::R16Unorm) {
                return false;
            }
            if (dawnInfo.fAspect == wgpu::TextureAspect::Plane1Only &&
                dawnInfo.getViewFormat() != wgpu::TextureFormat::RG16Unorm) {
                return false;
            }
            break;
        }
        case wgpu::TextureFormat::R8BG8A8Triplanar420Unorm: {
            if (dawnInfo.fAspect == wgpu::TextureAspect::Plane0Only &&
                dawnInfo.getViewFormat() != wgpu::TextureFormat::R8Unorm) {
                return false;
            }
            if (dawnInfo.fAspect == wgpu::TextureAspect::Plane1Only &&
                dawnInfo.getViewFormat() != wgpu::TextureFormat::RG8Unorm) {
                return false;
            }
            if (dawnInfo.fAspect == wgpu::TextureAspect::Plane2Only &&
                dawnInfo.getViewFormat() != wgpu::TextureFormat::R8Unorm) {
                return false;
            }
            break;
        }
        default:
            break;
    }
#endif

    return this->isTexturable(dawnInfo.getViewFormat());
}

bool DawnCaps::isTexturable(wgpu::TextureFormat format) const {
    const FormatInfo& formatInfo = this->getFormatInfo(format);
    return SkToBool(FormatInfo::kTexturable_Flag & formatInfo.fFlags);
}

bool DawnCaps::isRenderable(const TextureInfo& info) const {
    if (!info.isValid()) {
        return false;
    }
    const auto& dawnInfo = TextureInfoPriv::Get<DawnTextureInfo>(info);
    return (dawnInfo.fUsage & wgpu::TextureUsage::RenderAttachment) &&
           this->isRenderable(dawnInfo.getViewFormat(), dawnInfo.fSampleCount);
}

bool DawnCaps::isStorage(const TextureInfo& info) const {
    if (!info.isValid()) {
        return false;
    }
    const auto& dawnInfo = TextureInfoPriv::Get<DawnTextureInfo>(info);
    if (!(dawnInfo.fUsage & wgpu::TextureUsage::StorageBinding)) {
        return false;
    }
    const FormatInfo& formatInfo = this->getFormatInfo(dawnInfo.getViewFormat());
    return dawnInfo.fSampleCount == 1 && SkToBool(FormatInfo::kStorage_Flag & formatInfo.fFlags);
}

uint32_t DawnCaps::maxRenderTargetSampleCount(wgpu::TextureFormat format) const {
    const FormatInfo& formatInfo = this->getFormatInfo(format);
    if (!SkToBool(formatInfo.fFlags & FormatInfo::kRenderable_Flag)) {
        return 0;
    }
    if (SkToBool(formatInfo.fFlags & FormatInfo::kMSAA_Flag)) {
        return 8;
    } else {
        return 1;
    }
}

bool DawnCaps::isRenderable(wgpu::TextureFormat format, uint32_t sampleCount) const {
    return sampleCount <= this->maxRenderTargetSampleCount(format);
}

TextureInfo DawnCaps::getDefaultSampledTextureInfo(SkColorType colorType,
                                                   Mipmapped mipmapped,
                                                   Protected,
                                                   Renderable renderable) const {
    wgpu::TextureUsage usage = wgpu::TextureUsage::TextureBinding |
                               wgpu::TextureUsage::CopyDst |
                               wgpu::TextureUsage::CopySrc;
    if (renderable == Renderable::kYes) {
        usage |= wgpu::TextureUsage::RenderAttachment;
    }

    wgpu::TextureFormat format = this->getFormatFromColorType(colorType);
    if (format == wgpu::TextureFormat::Undefined) {
        return {};
    }

    DawnTextureInfo info;
    info.fSampleCount = 1;
    info.fMipmapped = mipmapped;
    info.fFormat = format;
    info.fViewFormat = format;
    info.fUsage = usage;

    return TextureInfos::MakeDawn(info);
}

TextureInfo DawnCaps::getTextureInfoForSampledCopy(const TextureInfo& textureInfo,
                                                   Mipmapped mipmapped) const {
    DawnTextureInfo info;
    info.fSampleCount = 1;
    info.fMipmapped = mipmapped;
    info.fFormat = TextureInfoPriv::Get<DawnTextureInfo>(textureInfo).getViewFormat();
    info.fUsage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst |
                  wgpu::TextureUsage::CopySrc;

    return TextureInfos::MakeDawn(info);
}

namespace {
wgpu::TextureFormat format_from_compression(SkTextureCompressionType compression) {
    switch (compression) {
        case SkTextureCompressionType::kETC2_RGB8_UNORM:
            return wgpu::TextureFormat::ETC2RGB8Unorm;
        case SkTextureCompressionType::kBC1_RGBA8_UNORM:
            return wgpu::TextureFormat::BC1RGBAUnorm;
        default:
            return wgpu::TextureFormat::Undefined;
    }
}
}

TextureInfo DawnCaps::getDefaultCompressedTextureInfo(SkTextureCompressionType compression,
                                                      Mipmapped mipmapped,
                                                      Protected) const {
    wgpu::TextureUsage usage = wgpu::TextureUsage::TextureBinding |
                               wgpu::TextureUsage::CopyDst |
                               wgpu::TextureUsage::CopySrc;

    wgpu::TextureFormat format = format_from_compression(compression);
    if (format == wgpu::TextureFormat::Undefined) {
        return {};
    }

    DawnTextureInfo info;
    info.fSampleCount = 1;
    info.fMipmapped = mipmapped;
    info.fFormat = format;
    info.fViewFormat = format;
    info.fUsage = usage;

    return TextureInfos::MakeDawn(info);
}

TextureInfo DawnCaps::getDefaultMSAATextureInfo(const TextureInfo& singleSampledInfo,
                                                Discardable discardable) const {
    if (fDefaultMSAASamples <= 1) {
        return {};
    }

    DawnTextureInfo info;
    info.fSampleCount = fDefaultMSAASamples;
    info.fMipmapped   = Mipmapped::kNo;
    info.fFormat      = TextureInfoPriv::Get<DawnTextureInfo>(singleSampledInfo).getViewFormat();
    info.fUsage       = wgpu::TextureUsage::RenderAttachment;

    if (fSupportedTransientAttachmentUsage != wgpu::TextureUsage::None &&
        discardable == Discardable::kYes) {
        info.fUsage |= fSupportedTransientAttachmentUsage;
    }

    return TextureInfos::MakeDawn(info);
}

TextureInfo DawnCaps::getDefaultDepthStencilTextureInfo(
        SkEnumBitMask<DepthStencilFlags> depthStencilType,
        uint32_t sampleCount,
        Protected,
        Discardable discardable) const {
    DawnTextureInfo info;
    info.fSampleCount = sampleCount;
    info.fMipmapped   = Mipmapped::kNo;
    info.fFormat      = DawnDepthStencilFlagsToFormat(depthStencilType);
    info.fViewFormat  = info.fFormat;
    info.fUsage       = wgpu::TextureUsage::RenderAttachment;

    if (discardable == Discardable::kYes &&
        fSupportedTransientAttachmentUsage != wgpu::TextureUsage::None) {
        info.fUsage |= fSupportedTransientAttachmentUsage;
    }

    return TextureInfos::MakeDawn(info);
}

TextureInfo DawnCaps::getDefaultStorageTextureInfo(SkColorType colorType) const {
    wgpu::TextureFormat format = this->getFormatFromColorType(colorType);
    if (format == wgpu::TextureFormat::Undefined) {
        SkDebugf("colorType=%d is not supported\n", static_cast<int>(colorType));
        return {};
    }

    const FormatInfo& formatInfo = this->getFormatInfo(format);
    if (!SkToBool(FormatInfo::kStorage_Flag & formatInfo.fFlags)) {
        return {};
    }

    wgpu::TextureUsage usage = wgpu::TextureUsage::StorageBinding |
                               wgpu::TextureUsage::TextureBinding |
                               wgpu::TextureUsage::CopySrc;
    DawnTextureInfo info;
    info.fSampleCount = 1;
    info.fMipmapped = Mipmapped::kNo;
    info.fFormat = format;
    info.fViewFormat = format;
    info.fUsage = usage;

    return TextureInfos::MakeDawn(info);
}

SkISize DawnCaps::getDepthAttachmentDimensions(const TextureInfo& textureInfo,
                                               const SkISize colorAttachmentDimensions) const {
#if !defined(__EMSCRIPTEN__)
    // For multiplanar textures, texture->textureInfo() uses the format of planes instead of
    // textures (R8, R8G8, vs R8BG8Biplanar420Unorm), so we have to query texture format from
    // wgpu::Texture object, and then use it reconstruct the full dimensions.
    const auto& dawnInfo = TextureInfoPriv::Get<DawnTextureInfo>(textureInfo);
    wgpu::TextureFormat format = dawnInfo.fFormat;
    if (IsMultiplanarFormat(format) && dawnInfo.fAspect == wgpu::TextureAspect::Plane1Only) {
        // Dawn requires depth attachment to match the size of Y plane (texture size).
        return SkISize::Make(colorAttachmentDimensions.width() * 2,
                             colorAttachmentDimensions.height() * 2);
    }
#endif

    return colorAttachmentDimensions;
}

const Caps::ColorTypeInfo* DawnCaps::getColorTypeInfo(SkColorType colorType,
                                                      const TextureInfo& textureInfo) const {
    auto dawnFormat = TextureInfoPriv::Get<DawnTextureInfo>(textureInfo).getViewFormat();
    if (dawnFormat == wgpu::TextureFormat::Undefined) {
        SkASSERT(false);
        return nullptr;
    }

    const FormatInfo& info = this->getFormatInfo(dawnFormat);
    for (int i = 0; i < info.fColorTypeInfoCount; ++i) {
        const ColorTypeInfo& ctInfo = info.fColorTypeInfos[i];
        if (ctInfo.fColorType == colorType) {
            return &ctInfo;
        }
    }

    return nullptr;
}

bool DawnCaps::supportsWritePixels(const TextureInfo& textureInfo) const {
    const auto& dawnInfo = TextureInfoPriv::Get<DawnTextureInfo>(textureInfo);
    return dawnInfo.fUsage & wgpu::TextureUsage::CopyDst;
}

bool DawnCaps::supportsReadPixels(const TextureInfo& textureInfo) const {
    const auto& dawnInfo = TextureInfoPriv::Get<DawnTextureInfo>(textureInfo);
    return dawnInfo.fUsage & wgpu::TextureUsage::CopySrc;
}

std::pair<SkColorType, bool /*isRGBFormat*/> DawnCaps::supportedWritePixelsColorType(
        SkColorType dstColorType,
        const TextureInfo& dstTextureInfo,
        SkColorType srcColorType) const {
    const auto viewFormat = TextureInfoPriv::Get<DawnTextureInfo>(dstTextureInfo).getViewFormat();
    const FormatInfo& info = this->getFormatInfo(viewFormat);
    for (int i = 0; i < info.fColorTypeInfoCount; ++i) {
        const auto& ctInfo = info.fColorTypeInfos[i];
        if (ctInfo.fColorType == dstColorType) {
            return {ctInfo.fTransferColorType, false};
        }
    }
    return {kUnknown_SkColorType, false};
}

std::pair<SkColorType, bool /*isRGBFormat*/> DawnCaps::supportedReadPixelsColorType(
        SkColorType srcColorType,
        const TextureInfo& srcTextureInfo,
        SkColorType dstColorType) const {
    const auto viewFormat = TextureInfoPriv::Get<DawnTextureInfo>(srcTextureInfo).getViewFormat();

    if (DawnFormatToCompressionType(viewFormat) != SkTextureCompressionType::kNone) {
        return {kUnknown_SkColorType, false};
    }

    const FormatInfo& info = this->getFormatInfo(viewFormat);
    for (int i = 0; i < info.fColorTypeInfoCount; ++i) {
        const auto& ctInfo = info.fColorTypeInfos[i];
        if (ctInfo.fColorType == srcColorType) {
            return {ctInfo.fTransferColorType, false};
        }
    }
    return {kUnknown_SkColorType, false};
}

void DawnCaps::initCaps(const DawnBackendContext& backendContext, const ContextOptions& options) {
    // GetAdapter() is not available in WASM and there's no way to get AdapterInfo off of
    // the WGPUDevice directly.
#if !defined(__EMSCRIPTEN__)
    wgpu::AdapterInfo info;
    backendContext.fDevice.GetAdapter().GetInfo(&info);

#if defined(GPU_TEST_UTILS)
    this->setDeviceName(std::string(info.device));
#endif
#endif // defined(__EMSCRIPTEN__)

#if defined(__EMSCRIPTEN__)
    wgpu::SupportedLimits supportedLimits;
    // TODO(crbug.com/42241199): Update to use wgpu::Status when webgpu.h in Emscripten is updated.
    [[maybe_unused]] bool limitsSucceeded = backendContext.fDevice.GetLimits(&supportedLimits);
    SkASSERT(limitsSucceeded);
    wgpu::Limits& limits = supportedLimits.limits;
#else
    wgpu::Limits limits;
    wgpu::DawnTexelCopyBufferRowAlignmentLimits alignmentLimits{};
    if (backendContext.fDevice.HasFeature(wgpu::FeatureName::DawnTexelCopyBufferRowAlignment)) {
        limits.nextInChain = &alignmentLimits;
    }
    [[maybe_unused]] wgpu::Status status = backendContext.fDevice.GetLimits(&limits);
    SkASSERT(status == wgpu::Status::Success);
#endif  // defined(__EMSCRIPTEN__)

    fMaxTextureSize = limits.maxTextureDimension2D;

    fRequiredTransferBufferAlignment = 4;
    fRequiredUniformBufferAlignment = limits.minUniformBufferOffsetAlignment;
    fRequiredStorageBufferAlignment = limits.minStorageBufferOffsetAlignment;

    // Dawn requires 256 bytes per row alignment for buffer texture copies.
    fTextureDataRowBytesAlignment = 256;
#if !defined(__EMSCRIPTEN__)
    // If the device supports the DawnTexelCopyBufferRowAlignment feature, the alignment can be
    // queried from its limits.
    if (backendContext.fDevice.HasFeature(wgpu::FeatureName::DawnTexelCopyBufferRowAlignment)) {
        fTextureDataRowBytesAlignment = alignmentLimits.minTexelCopyBufferRowAlignment;
    }
#endif

    fResourceBindingReqs.fUniformBufferLayout = Layout::kStd140;
    // The WGSL generator assumes tightly packed std430 layout for SSBOs which is also the default
    // for all types outside the uniform address space in WGSL.
    fResourceBindingReqs.fStorageBufferLayout = Layout::kStd430;
    fResourceBindingReqs.fSeparateTextureAndSamplerBinding = true;

    fResourceBindingReqs.fUniformsSetIdx = DawnGraphicsPipeline::kUniformBufferBindGroupIndex;
    fResourceBindingReqs.fTextureSamplerSetIdx = DawnGraphicsPipeline::kTextureBindGroupIndex;

    fResourceBindingReqs.fIntrinsicBufferBinding =
            DawnGraphicsPipeline::kIntrinsicUniformBufferIndex;
    fResourceBindingReqs.fRenderStepBufferBinding =
            DawnGraphicsPipeline::kRenderStepUniformBufferIndex;
    fResourceBindingReqs.fPaintParamsBufferBinding = DawnGraphicsPipeline::kPaintUniformBufferIndex;
    fResourceBindingReqs.fGradientBufferBinding = DawnGraphicsPipeline::kGradientBufferIndex;

#if !defined(__EMSCRIPTEN__)
    // TODO(b/344963958): SSBOs contribute to OOB shader memory access and dawn device loss on
    // Android. Once the problem is fixed SSBOs can be enabled again.
    // TODO(dawn:388028942): In compat mode, the number of storage buffers in vertex stage could be
    // zero on some devices. We currently use SSBO in vertex shaders so disabling it entirely in
    // this case. There is a bug in D3D11's backend where it sets number of SSBOs in vertex shader
    // to non-zero in compat mode. Once that bug is fixed, and a new limit is used for readonly
    // SSBOs, we can enable this again.
    fStorageBufferSupport =
            info.backendType != wgpu::BackendType::OpenGL &&
            info.backendType != wgpu::BackendType::OpenGLES &&
            info.backendType != wgpu::BackendType::Vulkan &&
            backendContext.fDevice.HasFeature(wgpu::FeatureName::CoreFeaturesAndLimits);
#else
    // WASM doesn't provide a way to query the backend, so can't tell if we are on a backend that
    // needs to have SSBOs disabled. Pessimistically assume we could be. Once the above conditions
    // go away in Dawn-native, then we can assume SSBOs are always supported in pure WebGPU too.
    fStorageBufferSupport = false;
#endif

    fDrawBufferCanBeMapped = false;

    fComputeSupport = true;

    // TODO: support clamp to border.
    fClampToBorderSupport = false;

#if defined(GPU_TEST_UTILS)
    fDrawBufferCanBeMappedForReadback = false;
#endif

#if defined(__EMSCRIPTEN__)
    // For wasm, we use async map.
    fBufferMapsAreAsync = true;
#else
    // For Dawn native, we use direct mapping.
    fBufferMapsAreAsync = false;
    fDrawBufferCanBeMapped =
            backendContext.fDevice.HasFeature(wgpu::FeatureName::BufferMapExtendedUsages);

    fMSAARenderToSingleSampledSupport =
            backendContext.fDevice.HasFeature(wgpu::FeatureName::MSAARenderToSingleSampled);

    if (backendContext.fDevice.HasFeature(wgpu::FeatureName::TransientAttachments)) {
        fSupportedTransientAttachmentUsage = wgpu::TextureUsage::TransientAttachment;
    }
    if (backendContext.fDevice.HasFeature(wgpu::FeatureName::DawnLoadResolveTexture)) {
        fSupportedResolveTextureLoadOp = wgpu::LoadOp::ExpandResolveTexture;
    }
    fSupportsPartialLoadResolve =
            backendContext.fDevice.HasFeature(wgpu::FeatureName::DawnPartialLoadResolveTexture);
#endif

    if (backendContext.fDevice.HasFeature(wgpu::FeatureName::TimestampQuery)) {
        // Native Dawn has an API for writing timestamps on command buffers. WebGPU only supports
        // begin and end timestamps on render and compute passes.
#if !defined(__EMSCRIPTEN__)
        // TODO(b/42240559): On Apple silicon, the timer queries don't have the correct dependencies
        // to measure all the encoders that the start/end commands encapsulate in the commandbuffer.
        // We would prefer to keep this API as it lets us measure our texture uploads. If either
        // this is fixed in Dawn, we can unconditionally take this approach for dawn-native; or
        // the WebGPU API can hopefully be extended to capture blit passes.
        fSupportsCommandBufferTimestamps = info.backendType != wgpu::BackendType::Metal;
#endif

        // The emscripten C/C++ interface before 3.1.48 for timestamp query writes on render and
        // compute passes is different than on current emsdk. The older API isn't correctly
        // translated to the current JS WebGPU API in emsdk. So we require 3.1.48+.
#if !defined(__EMSCRIPTEN__)                                                                   \
        || (__EMSCRIPTEN_major__ > 3)                                                          \
        || (__EMSCRIPTEN_major__ == 3 && __EMSCRIPTEN_minor__ > 1)                             \
        || (__EMSCRIPTEN_major__ == 3 && __EMSCRIPTEN_minor__ == 1 && __EMSCRIPTEN_tiny__ >= 48)
        fSupportedGpuStats |= GpuStatsFlags::kElapsedTime;
#endif
    }

    if (!backendContext.fTick) {
        fAllowCpuSync = false;
        // This seems paradoxical. However, if we use the async pipeline creation methods (e.g
        // Device::CreateRenderPipelineAsync) then we may have to synchronize before a submit that
        // uses the pipeline. If we use the methods that look synchronous (e.g.
        // Device::CreateRenderPipeline) they actually operate asynchronously on WebGPU but the
        // browser becomes responsible for synchronizing when we call submit.
        fUseAsyncPipelineCreation = false;

        // The implementation busy waits after popping.
        fAllowScopedErrorChecks = false;
    }

    fFullCompressedUploadSizeMustAlignToBlockDims = true;
}

void DawnCaps::initShaderCaps(const wgpu::Device& device) {
    SkSL::ShaderCaps* shaderCaps = fShaderCaps.get();

    // WGSL does not support infinities regardless of hardware support. There are discussions around
    // enabling it using an extension in the future.
    shaderCaps->fInfinitySupport = false;

    // WGSL supports shader derivatives in the fragment shader
    shaderCaps->fShaderDerivativeSupport = true;

#if !defined(__EMSCRIPTEN__)
    if (device.HasFeature(wgpu::FeatureName::DualSourceBlending)) {
        shaderCaps->fDualSourceBlendingSupport = true;
    }
    if (device.HasFeature(wgpu::FeatureName::FramebufferFetch)) {
        shaderCaps->fFBFetchSupport = true;
    }
#endif
}

void DawnCaps::initFormatTable(const wgpu::Device& device) {
    // NOTE: wgpu::TextureFormat's naming convention orders channels from least significant to most,
    // matching the data address ordering of a little endian system.
    FormatInfo* info;
    // Format: RGBA8Unorm
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::RGBA8Unorm)];
        info->fFlags = FormatInfo::kAllFlags;
        info->fColorTypeInfoCount = 2;
        info->fColorTypeInfos = std::make_unique<ColorTypeInfo[]>(info->fColorTypeInfoCount);
        int ctIdx = 0;
        // Format: RGBA8Unorm, Surface: kRGBA_8888
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kRGBA_8888_SkColorType;
            ctInfo.fTransferColorType = kRGBA_8888_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag | ColorTypeInfo::kRenderable_Flag;
        }
        // Format: RGBA8Unorm, Surface: kRGB_888x
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kRGB_888x_SkColorType;
            ctInfo.fTransferColorType = kRGB_888x_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag;
            ctInfo.fReadSwizzle = skgpu::Swizzle::RGB1();
        }
    }

    // Format: R8Unorm
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::R8Unorm)];
#if !defined(__EMSCRIPTEN__)
        info->fFlags = FormatInfo::kAllFlags;
        if (!device.HasFeature(wgpu::FeatureName::R8UnormStorage)) {
            info->fFlags &= ~FormatInfo::kStorage_Flag;
        }
#else
        info->fFlags = FormatInfo::kAllFlags & ~FormatInfo::kStorage_Flag;
#endif
        info->fColorTypeInfoCount = 3;
        info->fColorTypeInfos = std::make_unique<ColorTypeInfo[]>(info->fColorTypeInfoCount);
        int ctIdx = 0;
        // Format: R8Unorm, Surface: kR8_unorm
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kR8_unorm_SkColorType;
            ctInfo.fTransferColorType = kR8_unorm_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag | ColorTypeInfo::kRenderable_Flag;
        }
        // Format: R8Unorm, Surface: kAlpha_8
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kAlpha_8_SkColorType;
            ctInfo.fTransferColorType = kAlpha_8_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag | ColorTypeInfo::kRenderable_Flag;
            ctInfo.fReadSwizzle = skgpu::Swizzle("000r");
            ctInfo.fWriteSwizzle = skgpu::Swizzle("a000");
        }
        // Format: R8Unorm, Surface: kGray_8
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kGray_8_SkColorType;
            ctInfo.fTransferColorType = kGray_8_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag;
            ctInfo.fReadSwizzle = skgpu::Swizzle("rrr1");
        }
    }

#if !defined(__EMSCRIPTEN__)
    const bool supportUnorm16 = device.HasFeature(wgpu::FeatureName::Unorm16TextureFormats);
    // TODO(crbug.com/dawn/1856): Support storage binding for compute shader in Dawn.
    // Format: R16Unorm
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::R16Unorm)];
        if (supportUnorm16) {
            info->fFlags = FormatInfo::kAllFlags & ~FormatInfo::kStorage_Flag;
            info->fColorTypeInfoCount = 1;
            info->fColorTypeInfos = std::make_unique<ColorTypeInfo[]>(info->fColorTypeInfoCount);
            int ctIdx = 0;
            // Format: R16Unorm, Surface: kA16_unorm
            {
                auto& ctInfo = info->fColorTypeInfos[ctIdx++];
                ctInfo.fColorType = kA16_unorm_SkColorType;
                ctInfo.fTransferColorType = kA16_unorm_SkColorType;
                ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag | ColorTypeInfo::kRenderable_Flag;
                ctInfo.fReadSwizzle = skgpu::Swizzle("000r");
                ctInfo.fWriteSwizzle = skgpu::Swizzle("a000");
            }
        }
    }
#endif

    // Format: BGRA8Unorm
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::BGRA8Unorm)];
        info->fFlags = FormatInfo::kAllFlags;
        info->fColorTypeInfoCount = 2;
        info->fColorTypeInfos = std::make_unique<ColorTypeInfo[]>(info->fColorTypeInfoCount);
        int ctIdx = 0;
        // Format: BGRA8Unorm, Surface: kBGRA_8888
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kBGRA_8888_SkColorType;
            ctInfo.fTransferColorType = kBGRA_8888_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag | ColorTypeInfo::kRenderable_Flag;
        }
        // Format: BGRA8Unorm, Surface: kRGB_888x
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kRGB_888x_SkColorType;
            // There is no kBGR_888x color type, so report that the data is BGRA and rely on
            // SkConvertPixels to force alpha to opaque when kRGB_888x is either the src or dst type
            ctInfo.fTransferColorType = kBGRA_8888_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag;
        }
    }

    // Format: RGBA16Float
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::RGBA16Float)];
        info->fFlags = FormatInfo::kAllFlags;
        info->fColorTypeInfoCount = 2;
        info->fColorTypeInfos = std::make_unique<ColorTypeInfo[]>(info->fColorTypeInfoCount);
        int ctIdx = 0;
        // Format: RGBA16Float, Surface: RGBA_F16
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kRGBA_F16_SkColorType;
            ctInfo.fTransferColorType = kRGBA_F16_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag | ColorTypeInfo::kRenderable_Flag;
        }
        // Format: RGBA16Float, Surface: RGB_F16F16F16x
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kRGB_F16F16F16x_SkColorType;
            ctInfo.fTransferColorType = kRGB_F16F16F16x_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag;
            ctInfo.fReadSwizzle = skgpu::Swizzle::RGB1();
        }
    }

    // Format: R16Float
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::R16Float)];
        info->fFlags = FormatInfo::kAllFlags;
        info->fColorTypeInfoCount = 1;
        info->fColorTypeInfos = std::make_unique<ColorTypeInfo[]>(info->fColorTypeInfoCount);
        int ctIdx = 0;
        // Format: R16Float, Surface: kA16_float
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kA16_float_SkColorType;
            ctInfo.fTransferColorType = kA16_float_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag | ColorTypeInfo::kRenderable_Flag;
            ctInfo.fReadSwizzle = skgpu::Swizzle("000r");
            ctInfo.fWriteSwizzle = skgpu::Swizzle("a000");
        }
    }

    // TODO(crbug.com/dawn/1856): Support storage binding for compute shader in Dawn.
    // Format: RG8Unorm
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::RG8Unorm)];
        info->fFlags = FormatInfo::kAllFlags;
        info->fColorTypeInfoCount = 1;
        info->fColorTypeInfos = std::make_unique<ColorTypeInfo[]>(info->fColorTypeInfoCount);
        int ctIdx = 0;
        // Format: RG8Unorm, Surface: kR8G8_unorm
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kR8G8_unorm_SkColorType;
            ctInfo.fTransferColorType = kR8G8_unorm_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag | ColorTypeInfo::kRenderable_Flag;
        }
    }

#if !defined(__EMSCRIPTEN__)
    // TODO(crbug.com/dawn/1856): Support storage binding for compute shader in Dawn.
    // Format: RG16Unorm
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::RG16Unorm)];
        if (supportUnorm16) {
            info->fFlags = FormatInfo::kAllFlags;
            info->fColorTypeInfoCount = 1;
            info->fColorTypeInfos = std::make_unique<ColorTypeInfo[]>(info->fColorTypeInfoCount);
            int ctIdx = 0;
            // Format: RG16Unorm, Surface: kR16G16_unorm
            {
                auto& ctInfo = info->fColorTypeInfos[ctIdx++];
                ctInfo.fColorType = kR16G16_unorm_SkColorType;
                ctInfo.fTransferColorType = kR16G16_unorm_SkColorType;
                ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag | ColorTypeInfo::kRenderable_Flag;
            }
        }
    }
#endif

    // Format: RGB10A2Unorm
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::RGB10A2Unorm)];
        info->fFlags = FormatInfo::kAllFlags;
        info->fColorTypeInfoCount = 2;
        info->fColorTypeInfos = std::make_unique<ColorTypeInfo[]>(info->fColorTypeInfoCount);
        int ctIdx = 0;
        // Format: RGB10A2Unorm, Surface: kRGBA_1010102
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kRGBA_1010102_SkColorType;
            ctInfo.fTransferColorType = kRGBA_1010102_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag | ColorTypeInfo::kRenderable_Flag;
        }
        // Format: RGB10A2Unorm, Surface: kRGB_101010x
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kRGB_101010x_SkColorType;
            ctInfo.fTransferColorType = kRGB_101010x_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag;
            ctInfo.fReadSwizzle = skgpu::Swizzle::RGB1();
        }
    }

    // Format: RG16Float
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::RG16Float)];
        info->fFlags = FormatInfo::kAllFlags;
        info->fColorTypeInfoCount = 1;
        info->fColorTypeInfos = std::make_unique<ColorTypeInfo[]>(info->fColorTypeInfoCount);
        int ctIdx = 0;
        // Format: RG16Float, Surface: kR16G16_float
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kR16G16_float_SkColorType;
            ctInfo.fTransferColorType = kR16G16_float_SkColorType;
            ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag | ColorTypeInfo::kRenderable_Flag;
        }
    }

    // Format: ETC2RGB8Unorm
    {
        if (device.HasFeature(wgpu::FeatureName::TextureCompressionETC2)) {
            info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::ETC2RGB8Unorm)];
            info->fFlags = FormatInfo::kTexturable_Flag;
            info->fColorTypeInfoCount = 1;
            info->fColorTypeInfos = std::make_unique<ColorTypeInfo[]>(info->fColorTypeInfoCount);
            int ctIdx = 0;
            // Format: ETC2RGB8Unorm, Surface: kRGB_888x
            {
                auto& ctInfo = info->fColorTypeInfos[ctIdx++];
                ctInfo.fColorType = kRGB_888x_SkColorType;
                ctInfo.fTransferColorType = kRGB_888x_SkColorType;
                ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag;
            }
        }
    }

    // Format: BC1RGBAUnorm
    {
        if (device.HasFeature(wgpu::FeatureName::TextureCompressionBC)) {
            info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::BC1RGBAUnorm)];
            info->fFlags = FormatInfo::kTexturable_Flag;
            info->fColorTypeInfoCount = 1;
            info->fColorTypeInfos = std::make_unique<ColorTypeInfo[]>(info->fColorTypeInfoCount);
            int ctIdx = 0;
            // Format: BC1RGBAUnorm, Surface: kRGBA_8888
            {
                auto& ctInfo = info->fColorTypeInfos[ctIdx++];
                ctInfo.fColorType = kRGBA_8888_SkColorType;
                ctInfo.fTransferColorType = kRGBA_8888_SkColorType;
                ctInfo.fFlags = ColorTypeInfo::kUploadData_Flag;
            }
        }
    }

    /*
     * Non-color formats
     */

    // Format: Stencil8
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::Stencil8)];
        info->fFlags = FormatInfo::kMSAA_Flag;
        info->fColorTypeInfoCount = 0;
    }

    // Format: Depth16UNorm
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::Depth16Unorm)];
        info->fFlags = FormatInfo::kMSAA_Flag;
        info->fColorTypeInfoCount = 0;
    }

    // Format: Depth32Float
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::Depth32Float)];
        info->fFlags = FormatInfo::kMSAA_Flag;
        info->fColorTypeInfoCount = 0;
    }

    // Format: Depth24PlusStencil8
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::Depth24PlusStencil8)];
        info->fFlags = FormatInfo::kMSAA_Flag;
        info->fColorTypeInfoCount = 0;
    }

#if !defined(__EMSCRIPTEN__)
    // Format: External
    {
        info = &fFormatTable[GetFormatIndex(wgpu::TextureFormat::External)];
        info->fFlags = FormatInfo::kTexturable_Flag;
        info->fColorTypeInfoCount = 1;
        info->fColorTypeInfos = std::make_unique<ColorTypeInfo[]>(info->fColorTypeInfoCount);
        int ctIdx = 0;
        // Format: External, Surface: kRGBA_8888
        {
            auto& ctInfo = info->fColorTypeInfos[ctIdx++];
            ctInfo.fColorType = kRGBA_8888_SkColorType;
        }
    }
#endif

    ////////////////////////////////////////////////////////////////////////////
    // Map SkColorTypes (used for creating SkSurfaces) to wgpu::TextureFormat.
    // The order in which the formats are passed into the setColorType function
    // indicates the priority in selecting which format we use for a given
    // SkColorType.

    std::fill_n(fColorTypeToFormatTable, kSkColorTypeCnt, wgpu::TextureFormat::Undefined);

    this->setColorType(kAlpha_8_SkColorType,          { wgpu::TextureFormat::R8Unorm });
    this->setColorType(kRGBA_8888_SkColorType,        { wgpu::TextureFormat::RGBA8Unorm });
    this->setColorType(kRGB_888x_SkColorType,
                       {wgpu::TextureFormat::RGBA8Unorm, wgpu::TextureFormat::BGRA8Unorm});
    this->setColorType(kBGRA_8888_SkColorType,        { wgpu::TextureFormat::BGRA8Unorm });
    this->setColorType(kGray_8_SkColorType,           { wgpu::TextureFormat::R8Unorm });
    this->setColorType(kR8_unorm_SkColorType,         { wgpu::TextureFormat::R8Unorm });
    this->setColorType(kRGBA_F16_SkColorType,         { wgpu::TextureFormat::RGBA16Float });
    this->setColorType(kRGB_F16F16F16x_SkColorType,   { wgpu::TextureFormat::RGBA16Float });
    this->setColorType(kA16_float_SkColorType,        { wgpu::TextureFormat::R16Float });
    this->setColorType(kR8G8_unorm_SkColorType,       { wgpu::TextureFormat::RG8Unorm });
    this->setColorType(kRGBA_1010102_SkColorType,     { wgpu::TextureFormat::RGB10A2Unorm });
    this->setColorType(kRGB_101010x_SkColorType,      { wgpu::TextureFormat::RGB10A2Unorm });
    this->setColorType(kR16G16_float_SkColorType,     { wgpu::TextureFormat::RG16Float });

#if !defined(__EMSCRIPTEN__)
    this->setColorType(kA16_unorm_SkColorType,        { wgpu::TextureFormat::R16Unorm });
    this->setColorType(kR16G16_unorm_SkColorType,     { wgpu::TextureFormat::RG16Unorm });
#endif
}

// static
size_t DawnCaps::GetFormatIndex(wgpu::TextureFormat format) {
    for (size_t i = 0; i < std::size(kFormats); ++i) {
        if (format == kFormats[i]) {
            return i;
        }
    }
    SkDEBUGFAILF("Unsupported wgpu::TextureFormat: 0x%08X\n", static_cast<uint32_t>(format));
    return 0;
}

void DawnCaps::setColorType(SkColorType colorType,
                            std::initializer_list<wgpu::TextureFormat> formats) {
    static_assert(std::size(kFormats) <= kFormatCount,
                  "Size is not compatible for DawnCaps::fFormatTable and kFormats");
    int idx = static_cast<int>(colorType);
    for (auto it = formats.begin(); it != formats.end(); ++it) {
        const auto& info = this->getFormatInfo(*it);
        for (int i = 0; i < info.fColorTypeInfoCount; ++i) {
            if (info.fColorTypeInfos[i].fColorType == colorType) {
                fColorTypeToFormatTable[idx] = *it;
                return;
            }
        }
    }
}

bool DawnCaps::serializeTextureInfo(const TextureInfo& info, SkWStream* out) const {
    return TextureInfoPriv::Serialize<DawnTextureInfo>(info, out);
}

bool DawnCaps::deserializeTextureInfo(SkStream* stream, TextureInfo* out) const {
    return TextureInfoPriv::Deserialize<DawnTextureInfo>(stream, out);
}

// Make sure the format table indices will fit into the packed bits, with room to spare for
// representing an unused attachment.
static constexpr int kFormatBits = 11; // x2 attachments (color & depthStencil formats)
static constexpr int kSampleBits = 4;  // x2 attachments (color & depthStencil numSamples)
static constexpr int kResolveBits = 1;
static constexpr int kUnusedAttachmentIndex = (1 << kFormatBits) - 1;
static_assert(2*(kFormatBits + kSampleBits) + kResolveBits <= 32);
static_assert(std::size(kFormats) <= kUnusedAttachmentIndex);

static constexpr int kDepthStencilNumSamplesOffset = kResolveBits;
static constexpr int kDepthStencilFormatOffset = kDepthStencilNumSamplesOffset + kSampleBits;
static constexpr int kColorNumSamplesOffset = kDepthStencilFormatOffset + kFormatBits;
static constexpr int kColorFormatOffset = kColorNumSamplesOffset + kSampleBits;

static constexpr uint32_t kFormatMask     = (1 << kFormatBits) - 1;
static constexpr uint32_t kNumSamplesMask = (1 << kSampleBits) - 1;
static constexpr uint32_t kResolveMask    = (1 << kResolveBits) - 1;

uint32_t DawnCaps::getRenderPassDescKeyForPipeline(const RenderPassDesc& renderPassDesc) const {
    const TextureInfo& colorInfo = renderPassDesc.fColorAttachment.fTextureInfo;
    const TextureInfo& depthStencilInfo = renderPassDesc.fDepthStencilAttachment.fTextureInfo;
    // The color attachment should be valid; the depth-stencil attachment may not be if it's not
    // being used.
    SkASSERT(colorInfo.isValid());

    // Use format indices instead of WGPUTextureFormat values since they can be larger than 16 bits.
    uint32_t colorFormatIndex =
            GetFormatIndex(TextureInfoPriv::Get<DawnTextureInfo>(colorInfo).getViewFormat());
    uint32_t depthStencilFormatIndex = depthStencilInfo.isValid()
            ? GetFormatIndex(
                    TextureInfoPriv::Get<DawnTextureInfo>(depthStencilInfo).getViewFormat())
            : kUnusedAttachmentIndex;

    // Note: if Dawn supports ExpandResolveTexture load op and the render pass uses it to load
    // the resolve texture, a render pipeline will need to be created with
    // wgpu::ColorTargetStateExpandResolveTextureDawn chained struct in order to be compatible.
    // Hence a render pipeline created for a render pass using ExpandResolveTexture load op will
    // be different from the one created for a render pass not using that load op.
    // So we need to include a bit flag to differentiate the two kinds of pipelines.
    // Also avoid returning a cached pipeline that is not compatible with the render pass using
    // ExpandResolveTexture load op and vice versa.
    const bool shouldIncludeLoadResolveAttachmentBit = this->loadOpAffectsMSAAPipelines();
    uint32_t loadResolveAttachmentKey = 0;
    if (shouldIncludeLoadResolveAttachmentBit &&
        renderPassDesc.fColorResolveAttachment.fTextureInfo.isValid() &&
        renderPassDesc.fColorResolveAttachment.fLoadOp == LoadOp::kLoad) {
        loadResolveAttachmentKey = 1;
    }

    SkASSERT(colorFormatIndex < (1 << kFormatBits));
    SkASSERT(colorInfo.numSamples() < (1 << kSampleBits));
    SkASSERT(depthStencilFormatIndex < (1 << kFormatBits));
    SkASSERT(depthStencilInfo.numSamples() < (1 << kSampleBits));
    SkASSERT(loadResolveAttachmentKey < (1 << kResolveBits));

    return (colorFormatIndex              << kColorFormatOffset) |
           (colorInfo.numSamples()        << kColorNumSamplesOffset) |
           (depthStencilFormatIndex       << kDepthStencilFormatOffset) |
           (depthStencilInfo.numSamples() << kDepthStencilNumSamplesOffset) |
           loadResolveAttachmentKey;
}

static constexpr int kDawnGraphicsPipelineKeyData32Count = 4;

UniqueKey DawnCaps::makeGraphicsPipelineKey(const GraphicsPipelineDesc& pipelineDesc,
                                            const RenderPassDesc& renderPassDesc) const {
    UniqueKey pipelineKey;
    {
        // 4 uint32_t's (render step id, paint id, uint32 RenderPassDesc, uint16 write swizzle key)
        UniqueKey::Builder builder(&pipelineKey, get_pipeline_domain(),
                                   kDawnGraphicsPipelineKeyData32Count, "DawnGraphicsPipeline");
        // Add GraphicsPipelineDesc key.
        builder[0] = static_cast<uint32_t>(pipelineDesc.renderStepID());
        builder[1] = pipelineDesc.paintParamsID().asUInt();

        // Add RenderPassDesc key and write swizzle (which is separate from the RenderPassDescKey
        // because it is applied in the program writing to the target, and is not actually part of
        // the underlying GPU render pass config).
        builder[2] = this->getRenderPassDescKeyForPipeline(renderPassDesc);
        builder[3] = renderPassDesc.fWriteSwizzle.asKey();
        builder.finish();
    }

    return pipelineKey;
}

bool DawnCaps::extractGraphicsDescs(const UniqueKey& key,
                                    GraphicsPipelineDesc* pipelineDesc,
                                    RenderPassDesc* renderPassDesc,
                                    const RendererProvider* rendererProvider) const {
    SkASSERT(key.domain() == get_pipeline_domain());
    SkASSERT(key.dataSize() == 4 * kDawnGraphicsPipelineKeyData32Count);

    const uint32_t* rawKeyData = key.data();

    SkASSERT(RenderStep::IsValidRenderStepID(rawKeyData[0]));
    RenderStep::RenderStepID renderStepID = static_cast<RenderStep::RenderStepID>(rawKeyData[0]);

    SkDEBUGCODE(const RenderStep* renderStep = rendererProvider->lookup(renderStepID);)
    *pipelineDesc = GraphicsPipelineDesc(renderStepID, UniquePaintParamsID(rawKeyData[1]));
    SkASSERT(renderStep->performsShading() == pipelineDesc->paintParamsID().isValid());

    uint32_t renderpassDescBits = rawKeyData[2];
    uint32_t colorFormatIndex = (renderpassDescBits >> kColorFormatOffset) & kFormatMask;
    SkASSERT(colorFormatIndex < std::size(kFormats));

    DawnTextureInfo dawnInfo;
    dawnInfo.fFormat = dawnInfo.fViewFormat = kFormats[colorFormatIndex];
    dawnInfo.fSampleCount  = 1;
    dawnInfo.fMipmapped = skgpu::Mipmapped::kNo;
    dawnInfo.fUsage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;

    uint32_t colorSampleCount = (renderpassDescBits >> kColorNumSamplesOffset) & kNumSamplesMask;
    bool requiresMSAA = colorSampleCount > 1;

    SkEnumBitMask<DepthStencilFlags> dsFlags = DepthStencilFlags::kNone;

    uint32_t depthStencilFormatIndex =
            (renderpassDescBits >> kDepthStencilFormatOffset) & kFormatMask;
    if (depthStencilFormatIndex != kUnusedAttachmentIndex) {
        SkASSERT(depthStencilFormatIndex < std::size(kFormats));
        wgpu::TextureFormat dsFormat = kFormats[depthStencilFormatIndex];
        if (DawnFormatIsDepth(dsFormat)) {
            dsFlags |= DepthStencilFlags::kDepth;
        }
        if (DawnFormatIsStencil(dsFormat)) {
            dsFlags |= DepthStencilFlags::kStencil;
        }
    }
    SkDEBUGCODE(uint32_t dsSampleCount =
                    (renderpassDescBits >> kDepthStencilNumSamplesOffset) & kNumSamplesMask;)
    SkASSERT(colorSampleCount == dsSampleCount);

    LoadOp loadOp = LoadOp::kClear;
    if (renderpassDescBits & kResolveMask) {
        // This bit should only be set if Dawn supports ExpandResolveTexture load op
        SkASSERT(this->loadOpAffectsMSAAPipelines());
        loadOp = LoadOp::kLoad;
    }

    Swizzle writeSwizzle = SwizzleCtorAccessor::Make(rawKeyData[3]);
    TextureInfo targetTexInfo = TextureInfos::MakeDawn(dawnInfo);
    *renderPassDesc = RenderPassDesc::Make(this,
                                           targetTexInfo,
                                           loadOp,
                                           StoreOp::kStore,
                                           dsFlags,
                                           /* clearColor= */ { .0f, .0f, .0f, .0f },
                                           requiresMSAA,
                                           writeSwizzle,
                                           this->getDstReadStrategy(targetTexInfo));

    return true;
}

UniqueKey DawnCaps::makeComputePipelineKey(const ComputePipelineDesc& pipelineDesc) const {
    UniqueKey pipelineKey;
    {
        static const skgpu::UniqueKey::Domain kComputePipelineDomain = UniqueKey::GenerateDomain();
        // The key is made up of a single uint32_t corresponding to the compute step ID.
        UniqueKey::Builder builder(&pipelineKey, kComputePipelineDomain, 1, "ComputePipeline");
        builder[0] = pipelineDesc.computeStep()->uniqueID();

        // TODO(b/240615224): The local work group size should factor into the key here since it is
        // specified in the shader text on Dawn/SPIR-V. This is not a problem right now since
        // ComputeSteps don't vary their workgroup size dynamically.

        builder.finish();
    }
    return pipelineKey;
}

ImmutableSamplerInfo DawnCaps::getImmutableSamplerInfo(const TextureInfo& textureInfo) const {
#if !defined(__EMSCRIPTEN__)
    const wgpu::YCbCrVkDescriptor& ycbcrConversionInfo =
            TextureInfoPriv::Get<DawnTextureInfo>(textureInfo).fYcbcrVkDescriptor;

    if (DawnDescriptorIsValid(ycbcrConversionInfo)) {
        return DawnDescriptorToImmutableSamplerInfo(ycbcrConversionInfo);
    }
#endif

    // If the YCbCr conversion for is invalid, then return a default ImmutableSamplerInfo struct.
    return {};
}

void DawnCaps::buildKeyForTexture(SkISize dimensions,
                                  const TextureInfo& info,
                                  ResourceType type,
                                  GraphiteResourceKey* key) const {
    const auto& dawnInfo = TextureInfoPriv::Get<DawnTextureInfo>(info);

    SkASSERT(!dimensions.isEmpty());

    SkASSERT(dawnInfo.getViewFormat() != wgpu::TextureFormat::Undefined);
    uint32_t formatKey = static_cast<uint32_t>(dawnInfo.getViewFormat());

    uint32_t samplesKey = SamplesToKey(info.numSamples());
    // We don't have to key the number of mip levels because it is inherit in the combination of
    // isMipped and dimensions.
    bool isMipped = info.mipmapped() == Mipmapped::kYes;

    // Confirm all the below parts of the key can fit in a single uint32_t. The sum of the shift
    // amounts in the asserts must be less than or equal to 32.
    SkASSERT(samplesKey                             < (1u << 3));  // sample key is first 3 bits
    SkASSERT(static_cast<uint32_t>(isMipped)        < (1u << 1));  // isMapped is 4th bit
    SkASSERT(static_cast<uint32_t>(dawnInfo.fUsage) < (1u << 28)); // usage is remaining 28 bits

    // We need two uint32_ts for dimensions, 1 for format, and 1 for the rest of the key;
    int num32DataCnt = 2 + 1 + 1;
    bool hasYcbcrInfo = false;
#if !defined(__EMSCRIPTEN__)
    // If we are using ycbcr texture/sampling, more key information is needed.
    if ((hasYcbcrInfo = DawnDescriptorIsValid(dawnInfo.fYcbcrVkDescriptor))) {
        num32DataCnt += 3; // non-format flags and 64-bit format
    }
#endif
    GraphiteResourceKey::Builder builder(key, type, num32DataCnt);

    builder[0] = dimensions.width();
    builder[1] = dimensions.height();
    builder[2] = formatKey;
    builder[3] = (samplesKey                                   << 0) |
                 (static_cast<uint32_t>(isMipped)              << 3) |
                 (static_cast<uint32_t>(dawnInfo.fUsage)       << 4);

#if !defined(__EMSCRIPTEN__)
    if (hasYcbcrInfo) {
        ImmutableSamplerInfo packedInfo =
                DawnDescriptorToImmutableSamplerInfo(dawnInfo.fYcbcrVkDescriptor);
        builder[4] = packedInfo.fNonFormatYcbcrConversionInfo;
        // Even though we already have formatKey appended to the texture key, we still need to add
        // fYcbcrVkDescriptor's vkFormat or externalFormat. The latter two are distinct from
        // dawnSpec's wgpu::TextureFormat.
        builder[5] = (uint32_t) packedInfo.fFormat;
        builder[6] = (uint32_t) (packedInfo.fFormat >> 32);
    }
#endif
}

} // namespace skgpu::graphite
