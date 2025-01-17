/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
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
#include "LayoutIntegrationLineLayout.h"

#if ENABLE(LAYOUT_FORMATTING_CONTEXT)

#include "DisplayBox.h"
#include "EventRegion.h"
#include "InlineFormattingState.h"
#include "InvalidationState.h"
#include "LayoutContext.h"
#include "LayoutTreeBuilder.h"
#include "PaintInfo.h"
#include "RenderBlockFlow.h"
#include "RenderChildIterator.h"
#include "RenderLineBreak.h"
#include "RuntimeEnabledFeatures.h"
#include "Settings.h"
#include "SimpleLineLayout.h"
#include "TextDecorationPainter.h"
#include "TextPainter.h"

namespace WebCore {
namespace LayoutIntegration {

LineLayout::LineLayout(const RenderBlockFlow& flow)
    : m_flow(flow)
{
    m_treeContent = Layout::TreeBuilder::buildLayoutTree(flow);
}

LineLayout::~LineLayout() = default;

bool LineLayout::canUseFor(const RenderBlockFlow& flow)
{
    if (!RuntimeEnabledFeatures::sharedFeatures().layoutFormattingContextIntegrationEnabled())
        return false;

    // Initially only a subset of SLL features is supported.
    if (!SimpleLineLayout::canUseFor(flow))
        return false;

    if (flow.containsFloats())
        return false;

    if (flow.style().textTransform() == TextTransform::Capitalize)
        return false;

    if (flow.fragmentedFlowState() != RenderObject::NotInsideFragmentedFlow)
        return false;

    return true;
}

void LineLayout::layout()
{
    if (!m_layoutState)
        m_layoutState = makeUnique<Layout::LayoutState>(*m_treeContent);

    prepareRootGeometryForLayout();

    auto layoutContext = Layout::LayoutContext { *m_layoutState };
    auto invalidationState = Layout::InvalidationState { };

    layoutContext.layoutWithPreparedRootGeometry(invalidationState);

    auto& lineBoxes = downcast<Layout::InlineFormattingState>(m_layoutState->establishedFormattingState(rootLayoutBox())).displayInlineContent()->lineBoxes;
    m_contentLogicalHeight = lineBoxes.last().logicalBottom() - lineBoxes.first().logicalTop();
}

void LineLayout::prepareRootGeometryForLayout()
{
    auto& displayBox = m_layoutState->displayBoxForRootLayoutBox();

    // Don't set marging properties or height. These should not be be accessed by inline layout.
    displayBox.setBorder(Layout::Edges { { m_flow.borderStart(), m_flow.borderEnd() }, { m_flow.borderBefore(), m_flow.borderAfter() } });
    displayBox.setPadding(Layout::Edges { { m_flow.paddingStart(), m_flow.paddingEnd() }, { m_flow.paddingBefore(), m_flow.paddingAfter() } });
    displayBox.setContentBoxWidth(m_flow.contentSize().width());
}

size_t LineLayout::lineCount() const
{
    auto* inlineContent = displayInlineContent();
    return inlineContent ? inlineContent->lineBoxes.size() : 0;
}

LayoutUnit LineLayout::firstLineBaseline() const
{
    auto* inlineContent = displayInlineContent();
    if (!inlineContent) {
        ASSERT_NOT_REACHED();
        return 0_lu;
    }

    auto& firstLineBox = inlineContent->lineBoxes.first();
    return Layout::toLayoutUnit(firstLineBox.logicalTop() + firstLineBox.baselineOffset());
}

LayoutUnit LineLayout::lastLineBaseline() const
{
    auto* inlineContent = displayInlineContent();
    if (!inlineContent) {
        ASSERT_NOT_REACHED();
        return 0_lu;
    }

    auto& lastLineBox = inlineContent->lineBoxes.last();
    return Layout::toLayoutUnit(lastLineBox.logicalTop() + lastLineBox.baselineOffset());
}

// FIXME: LFC should handle overflow computations.
static LayoutRect computeVisualOverflow(const RenderStyle& style, const LayoutRect& boxRect, IntSize& viewportSize)
{
    auto overflowRect = boxRect;
    auto strokeOverflow = std::ceil(style.computedStrokeWidth(viewportSize));
    overflowRect.inflate(strokeOverflow);

    auto letterSpacing = style.fontCascade().letterSpacing();
    if (letterSpacing >= 0)
        return overflowRect;
    // Last letter's negative spacing shrinks layout rect. Push it to visual overflow.
    overflowRect.expand(-letterSpacing, 0);
    return overflowRect;
}

void LineLayout::collectOverflow(RenderBlockFlow& flow)
{
    ASSERT(&flow == &m_flow);
    ASSERT(!flow.hasOverflowClip());

    auto viewportSize = m_flow.frame().view()->size();

    for (auto& lineBox : displayInlineContent()->lineBoxes) {
        auto lineRect = Layout::toLayoutRect(lineBox.logicalRect());
        auto visualOverflowRect = computeVisualOverflow(flow.style(), lineRect, viewportSize);
        flow.addLayoutOverflow(lineRect);
        flow.addVisualOverflow(visualOverflowRect);
    }
}

const Display::InlineContent* LineLayout::displayInlineContent() const
{
    return downcast<Layout::InlineFormattingState>(m_layoutState->establishedFormattingState(rootLayoutBox())).displayInlineContent();
}

LineLayoutTraversal::TextBoxIterator LineLayout::textBoxesFor(const RenderText& renderText) const
{
    auto* inlineContent = displayInlineContent();
    if (!inlineContent)
        return { };
    auto* layoutBox = m_treeContent->layoutBoxForRenderer(renderText);
    ASSERT(layoutBox);

    Optional<size_t> firstIndex;
    size_t lastIndex = 0;
    for (size_t i = 0; i < inlineContent->runs.size(); ++i) {
        auto& run = inlineContent->runs[i];
        if (&run.layoutBox() == layoutBox) {
            if (!firstIndex)
                firstIndex = i;
            lastIndex = i;
        } else if (firstIndex)
            break;
    }
    if (!firstIndex)
        return { };

    return { LineLayoutTraversal::DisplayRunPath(*inlineContent, *firstIndex, lastIndex + 1) };
}

LineLayoutTraversal::ElementBoxIterator LineLayout::elementBoxFor(const RenderLineBreak& renderLineBreak) const
{
    auto* inlineContent = displayInlineContent();
    if (!inlineContent)
        return { };
    auto* layoutBox = m_treeContent->layoutBoxForRenderer(renderLineBreak);
    ASSERT(layoutBox);

    for (size_t i = 0; i < inlineContent->runs.size(); ++i) {
        auto& run =  inlineContent->runs[i];
        if (&run.layoutBox() == layoutBox)
            return { LineLayoutTraversal::DisplayRunPath(*inlineContent, i, i + 1) };
    }

    return { };
}

const Layout::Container& LineLayout::rootLayoutBox() const
{
    return m_treeContent->rootLayoutBox();
}

void LineLayout::paint(PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    if (!displayInlineContent())
        return;

    if (paintInfo.phase != PaintPhase::Foreground && paintInfo.phase != PaintPhase::EventRegion)
        return;

    auto& inlineContent = *displayInlineContent();

    auto viewportSize = m_flow.frame().view()->size();
    float deviceScaleFactor = m_flow.document().deviceScaleFactor();

    auto paintRect = paintInfo.rect;
    paintRect.moveBy(-paintOffset);

    for (auto& run : inlineContent.runsForRect(paintRect)) {
        if (!run.textContext())
            continue;

        auto& textContext = *run.textContext();
        if (!textContext.length())
            continue;

        auto& style = run.style();
        if (style.visibility() != Visibility::Visible)
            return;

        auto rect = Layout::toLayoutRect(run.logicalRect());
        auto visualOverflowRect = computeVisualOverflow(style, rect, viewportSize);
        if (paintRect.y() > visualOverflowRect.maxY() || paintRect.maxY() < visualOverflowRect.y())
            continue;

        if (paintInfo.eventRegionContext) {
            if (style.pointerEvents() != PointerEvents::None)
                paintInfo.eventRegionContext->unite(enclosingIntRect(visualOverflowRect), style);
            continue;
        }

        auto& lineBox = inlineContent.lineBoxForRun(run);
        auto baselineOffset = paintOffset.y() + lineBox.logicalTop() + lineBox.baselineOffset();

        auto behavior = textContext.expansion() ? textContext.expansion()->behavior : DefaultExpansion;
        auto horizontalExpansion = textContext.expansion() ? Layout::toLayoutUnit(textContext.expansion()->horizontalExpansion) : 0_lu;
        auto logicalLeft = paintOffset.x() + run.logicalLeft();

        String textWithHyphen;
        if (textContext.needsHyphen())
            textWithHyphen = makeString(textContext.content(), style.hyphenString());
        TextRun textRun { !textWithHyphen.isEmpty() ? textWithHyphen : textContext.content(), logicalLeft, horizontalExpansion, behavior };
        textRun.setTabSize(!style.collapseWhiteSpace(), style.tabSize());
        FloatPoint textOrigin { rect.x() + paintOffset.x(), roundToDevicePixel(baselineOffset, deviceScaleFactor) };

        TextPainter textPainter(paintInfo.context());
        textPainter.setFont(style.fontCascade());
        textPainter.setStyle(computeTextPaintStyle(m_flow.frame(), style, paintInfo));
        if (auto* debugShadow = debugTextShadow())
            textPainter.setShadow(debugShadow);

        textPainter.paint(textRun, rect, textOrigin);

        if (!style.textDecorationsInEffect().isEmpty()) {
            // FIXME: Use correct RenderText.
            if (auto* textRenderer = childrenOfType<RenderText>(m_flow).first()) {
                auto painter = TextDecorationPainter { paintInfo.context(), style.textDecorationsInEffect(), *textRenderer, false, style.fontCascade() };
                painter.setWidth(rect.width());
                painter.paintTextDecoration(textRun, textOrigin, rect.location() + paintOffset);
            }
        }
    }
}

ShadowData* LineLayout::debugTextShadow()
{
    if (!m_flow.settings().simpleLineLayoutDebugBordersEnabled())
        return nullptr;

    static NeverDestroyed<ShadowData> debugTextShadow(IntPoint(0, 0), 10, 20, ShadowStyle::Normal, true, Color(0, 0, 150, 150));
    return &debugTextShadow.get();
}

}
}

#endif
