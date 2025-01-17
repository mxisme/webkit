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
#include "InlineLineBuilder.h"

#if ENABLE(LAYOUT_FORMATTING_CONTEXT)

#include "InlineFormattingContext.h"
#include "InlineSoftLineBreakItem.h"
#include "TextUtil.h"
#include <wtf/IsoMallocInlines.h>

namespace WebCore {
namespace Layout {

struct LineBuilder::ContinousContent {
public:
    ContinousContent(const InlineItemRun&, bool textIsAlignJustify);

    bool append(const InlineItemRun&);
    LineBuilder::Run close();

private:
    static bool canBeExpanded(const InlineItemRun& run) { return run.isText() && !run.isCollapsed() && !run.isCollapsedToZeroAdvanceWidth(); }
    bool canBeMerged(const InlineItemRun& run) const { return run.isText() && !run.isCollapsedToZeroAdvanceWidth() && &m_initialInlineRun.layoutBox() == &run.layoutBox(); }

    const InlineItemRun& m_initialInlineRun;
    const bool m_textIsAlignJustify { false };
    unsigned m_expandedLength { 0 };
    InlineLayoutUnit m_expandedWidth { 0 };
    bool m_trailingRunCanBeExpanded { false };
    bool m_hasTrailingExpansionOpportunity { false };
    unsigned m_expansionOpportunityCount { 0 };
};

LineBuilder::ContinousContent::ContinousContent(const InlineItemRun& initialInlineRun, bool textIsAlignJustify)
    : m_initialInlineRun(initialInlineRun)
    , m_textIsAlignJustify(textIsAlignJustify)
    , m_trailingRunCanBeExpanded(canBeExpanded(initialInlineRun))
{
}

bool LineBuilder::ContinousContent::append(const InlineItemRun& inlineItemRun)
{
    // Merged content needs to be continuous.
    if (!m_trailingRunCanBeExpanded)
        return false;
    if (!canBeMerged(inlineItemRun))
        return false;

    m_trailingRunCanBeExpanded = canBeExpanded(inlineItemRun);

    ASSERT(inlineItemRun.isText());
    m_expandedLength += inlineItemRun.textContext()->length();
    m_expandedWidth += inlineItemRun.logicalWidth();

    if (m_textIsAlignJustify) {
        m_hasTrailingExpansionOpportunity = inlineItemRun.hasExpansionOpportunity();
        if (m_hasTrailingExpansionOpportunity)
            ++m_expansionOpportunityCount;
    }
    return true;
}

LineBuilder::Run LineBuilder::ContinousContent::close()
{
    if (!m_expandedLength)
        return { m_initialInlineRun };
    // Expand the text content and set the expansion opportunities.
    ASSERT(m_initialInlineRun.isText());
    auto textContext = *m_initialInlineRun.textContext();
    auto length = textContext.length() + m_expandedLength;
    textContext.expand(length);

    if (m_textIsAlignJustify) {
        // FIXME: This is a very simple expansion merge. We should eventually switch over to FontCascade::expansionOpportunityCount.
        ExpansionBehavior expansionBehavior = m_hasTrailingExpansionOpportunity ? (ForbidLeadingExpansion | AllowTrailingExpansion) : (AllowLeadingExpansion | AllowTrailingExpansion);
        if (m_initialInlineRun.hasExpansionOpportunity())
            ++m_expansionOpportunityCount;
        textContext.setExpansion({ expansionBehavior, { } });
    }
    return { m_initialInlineRun,  Display::InlineRect { 0_lu, m_initialInlineRun.logicalLeft(), m_initialInlineRun.logicalWidth() + m_expandedWidth, 0_lu }, textContext, m_expansionOpportunityCount };
}

LineBuilder::Run::Run(const InlineItemRun& inlineItemRun)
    : m_layoutBox(&inlineItemRun.layoutBox())
    , m_type(inlineItemRun.type())
    , m_logicalRect({ 0_lu, inlineItemRun.logicalLeft(), inlineItemRun.logicalWidth(), 0_lu })
    , m_textContext(inlineItemRun.textContext())
    , m_isCollapsedToVisuallyEmpty(inlineItemRun.isCollapsedToZeroAdvanceWidth())
{
    if (inlineItemRun.hasExpansionOpportunity()) {
        m_expansionOpportunityCount = 1;
        ASSERT(m_textContext);
        m_textContext->setExpansion({ DefaultExpansion, { } });
    }
}

LineBuilder::Run::Run(const InlineItemRun& inlineItemRun, const Display::InlineRect& logicalRect, const Display::Run::TextContext& textContext, unsigned expansionOpportunityCount)
    : m_layoutBox(&inlineItemRun.layoutBox())
    , m_type(inlineItemRun.type())
    , m_logicalRect(logicalRect)
    , m_textContext(textContext)
    , m_expansionOpportunityCount(expansionOpportunityCount)
    , m_isCollapsedToVisuallyEmpty(inlineItemRun.isCollapsedToZeroAdvanceWidth())
{
}

void LineBuilder::Run::adjustExpansionBehavior(ExpansionBehavior expansionBehavior)
{
    ASSERT(isText());
    ASSERT(hasExpansionOpportunity());
    m_textContext->setExpansion({ expansionBehavior, m_textContext->expansion()->horizontalExpansion });
}

inline Optional<ExpansionBehavior> LineBuilder::Run::expansionBehavior() const
{
    ASSERT(isText());
    if (auto expansionContext = m_textContext->expansion())
        return expansionContext->behavior;
    return { };
}

void LineBuilder::Run::setComputedHorizontalExpansion(InlineLayoutUnit logicalExpansion)
{
    ASSERT(isText());
    ASSERT(hasExpansionOpportunity());
    m_logicalRect.expandHorizontally(logicalExpansion);
    m_textContext->setExpansion({ m_textContext->expansion()->behavior, logicalExpansion });
}

LineBuilder::LineBuilder(const InlineFormattingContext& inlineFormattingContext, Optional<TextAlignMode> horizontalAlignment, SkipAlignment skipAlignment)
    : m_inlineFormattingContext(inlineFormattingContext)
    , m_trimmableContent(m_inlineItemRuns)
    , m_horizontalAlignment(horizontalAlignment)
    , m_skipAlignment(skipAlignment == SkipAlignment::Yes)
{
}

LineBuilder::~LineBuilder()
{
}

void LineBuilder::initialize(const Constraints& constraints)
{
    ASSERT(m_skipAlignment || constraints.heightAndBaseline);

    InlineLayoutUnit initialLineHeight = 0;
    InlineLayoutUnit initialBaselineOffset = 0;
    if (constraints.heightAndBaseline) {
        m_initialStrut = constraints.heightAndBaseline->strut;
        initialLineHeight = constraints.heightAndBaseline->height;
        initialBaselineOffset = constraints.heightAndBaseline->baselineOffset;
    } else
        m_initialStrut = { };

    auto lineRect = Display::InlineRect { constraints.logicalTopLeft, 0_lu, initialLineHeight };
    auto baseline = Display::LineBox::Baseline { initialBaselineOffset, initialLineHeight - initialBaselineOffset };
    m_lineBox = Display::LineBox { lineRect, baseline, initialBaselineOffset };
    m_lineLogicalWidth = constraints.availableLogicalWidth;
    m_hasIntrusiveFloat = constraints.lineIsConstrainedByFloat;

    m_inlineItemRuns.clear();
    m_trimmableContent.reset();
    m_lineIsVisuallyEmptyBeforeTrimmableContent = { };
}

static bool shouldPreserveLeadingContent(const InlineTextItem& inlineTextItem)
{
    if (!inlineTextItem.isWhitespace())
        return true;
    auto whitespace = inlineTextItem.style().whiteSpace();
    return whitespace == WhiteSpace::Pre || whitespace == WhiteSpace::PreWrap || whitespace == WhiteSpace::BreakSpaces;
}

LineBuilder::RunList LineBuilder::close(IsLastLineWithInlineContent isLastLineWithInlineContent)
{
    // 1. Remove trimmable trailing content.
    // 2. Join text runs together when possible [foo][ ][bar] -> [foo bar].
    // 3. Align merged runs both vertically and horizontally.
    removeTrailingTrimmableContent();
    RunList runList;
    unsigned runIndex = 0;
    while (runIndex < m_inlineItemRuns.size()) {
        // Merge eligible runs.
        auto continousContent = ContinousContent { m_inlineItemRuns[runIndex], isTextAlignJustify() };
        while (++runIndex < m_inlineItemRuns.size()) {
            if (!continousContent.append(m_inlineItemRuns[runIndex]))
                break;
        }
        runList.append(continousContent.close());
    }

    if (!m_skipAlignment) {
        for (auto& run : runList) {
            adjustBaselineAndLineHeight(run);
            run.setLogicalHeight(runContentHeight(run));
        }
        if (isVisuallyEmpty()) {
            m_lineBox.resetBaseline();
            m_lineBox.setLogicalHeight(0_lu);
        }
        // Remove descent when all content is baseline aligned but none of them have descent.
        if (formattingContext().quirks().lineDescentNeedsCollapsing(runList)) {
            m_lineBox.shrinkVertically(m_lineBox.baseline().descent());
            m_lineBox.resetDescent();
        }
        alignContentVertically(runList);
        alignContentHorizontally(runList, isLastLineWithInlineContent);
    }
    return runList;
}

void LineBuilder::alignContentVertically(RunList& runList)
{
    ASSERT(!m_skipAlignment);
    for (auto& run : runList) {
        InlineLayoutUnit logicalTop = 0;
        auto& layoutBox = run.layoutBox();
        auto verticalAlign = layoutBox.style().verticalAlign();
        auto ascent = layoutBox.style().fontMetrics().ascent();

        switch (verticalAlign) {
        case VerticalAlign::Baseline:
            if (run.isLineBreak() || run.isText())
                logicalTop = baselineOffset() - ascent;
            else if (run.isContainerStart()) {
                auto& boxGeometry = formattingContext().geometryForBox(layoutBox);
                logicalTop = baselineOffset() - ascent - boxGeometry.borderTop() - boxGeometry.paddingTop().valueOr(0);
            } else if (layoutBox.isInlineBlockBox() && layoutBox.establishesInlineFormattingContext()) {
                auto& formattingState = downcast<InlineFormattingState>(layoutState().establishedFormattingState(downcast<Container>(layoutBox)));
                // Spec makes us generate at least one line -even if it is empty.
                auto inlineBlockBaselineOffset = formattingState.displayInlineContent()->lineBoxes.last().baselineOffset();
                // The inline-block's baseline offset is relative to its content box. Let's convert it relative to the margin box.
                //           _______________ <- margin box
                //          |
                //          |  ____________  <- border box
                //          | |
                //          | |  _________  <- content box
                //          | | |   ^
                //          | | |   |  <- baseline offset
                //          | | |   |
                //     text | | |   v text
                //     -----|-|-|---------- <- baseline
                //
                auto& boxGeometry = formattingContext().geometryForBox(layoutBox);
                auto baselineOffsetFromMarginBox = boxGeometry.marginBefore() + boxGeometry.borderTop() + boxGeometry.paddingTop().valueOr(0) + inlineBlockBaselineOffset;
                logicalTop = baselineOffset() - baselineOffsetFromMarginBox;
            } else
                logicalTop = baselineOffset() - run.logicalRect().height();
            break;
        case VerticalAlign::Top:
            logicalTop = 0_lu;
            break;
        case VerticalAlign::Bottom:
            logicalTop = logicalBottom() - run.logicalRect().height();
            break;
        default:
            ASSERT_NOT_IMPLEMENTED_YET();
            break;
        }
        run.adjustLogicalTop(logicalTop);
        // Convert runs from relative to the line top/left to the formatting root's border box top/left.
        run.moveVertically(this->logicalTop());
        run.moveHorizontally(this->logicalLeft());
    }
}

void LineBuilder::justifyRuns(RunList& runList) const
{
    ASSERT(!runList.isEmpty());
    ASSERT(availableWidth() > 0);
    // Need to fix up the last run first.
    auto& lastRun = runList.last();
    if (lastRun.hasExpansionOpportunity())
        lastRun.adjustExpansionBehavior(*lastRun.expansionBehavior() | ForbidTrailingExpansion);
    // Collect the expansion opportunity numbers.
    auto expansionOpportunityCount = 0;
    for (auto& run : runList)
        expansionOpportunityCount += run.expansionOpportunityCount();
    // Nothing to distribute?
    if (!expansionOpportunityCount)
        return;
    // Distribute the extra space.
    auto expansionToDistribute = availableWidth() / expansionOpportunityCount;
    InlineLayoutUnit accumulatedExpansion = 0;
    for (auto& run : runList) {
        // Expand and moves runs by the accumulated expansion.
        if (!run.hasExpansionOpportunity()) {
            run.moveHorizontally(accumulatedExpansion);
            continue;
        }
        ASSERT(run.expansionOpportunityCount());
        auto computedExpansion = expansionToDistribute * run.expansionOpportunityCount();
        run.setComputedHorizontalExpansion(computedExpansion);
        run.moveHorizontally(accumulatedExpansion);
        accumulatedExpansion += computedExpansion;
    }
}

void LineBuilder::alignContentHorizontally(RunList& runList, IsLastLineWithInlineContent lastLine) const
{
    ASSERT(!m_skipAlignment);
    if (runList.isEmpty() || availableWidth() <= 0)
        return;

    if (isTextAlignJustify()) {
        // Do not justify align the last line.
        if (lastLine == IsLastLineWithInlineContent::No)
            justifyRuns(runList);
        return;
    }

    auto adjustmentForAlignment = [&]() -> Optional<InlineLayoutUnit> {
        switch (*m_horizontalAlignment) {
        case TextAlignMode::Left:
        case TextAlignMode::WebKitLeft:
        case TextAlignMode::Start:
            return { };
        case TextAlignMode::Right:
        case TextAlignMode::WebKitRight:
        case TextAlignMode::End:
            return std::max<InlineLayoutUnit>(availableWidth(), 0);
        case TextAlignMode::Center:
        case TextAlignMode::WebKitCenter:
            return std::max<InlineLayoutUnit>(availableWidth() / 2, 0);
        case TextAlignMode::Justify:
            ASSERT_NOT_REACHED();
            break;
        }
        ASSERT_NOT_REACHED();
        return { };
    };

    auto adjustment = adjustmentForAlignment();
    if (!adjustment)
        return;

    for (auto& run : runList)
        run.moveHorizontally(*adjustment);
}

void LineBuilder::removeTrailingTrimmableContent()
{
    if (m_trimmableContent.isEmpty() || m_inlineItemRuns.isEmpty())
        return;

    m_lineBox.shrinkHorizontally(m_trimmableContent.trim());
    // If we trimmed the first visible run on the line, we need to re-check the visibility status.
    if (!m_lineIsVisuallyEmptyBeforeTrimmableContent)
        return;
    // Just because the line was visually empty before the trimmed content, it does not necessarily mean it is still visually empty.
    // <span>  </span><span style="padding-left: 10px"></span>  <- non-empty
    auto lineIsVisuallyEmpty = [&] {
        for (auto& run : m_inlineItemRuns) {
            if (isVisuallyNonEmpty(run))
                return false;
        }
        return true;
    };
    // We could only go from visually non empty -> to visually empty. Trimming runs should never make the line visible.
    if (lineIsVisuallyEmpty())
        m_lineBox.setIsConsideredEmpty();
    m_lineIsVisuallyEmptyBeforeTrimmableContent = { };
}

void LineBuilder::moveLogicalLeft(InlineLayoutUnit delta)
{
    if (!delta)
        return;
    ASSERT(delta > 0);
    m_lineBox.moveHorizontally(delta);
    m_lineLogicalWidth -= delta;
}

void LineBuilder::moveLogicalRight(InlineLayoutUnit delta)
{
    ASSERT(delta > 0);
    m_lineLogicalWidth -= delta;
}

void LineBuilder::append(const InlineItem& inlineItem, InlineLayoutUnit logicalWidth)
{
    if (inlineItem.isText())
        appendTextContent(downcast<InlineTextItem>(inlineItem), logicalWidth);
    else if (inlineItem.isLineBreak())
        appendLineBreak(inlineItem);
    else if (inlineItem.isContainerStart())
        appendInlineContainerStart(inlineItem, logicalWidth);
    else if (inlineItem.isContainerEnd())
        appendInlineContainerEnd(inlineItem, logicalWidth);
    else if (inlineItem.layoutBox().replaced())
        appendReplacedInlineBox(inlineItem, logicalWidth);
    else if (inlineItem.isBox())
        appendNonReplacedInlineBox(inlineItem, logicalWidth);
    else
        ASSERT_NOT_REACHED();

    // Check if this freshly appended content makes the line visually non-empty.
    ASSERT(!m_inlineItemRuns.isEmpty());
    if (m_lineBox.isConsideredEmpty() && isVisuallyNonEmpty(m_inlineItemRuns.last()))
        m_lineBox.setIsConsideredNonEmpty();
}

void LineBuilder::appendNonBreakableSpace(const InlineItem& inlineItem, InlineLayoutUnit logicalLeft, InlineLayoutUnit logicalWidth)
{
    m_inlineItemRuns.append({ inlineItem, logicalLeft, logicalWidth });
    m_lineBox.expandHorizontally(logicalWidth);
}

void LineBuilder::appendInlineContainerStart(const InlineItem& inlineItem, InlineLayoutUnit logicalWidth)
{
    // This is really just a placeholder to mark the start of the inline level container <span>.
    appendNonBreakableSpace(inlineItem, contentLogicalWidth(), logicalWidth);
}

void LineBuilder::appendInlineContainerEnd(const InlineItem& inlineItem, InlineLayoutUnit logicalWidth)
{
    // This is really just a placeholder to mark the end of the inline level container </span>.
    auto trimTrailingLetterSpacing = [&] {
        if (!m_trimmableContent.isTrailingRunPartiallyTrimmable())
            return;
        m_lineBox.shrinkHorizontally(m_trimmableContent.trimTrailingRun());
    };
    // Prevent trailing letter-spacing from spilling out of the inline container.
    // https://drafts.csswg.org/css-text-3/#letter-spacing-property See example 21.
    trimTrailingLetterSpacing();
    appendNonBreakableSpace(inlineItem, contentLogicalRight(), logicalWidth);
}

void LineBuilder::appendTextContent(const InlineTextItem& inlineItem, InlineLayoutUnit logicalWidth)
{
    auto willCollapseCompletely = [&] {
        // Empty run.
        if (!inlineItem.length()) {
            ASSERT(!logicalWidth);
            return true;
        }
        // Leading whitespace.
        if (m_inlineItemRuns.isEmpty())
            return !shouldPreserveLeadingContent(inlineItem);

        if (!inlineItem.isCollapsible())
            return false;
        // Check if the last item is collapsed as well.
        for (auto i = m_inlineItemRuns.size(); i--;) {
            auto& run = m_inlineItemRuns[i];
            if (run.isBox())
                return false;
            // https://drafts.csswg.org/css-text-3/#white-space-phase-1
            // Any collapsible space immediately following another collapsible space—even one outside the boundary of the inline containing that space,
            // provided both spaces are within the same inline formatting context—is collapsed to have zero advance width.
            // : "<span>  </span> " <- the trailing whitespace collapses completely.
            // Not that when the inline container has preserve whitespace style, "<span style="white-space: pre">  </span> " <- this whitespace stays around.
            if (run.isText())
                return run.isCollapsible();
            ASSERT(run.isContainerStart() || run.isContainerEnd());
        }
        return true;
    };

    auto collapsesToZeroAdvanceWidth = willCollapseCompletely();
    auto collapsedRun = inlineItem.isCollapsible() && inlineItem.length() > 1;
    auto contentStart = inlineItem.start();
    auto contentLength =  collapsedRun ? 1 : inlineItem.length();
    m_inlineItemRuns.append({ inlineItem, contentLogicalWidth(), logicalWidth, Display::Run::TextContext { contentStart, contentLength, inlineItem.layoutBox().textContext()->content } });
    auto& lineRun = m_inlineItemRuns.last();

    if (collapsesToZeroAdvanceWidth)
        lineRun.setCollapsesToZeroAdvanceWidth();

    if (collapsedRun)
        lineRun.setIsCollapsed();

    m_lineBox.expandHorizontally(lineRun.logicalWidth());

    // Existing trailing trimmable content can only be expanded if the current rus is fully trimmable.
    auto trimmableListNeedsReset = !m_trimmableContent.isEmpty() && !lineRun.isTrimmableWhitespace();
    if (trimmableListNeedsReset)
        m_trimmableContent.reset();
    auto isTrimmable = lineRun.isTrimmableWhitespace() || lineRun.hasTrailingLetterSpacing();
    if (isTrimmable) {
        // If we ever trim this content, we need to know if the line visibility state needs to be recomputed.
        if (m_trimmableContent.isEmpty())
            m_lineIsVisuallyEmptyBeforeTrimmableContent = isVisuallyEmpty();
        m_trimmableContent.append(m_inlineItemRuns.size() - 1);
    }
}

void LineBuilder::appendNonReplacedInlineBox(const InlineItem& inlineItem, InlineLayoutUnit logicalWidth)
{
    auto& layoutBox = inlineItem.layoutBox();
    auto& boxGeometry = formattingContext().geometryForBox(layoutBox);
    auto horizontalMargin = boxGeometry.horizontalMargin();
    m_inlineItemRuns.append({ inlineItem, contentLogicalWidth() + horizontalMargin.start, logicalWidth });
    m_lineBox.expandHorizontally(logicalWidth + horizontalMargin.start + horizontalMargin.end);
    m_trimmableContent.reset();
}

void LineBuilder::appendReplacedInlineBox(const InlineItem& inlineItem, InlineLayoutUnit logicalWidth)
{
    ASSERT(inlineItem.layoutBox().isReplaced());
    // FIXME: Surely replaced boxes behave differently.
    appendNonReplacedInlineBox(inlineItem, logicalWidth);
}

void LineBuilder::appendLineBreak(const InlineItem& inlineItem)
{
    if (inlineItem.isHardLineBreak())
        return m_inlineItemRuns.append({ inlineItem, contentLogicalWidth(), 0_lu });
    // Soft line breaks (preserved new line characters) require inline text boxes for compatibility reasons.
    ASSERT(inlineItem.isSoftLineBreak());
    auto& softLineBreakItem = downcast<InlineSoftLineBreakItem>(inlineItem);
    m_inlineItemRuns.append({ softLineBreakItem, contentLogicalWidth(), 0_lu, Display::Run::TextContext { softLineBreakItem.position(), 1, softLineBreakItem.layoutBox().textContext()->content } });
}

void LineBuilder::adjustBaselineAndLineHeight(const Run& run)
{
    auto& baseline = m_lineBox.baseline();
    if (run.isText() || run.isLineBreak()) {
        // For text content we set the baseline either through the initial strut (set by the formatting context root) or
        // through the inline container (start) -see above. Normally the text content itself does not stretch the line.
        if (!m_initialStrut)
            return;
        m_lineBox.setAscentIfGreater(m_initialStrut->ascent());
        m_lineBox.setDescentIfGreater(m_initialStrut->descent());
        m_lineBox.setLogicalHeightIfGreater(baseline.height());
        m_initialStrut = { };
        return;
    }

    auto& layoutBox = run.layoutBox();
    auto& style = layoutBox.style();
    if (run.isContainerStart()) {
        // Inline containers stretch the line by their font size.
        // Vertical margins, paddings and borders don't contribute to the line height.
        auto& fontMetrics = style.fontMetrics();
        if (style.verticalAlign() == VerticalAlign::Baseline) {
            auto halfLeading = halfLeadingMetrics(fontMetrics, style.computedLineHeight());
            // Both halfleading ascent and descent could be negative (tall font vs. small line-height value)
            if (halfLeading.descent() > 0)
                m_lineBox.setDescentIfGreater(halfLeading.descent());
            if (halfLeading.ascent() > 0)
                m_lineBox.setAscentIfGreater(halfLeading.ascent());
            m_lineBox.setLogicalHeightIfGreater(baseline.height());
        } else
            m_lineBox.setLogicalHeightIfGreater(fontMetrics.height());
        return;
    }

    if (run.isContainerEnd()) {
        // The line's baseline and height have already been adjusted at ContainerStart.
        return;
    }

    if (run.isBox()) {
        auto& boxGeometry = formattingContext().geometryForBox(layoutBox);
        auto marginBoxHeight = boxGeometry.marginBoxHeight();

        switch (style.verticalAlign()) {
        case VerticalAlign::Baseline: {
            if (layoutBox.isInlineBlockBox() && layoutBox.establishesInlineFormattingContext()) {
                // Inline-blocks with inline content always have baselines.
                auto& formattingState = downcast<InlineFormattingState>(layoutState().establishedFormattingState(downcast<Container>(layoutBox)));
                // Spec makes us generate at least one line -even if it is empty.
                auto& lastLineBox = formattingState.displayInlineContent()->lineBoxes.last();
                auto inlineBlockBaseline = lastLineBox.baseline();
                auto beforeHeight = boxGeometry.marginBefore() + boxGeometry.borderTop() + boxGeometry.paddingTop().valueOr(0);

                m_lineBox.setAscentIfGreater(inlineBlockBaseline.ascent());
                m_lineBox.setDescentIfGreater(inlineBlockBaseline.descent());
                m_lineBox.setBaselineOffsetIfGreater(beforeHeight + lastLineBox.baselineOffset());
                m_lineBox.setLogicalHeightIfGreater(marginBoxHeight);
            } else {
                // Non inline-block boxes sit on the baseline (including their bottom margin).
                m_lineBox.setAscentIfGreater(marginBoxHeight);
                // Ignore negative descent (yes, negative descent is a thing).
                m_lineBox.setLogicalHeightIfGreater(marginBoxHeight + std::max<InlineLayoutUnit>(0, baseline.descent()));
            }
            break;
        }
        case VerticalAlign::Top:
            // Top align content never changes the baseline, it only pushes the bottom of the line further down.
            m_lineBox.setLogicalHeightIfGreater(marginBoxHeight);
            break;
        case VerticalAlign::Bottom: {
            // Bottom aligned, tall content pushes the baseline further down from the line top.
            auto lineLogicalHeight = m_lineBox.logicalHeight();
            if (marginBoxHeight > lineLogicalHeight) {
                m_lineBox.setLogicalHeightIfGreater(marginBoxHeight);
                m_lineBox.setBaselineOffsetIfGreater(m_lineBox.baselineOffset() + (marginBoxHeight - lineLogicalHeight));
            }
            break;
        }
        default:
            ASSERT_NOT_IMPLEMENTED_YET();
            break;
        }
        return;
    }
    ASSERT_NOT_REACHED();
}

InlineLayoutUnit LineBuilder::runContentHeight(const Run& run) const
{
    ASSERT(!m_skipAlignment);
    auto& fontMetrics = run.style().fontMetrics();
    if (run.isText() || run.isLineBreak())
        return fontMetrics.height();

    if (run.isContainerStart() || run.isContainerEnd())
        return fontMetrics.height();

    auto& layoutBox = run.layoutBox();
    auto& boxGeometry = formattingContext().geometryForBox(layoutBox);
    if (layoutBox.replaced() || layoutBox.isFloatingPositioned())
        return boxGeometry.contentBoxHeight();

    // Non-replaced inline box (e.g. inline-block). It looks a bit misleading but their margin box is considered the content height here.
    return boxGeometry.marginBoxHeight();
}

bool LineBuilder::isVisuallyNonEmpty(const InlineItemRun& run) const
{
    if (run.isText())
        return !run.isCollapsedToZeroAdvanceWidth();

    // Note that this does not check whether the inline container has content. It simply checks if the container itself is considered non-empty.
    if (run.isContainerStart() || run.isContainerEnd()) {
        if (!run.logicalWidth())
            return false;
        // Margin does not make the container visually non-empty. Check if it has border or padding.
        auto& boxGeometry = formattingContext().geometryForBox(run.layoutBox());
        if (run.isContainerStart())
            return boxGeometry.borderLeft() || (boxGeometry.paddingLeft() && boxGeometry.paddingLeft().value());
        return boxGeometry.borderRight() || (boxGeometry.paddingRight() && boxGeometry.paddingRight().value());
    }

    if (run.isLineBreak())
        return true;

    if (run.isBox()) {
        if (!run.layoutBox().establishesFormattingContext())
            return true;
        ASSERT(run.layoutBox().isInlineBlockBox());
        if (!run.logicalWidth())
            return false;
        if (m_skipAlignment || formattingContext().geometryForBox(run.layoutBox()).height())
            return true;
        return false;
    }

    ASSERT_NOT_REACHED();
    return false;
}

Display::LineBox::Baseline LineBuilder::halfLeadingMetrics(const FontMetrics& fontMetrics, InlineLayoutUnit lineLogicalHeight)
{
    auto ascent = fontMetrics.ascent();
    auto descent = fontMetrics.descent();
    // 10.8.1 Leading and half-leading
    auto halfLeading = (lineLogicalHeight - (ascent + descent)) / 2;
    // Inline tree height is all integer based.
    auto adjustedAscent = std::max<InlineLayoutUnit>(floorf(ascent + halfLeading), 0);
    auto adjustedDescent = std::max<InlineLayoutUnit>(ceilf(descent + halfLeading), 0);
    return { adjustedAscent, adjustedDescent };
}

LayoutState& LineBuilder::layoutState() const
{ 
    return formattingContext().layoutState();
}

const InlineFormattingContext& LineBuilder::formattingContext() const
{
    return m_inlineFormattingContext;
}

LineBuilder::TrimmableContent::TrimmableContent(InlineItemRunList& inlineItemRunList)
    : m_inlineitemRunList(inlineItemRunList)
{
}

void LineBuilder::TrimmableContent::append(size_t runIndex)
{
    auto& trimmableRun = m_inlineitemRunList[runIndex];
    InlineLayoutUnit trimmableWidth = 0;
    auto isFullyTrimmable = trimmableRun.isTrimmableWhitespace();
    if (isFullyTrimmable)
        trimmableWidth = trimmableRun.logicalWidth();
    else {
        ASSERT(trimmableRun.hasTrailingLetterSpacing());
        trimmableWidth = trimmableRun.trailingLetterSpacing();
    }
    // word-spacing could very well be negative, but it does not mean that the line gains that much extra space when the content is trimmed.
    trimmableWidth = std::max<InlineLayoutUnit>(0, trimmableWidth);

    ASSERT(trimmableWidth >= 0);
    m_width += trimmableWidth;
    m_lastRunIsFullyTrimmable = isFullyTrimmable;
    m_firstRunIndex = m_firstRunIndex.valueOr(runIndex);
}

InlineLayoutUnit LineBuilder::TrimmableContent::trim()
{
    ASSERT(!isEmpty());
#ifndef NDEBUG
    auto hasSeenNonWhitespaceTextContent = false;
#endif
    // Collapse trimmable trailing content and move all the other trailing runs.
    // <span> </span><span></span> ->
    // [whitespace][container end][container start][container end]
    // Trim the whitespace run and move the trailing inline container runs to the left.
    InlineLayoutUnit accumulatedTrimmedWidth = 0;
    for (auto index = *m_firstRunIndex; index < m_inlineitemRunList.size(); ++index) {
        auto& run = m_inlineitemRunList[index];
        run.moveHorizontally(-accumulatedTrimmedWidth);
        if (!run.isText()) {
            ASSERT(run.isContainerStart() || run.isContainerEnd() || run.isLineBreak());
            continue;
        }
        if (run.isWhitespace()) {
            accumulatedTrimmedWidth += run.logicalWidth();
            run.setCollapsesToZeroAdvanceWidth();
        } else {
            ASSERT(!hasSeenNonWhitespaceTextContent);
#ifndef NDEBUG
            hasSeenNonWhitespaceTextContent = true;
#endif
            // Must be a letter spacing trim.
            ASSERT(run.hasTrailingLetterSpacing());
            accumulatedTrimmedWidth += run.trailingLetterSpacing();
            run.removeTrailingLetterSpacing();
        }
    }
    ASSERT(accumulatedTrimmedWidth == width());
    reset();
    return accumulatedTrimmedWidth;
}

InlineLayoutUnit LineBuilder::TrimmableContent::trimTrailingRun()
{
    ASSERT(!isEmpty());
    // Find the last trimmable run (it is not necessarily the last run e.g [container start][whitespace][container end])
    for (auto index = m_inlineitemRunList.size(); index-- && *m_firstRunIndex >= index;) {
        auto& run = m_inlineitemRunList[index];
        if (!run.isText()) {
            ASSERT(run.isContainerStart() || run.isContainerEnd());
            continue;
        }
        InlineLayoutUnit trimmedWidth = 0;
        if (run.isWhitespace()) {
            trimmedWidth = run.logicalWidth();
            run.setCollapsesToZeroAdvanceWidth();
        } else {
            ASSERT(run.hasTrailingLetterSpacing());
            trimmedWidth = run.trailingLetterSpacing();
            run.removeTrailingLetterSpacing();
        }
        m_width -= trimmedWidth;
        // We managed to trim the last trimmable run.
        if (index == *m_firstRunIndex) {
            ASSERT(!m_width);
            m_firstRunIndex = { };
        }
        return trimmedWidth;
    }
    ASSERT_NOT_REACHED();
    return 0_lu;
}

LineBuilder::InlineItemRun::InlineItemRun(const InlineItem& inlineItem, InlineLayoutUnit logicalLeft, InlineLayoutUnit logicalWidth, WTF::Optional<Display::Run::TextContext> textContext)
    : m_inlineItem(inlineItem)
    , m_logicalLeft(logicalLeft)
    , m_logicalWidth(logicalWidth)
    , m_textContext(textContext)
{
}

bool LineBuilder::InlineItemRun::isTrimmableWhitespace() const
{
    if (!isWhitespace())
        return false;
    return !TextUtil::shouldPreserveTrailingWhitespace(style());
}

bool LineBuilder::InlineItemRun::hasTrailingLetterSpacing() const
{
    return !isWhitespace() && style().letterSpacing() > 0;
}

InlineLayoutUnit LineBuilder::InlineItemRun::trailingLetterSpacing() const
{
    if (!hasTrailingLetterSpacing())
        return 0_lu;
    return InlineLayoutUnit { style().letterSpacing() };
}

void LineBuilder::InlineItemRun::setCollapsesToZeroAdvanceWidth()
{
    m_collapsedToZeroAdvanceWidth = true;
    m_logicalWidth = 0_lu;
}

void LineBuilder::InlineItemRun::removeTrailingLetterSpacing()
{
    ASSERT(hasTrailingLetterSpacing());
    m_logicalWidth -= trailingLetterSpacing();
    ASSERT(m_logicalWidth > 0 || (!m_logicalWidth && style().letterSpacing() >= intMaxForLayoutUnit));
}

}
}

#endif
