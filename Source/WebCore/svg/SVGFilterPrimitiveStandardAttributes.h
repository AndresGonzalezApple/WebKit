/*
 * Copyright (C) 2004, 2005, 2006, 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2006 Rob Buis <buis@kde.org>
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

#pragma once

#include "FilterEffectGeometry.h"
#include "FilterEffectVector.h"
#include "SVGAnimatedPropertyImpl.h"
#include "SVGElement.h"
#include "SVGNames.h"
#include "SVGUnitTypes.h"
#include <wtf/RefPtr.h>
#include <wtf/TZoneMalloc.h>

namespace WebCore {

class Filter;
class FilterEffect;
class SVGFilter;

class SVGFilterPrimitiveStandardAttributes : public SVGElement {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(SVGFilterPrimitiveStandardAttributes);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(SVGFilterPrimitiveStandardAttributes);
public:
    using PropertyRegistry = SVGPropertyOwnerRegistry<SVGFilterPrimitiveStandardAttributes, SVGElement>;

    const SVGLengthValue& x() const { return m_x->currentValue(); }
    const SVGLengthValue& y() const { return m_y->currentValue(); }
    const SVGLengthValue& width() const { return m_width->currentValue(); }
    const SVGLengthValue& height() const { return m_height->currentValue(); }
    String result() const { return m_result->currentValue(); }

    SVGAnimatedLength& xAnimated() { return m_x; }
    SVGAnimatedLength& yAnimated() { return m_y; }
    SVGAnimatedLength& widthAnimated() { return m_width; }
    SVGAnimatedLength& heightAnimated() { return m_height; }
    SVGAnimatedString& resultAnimated() { return m_result; }

    OptionSet<FilterEffectGeometry::Flags> effectGeometryFlags() const;

    virtual Vector<AtomString> filterEffectInputsNames() const { return { }; }
    virtual bool isIdentity() const { return false; }
    virtual IntOutsets outsets(const FloatRect&, SVGUnitTypes::SVGUnitType) const { return { }; }
    RefPtr<FilterEffect> filterEffect(const FilterEffectVector&, const GraphicsContext& destinationContext);

    void primitiveAttributeChanged(const QualifiedName&);
    void markFilterEffectForRepaint();
    void markFilterEffectForRebuild();

    static void invalidateFilterPrimitiveParent(SVGElement*);

protected:
    SVGFilterPrimitiveStandardAttributes(const QualifiedName&, Document&, UniqueRef<SVGPropertyRegistry>&&);

    void attributeChanged(const QualifiedName&, const AtomString& oldValue, const AtomString& newValue, AttributeModificationReason) override;
    void svgAttributeChanged(const QualifiedName&) override;
    void childrenChanged(const ChildChange&) override;
    void primitiveAttributeOnChildChanged(const Element&, const QualifiedName&);

    virtual bool setFilterEffectAttribute(FilterEffect&, const QualifiedName&) { return false; }
    virtual bool setFilterEffectAttributeFromChild(FilterEffect&, const Element&, const QualifiedName&) { return false; }
    virtual RefPtr<FilterEffect> createFilterEffect(const FilterEffectVector&, const GraphicsContext& destinationContext) const = 0;

private:
    bool isFilterEffect() const override { return true; }

    RenderPtr<RenderElement> createElementRenderer(RenderStyle&&, const RenderTreePosition&) override;
    bool rendererIsNeeded(const RenderStyle&) override;
    bool childShouldCreateRenderer(const Node&) const override { return false; }

    RefPtr<FilterEffect> m_effect;

    // Spec: If the x/y attribute is not specified, the effect is as if a value of "0%" were specified.
    // Spec: If the width/height attribute is not specified, the effect is as if a value of "100%" were specified.
    Ref<SVGAnimatedLength> m_x { SVGAnimatedLength::create(this, SVGLengthMode::Width, "0%"_s) };
    Ref<SVGAnimatedLength> m_y { SVGAnimatedLength::create(this, SVGLengthMode::Height, "0%"_s) };
    Ref<SVGAnimatedLength> m_width { SVGAnimatedLength::create(this, SVGLengthMode::Width, "100%"_s) };
    Ref<SVGAnimatedLength> m_height { SVGAnimatedLength::create(this, SVGLengthMode::Height, "100%"_s) };
    Ref<SVGAnimatedString> m_result { SVGAnimatedString::create(this) };
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::SVGFilterPrimitiveStandardAttributes)
    static bool isType(const WebCore::SVGElement& element) { return element.isFilterEffect(); }
    static bool isType(const WebCore::Node& node)
    {
        auto* svgElement = dynamicDowncast<WebCore::SVGElement>(node);
        return svgElement && isType(*svgElement);
    }
SPECIALIZE_TYPE_TRAITS_END()
