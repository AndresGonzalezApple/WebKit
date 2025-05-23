/*
 * Copyright (C) 2004, 2005, 2006, 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2006 Rob Buis <buis@kde.org>
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) 2018-2024 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "SVGFilterPrimitiveStandardAttributes.h"

#include "ContainerNodeInlines.h"
#include "FilterEffect.h"
#include "LegacyRenderSVGResourceFilterPrimitive.h"
#include "NodeName.h"
#include "RenderSVGResourceFilterPrimitive.h"
#include "SVGElementInlines.h"
#include "SVGParsingError.h"
#include "SVGPropertyOwnerRegistry.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(SVGFilterPrimitiveStandardAttributes);

SVGFilterPrimitiveStandardAttributes::SVGFilterPrimitiveStandardAttributes(const QualifiedName& tagName, Document& document, UniqueRef<SVGPropertyRegistry>&& propertyRegistry)
    : SVGElement(tagName, document, WTFMove(propertyRegistry))
{
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] {
        PropertyRegistry::registerProperty<SVGNames::xAttr, &SVGFilterPrimitiveStandardAttributes::m_x>();
        PropertyRegistry::registerProperty<SVGNames::yAttr, &SVGFilterPrimitiveStandardAttributes::m_y>();
        PropertyRegistry::registerProperty<SVGNames::widthAttr, &SVGFilterPrimitiveStandardAttributes::m_width>();
        PropertyRegistry::registerProperty<SVGNames::heightAttr, &SVGFilterPrimitiveStandardAttributes::m_height>();
        PropertyRegistry::registerProperty<SVGNames::resultAttr, &SVGFilterPrimitiveStandardAttributes::m_result>();
    });
}

void SVGFilterPrimitiveStandardAttributes::attributeChanged(const QualifiedName& name, const AtomString& oldValue, const AtomString& newValue, AttributeModificationReason attributeModificationReason)
{
    auto parseError = SVGParsingError::None;

    switch (name.nodeName()) {
    case AttributeNames::xAttr:
        Ref { m_x }->setBaseValInternal(SVGLengthValue::construct(SVGLengthMode::Width, newValue, parseError));
        break;
    case AttributeNames::yAttr:
        Ref { m_y }->setBaseValInternal(SVGLengthValue::construct(SVGLengthMode::Height, newValue, parseError));
        break;
    case AttributeNames::widthAttr:
        Ref { m_width }->setBaseValInternal(SVGLengthValue::construct(SVGLengthMode::Width, newValue, parseError));
        break;
    case AttributeNames::heightAttr:
        Ref { m_height }->setBaseValInternal(SVGLengthValue::construct(SVGLengthMode::Height, newValue, parseError));
        break;
    case AttributeNames::resultAttr:
        Ref { m_result }->setBaseValInternal(newValue);
        break;
    default:
        break;
    }
    reportAttributeParsingError(parseError, name, newValue);

    SVGElement::attributeChanged(name, oldValue, newValue, attributeModificationReason);
}

OptionSet<FilterEffectGeometry::Flags> SVGFilterPrimitiveStandardAttributes::effectGeometryFlags() const
{
    OptionSet<FilterEffectGeometry::Flags> flags;

    if (hasAttribute(SVGNames::xAttr))
        flags.add(FilterEffectGeometry::Flags::HasX);
    if (hasAttribute(SVGNames::yAttr))
        flags.add(FilterEffectGeometry::Flags::HasY);
    if (hasAttribute(SVGNames::widthAttr))
        flags.add(FilterEffectGeometry::Flags::HasWidth);
    if (hasAttribute(SVGNames::heightAttr))
        flags.add(FilterEffectGeometry::Flags::HasHeight);

    return flags;
}

RefPtr<FilterEffect> SVGFilterPrimitiveStandardAttributes::filterEffect(const FilterEffectVector& inputs, const GraphicsContext& destinationContext)
{
    if (!m_effect)
        m_effect = createFilterEffect(inputs, destinationContext);
    return m_effect;
}

void SVGFilterPrimitiveStandardAttributes::primitiveAttributeChanged(const QualifiedName& attribute)
{
    RefPtr effect = m_effect;
    if (effect && !setFilterEffectAttribute(*effect, attribute))
        return;

    markFilterEffectForRepaint();
}

void SVGFilterPrimitiveStandardAttributes::primitiveAttributeOnChildChanged(const Element& child, const QualifiedName& attribute)
{
    ASSERT(child.parentNode() == this);

    RefPtr effect = m_effect;
    if (effect && !setFilterEffectAttributeFromChild(*effect, child, attribute))
        return;

    markFilterEffectForRepaint();
}

void SVGFilterPrimitiveStandardAttributes::markFilterEffectForRepaint()
{
    CheckedPtr renderer = this->renderer();
    if (!renderer)
        return;

    if (auto* filterPrimitiveRenderer = dynamicDowncast<RenderSVGResourceFilterPrimitive>(renderer.get())) {
        filterPrimitiveRenderer->markFilterEffectForRepaint(m_effect.get());
        return;
    }

    if (auto* filterPrimitiveRenderer = dynamicDowncast<LegacyRenderSVGResourceFilterPrimitive>(renderer.get())) {
        filterPrimitiveRenderer->markFilterEffectForRepaint(m_effect.get());
        return;
    }

    ASSERT_NOT_REACHED();
}

void SVGFilterPrimitiveStandardAttributes::markFilterEffectForRebuild()
{
    m_effect = nullptr;

    CheckedPtr renderer = this->renderer();
    if (!renderer)
        return;

    if (auto* filterPrimitiveRenderer = dynamicDowncast<RenderSVGResourceFilterPrimitive>(renderer.get())) {
        filterPrimitiveRenderer->markFilterEffectForRebuild();
        return;
    }

    if (auto* filterPrimitiveRenderer = dynamicDowncast<LegacyRenderSVGResourceFilterPrimitive>(renderer.get())) {
        filterPrimitiveRenderer->markFilterEffectForRebuild();
        return;
    }

    ASSERT_NOT_REACHED();
}

void SVGFilterPrimitiveStandardAttributes::svgAttributeChanged(const QualifiedName& attrName)
{
    if (PropertyRegistry::isKnownAttribute(attrName)) {
        InstanceInvalidationGuard guard(*this);
        updateSVGRendererForElementChange();
        return;
    }

    SVGElement::svgAttributeChanged(attrName);
}

void SVGFilterPrimitiveStandardAttributes::childrenChanged(const ChildChange& change)
{
    SVGElement::childrenChanged(change);

    if (change.source == ChildChange::Source::Parser)
        return;
    updateSVGRendererForElementChange();
}

RenderPtr<RenderElement> SVGFilterPrimitiveStandardAttributes::createElementRenderer(RenderStyle&& style, const RenderTreePosition&)
{
    if (document().settings().layerBasedSVGEngineEnabled())
        return createRenderer<RenderSVGResourceFilterPrimitive>(*this, WTFMove(style));

    return createRenderer<LegacyRenderSVGResourceFilterPrimitive>(*this, WTFMove(style));
}

bool SVGFilterPrimitiveStandardAttributes::rendererIsNeeded(const RenderStyle& style)
{
    if (parentNode() && (parentNode()->hasTagName(SVGNames::filterTag)))
        return SVGElement::rendererIsNeeded(style);

    return false;
}

void SVGFilterPrimitiveStandardAttributes::invalidateFilterPrimitiveParent(SVGElement* element)
{
    if (!element)
        return;

    RefPtr parent = element->parentNode();
    if (!parent)
        return;

    CheckedPtr renderer = parent->renderer();
    if (!renderer || !renderer->isRenderOrLegacyRenderSVGResourceFilterPrimitive())
        return;

    downcast<SVGElement>(*parent).updateSVGRendererForElementChange();
}

} // namespace WebCore
