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

#pragma once

#if ENABLE(LAYOUT_FORMATTING_CONTEXT)

#include "DisplayLineBox.h"
#include "DisplayRun.h"
#include "InlineItem.h"
#include "InlineTextItem.h"

namespace WebCore {
namespace Layout {

class InlineFormattingContext;

class LineBuilder {
    class InlineItemRun;
    struct ContinousContent;

public:
    struct Constraints {
        InlineLayoutPoint logicalTopLeft;
        InlineLayoutUnit availableLogicalWidth { 0 };
        bool lineIsConstrainedByFloat { false };
        struct HeightAndBaseline {
            InlineLayoutUnit height { 0 };
            InlineLayoutUnit baselineOffset { 0 };
            Optional<Display::LineBox::Baseline> strut;
        };
        Optional<HeightAndBaseline> heightAndBaseline;
    };

    enum class SkipAlignment { No, Yes };
    LineBuilder(const InlineFormattingContext&, Optional<TextAlignMode>, SkipAlignment);
    ~LineBuilder();

    void initialize(const Constraints&);
    void append(const InlineItem&, InlineLayoutUnit logicalWidth);
    bool isVisuallyEmpty() const { return m_lineBox.isConsideredEmpty(); }
    bool hasIntrusiveFloat() const { return m_hasIntrusiveFloat; }
    InlineLayoutUnit availableWidth() const { return logicalWidth() - contentLogicalWidth(); }

    InlineLayoutUnit trailingTrimmableWidth() const { return m_trimmableContent.width(); }
    bool isTrailingRunFullyTrimmable() const { return m_trimmableContent.isTrailingRunFullyTrimmable(); }

    const Display::LineBox& lineBox() const { return m_lineBox; }
    void moveLogicalLeft(InlineLayoutUnit);
    void moveLogicalRight(InlineLayoutUnit);
    void setHasIntrusiveFloat() { m_hasIntrusiveFloat = true; }

    struct Run {
        Run(const InlineItemRun&);
        Run(const InlineItemRun&, const Display::InlineRect&, const Display::Run::TextContext&, unsigned expansionOpportunityCount);
        Run(Run&&) = default;
        Run& operator=(Run&& other) = default;

        bool isText() const { return m_type == InlineItem::Type::Text; }
        bool isBox() const { return m_type == InlineItem::Type::Box; }
        bool isLineBreak() const { return m_type == InlineItem::Type::HardLineBreak || m_type == InlineItem::Type::SoftLineBreak; }
        bool isContainerStart() const { return m_type == InlineItem::Type::ContainerStart; }
        bool isContainerEnd() const { return m_type == InlineItem::Type::ContainerEnd; }

        const Box& layoutBox() const { return *m_layoutBox; }
        const RenderStyle& style() const { return m_layoutBox->style(); }
        const Display::InlineRect& logicalRect() const { return m_logicalRect; }
        const Optional<Display::Run::TextContext>& textContext() const { return m_textContext; }
        bool isCollapsedToVisuallyEmpty() const { return m_isCollapsedToVisuallyEmpty; }

    private:
        friend class LineBuilder;

        void adjustLogicalTop(InlineLayoutUnit logicalTop) { m_logicalRect.setTop(logicalTop); }
        void moveHorizontally(InlineLayoutUnit offset) { m_logicalRect.moveHorizontally(offset); }
        void moveVertically(InlineLayoutUnit offset) { m_logicalRect.moveVertically(offset); }
        void setLogicalHeight(InlineLayoutUnit logicalHeight) { m_logicalRect.setHeight(logicalHeight); }

        bool hasExpansionOpportunity() const { return m_expansionOpportunityCount; }
        Optional<ExpansionBehavior> expansionBehavior() const;
        unsigned expansionOpportunityCount() const { return m_expansionOpportunityCount; }
        void setComputedHorizontalExpansion(InlineLayoutUnit logicalExpansion);
        void adjustExpansionBehavior(ExpansionBehavior);

        const Box* m_layoutBox { nullptr };
        InlineItem::Type m_type;
        Display::InlineRect m_logicalRect;
        Optional<Display::Run::TextContext> m_textContext;
        unsigned m_expansionOpportunityCount { 0 };
        bool m_isCollapsedToVisuallyEmpty { false };
    };
    using RunList = Vector<Run, 50>;
    enum class IsLastLineWithInlineContent { No, Yes };
    RunList close(IsLastLineWithInlineContent = IsLastLineWithInlineContent::No);

    static Display::LineBox::Baseline halfLeadingMetrics(const FontMetrics&, InlineLayoutUnit lineLogicalHeight);

private:
    InlineLayoutUnit logicalTop() const { return m_lineBox.logicalTop(); }
    InlineLayoutUnit logicalBottom() const { return m_lineBox.logicalBottom(); }

    InlineLayoutUnit logicalLeft() const { return m_lineBox.logicalLeft(); }
    InlineLayoutUnit logicalRight() const { return logicalLeft() + logicalWidth(); }

    InlineLayoutUnit logicalWidth() const { return m_lineLogicalWidth; }
    InlineLayoutUnit logicalHeight() const { return m_lineBox.logicalHeight(); }

    InlineLayoutUnit contentLogicalWidth() const { return m_lineBox.logicalWidth(); }
    InlineLayoutUnit contentLogicalRight() const { return m_lineBox.logicalRight(); }
    InlineLayoutUnit baselineOffset() const { return m_lineBox.baselineOffset(); }

    void appendNonBreakableSpace(const InlineItem&, InlineLayoutUnit logicalLeft, InlineLayoutUnit logicalWidth);
    void appendTextContent(const InlineTextItem&, InlineLayoutUnit logicalWidth);
    void appendNonReplacedInlineBox(const InlineItem&, InlineLayoutUnit logicalWidth);
    void appendReplacedInlineBox(const InlineItem&, InlineLayoutUnit logicalWidth);
    void appendInlineContainerStart(const InlineItem&, InlineLayoutUnit logicalWidth);
    void appendInlineContainerEnd(const InlineItem&, InlineLayoutUnit logicalWidth);
    void appendLineBreak(const InlineItem&);

    void removeTrailingTrimmableContent();
    void alignContentHorizontally(RunList&, IsLastLineWithInlineContent) const;
    void alignContentVertically(RunList&);

    void adjustBaselineAndLineHeight(const Run&);
    InlineLayoutUnit runContentHeight(const Run&) const;

    bool isTextAlignJustify() const { return m_horizontalAlignment == TextAlignMode::Justify; };
    void justifyRuns(RunList&) const;

    bool isVisuallyNonEmpty(const InlineItemRun&) const;

    LayoutState& layoutState() const;
    const InlineFormattingContext& formattingContext() const;

    class InlineItemRun {
    public:
        InlineItemRun(const InlineItem&, InlineLayoutUnit logicalLeft, InlineLayoutUnit logicalWidth, WTF::Optional<Display::Run::TextContext> = WTF::nullopt);

        const Box& layoutBox() const { return m_inlineItem.layoutBox(); }
        const RenderStyle& style() const { return layoutBox().style(); }
        InlineLayoutUnit logicalLeft() const { return m_logicalLeft; }
        InlineLayoutUnit logicalWidth() const { return m_logicalWidth; }
        const Optional<Display::Run::TextContext>& textContext() const { return m_textContext; }

        bool isText() const { return m_inlineItem.isText(); }
        bool isBox() const { return m_inlineItem.isBox(); }
        bool isContainerStart() const { return m_inlineItem.isContainerStart(); }
        bool isContainerEnd() const { return m_inlineItem.isContainerEnd(); }
        bool isLineBreak() const { return m_inlineItem.isLineBreak(); }
        InlineItem::Type type() const { return m_inlineItem.type(); }

        void setIsCollapsed() { m_isCollapsed = true; }
        bool isCollapsed() const { return m_isCollapsed; }

        void moveHorizontally(InlineLayoutUnit offset) { m_logicalLeft += offset; }

        bool isTrimmableWhitespace() const;
        bool hasTrailingLetterSpacing() const;

        InlineLayoutUnit trailingLetterSpacing() const;
        void removeTrailingLetterSpacing();

        void setCollapsesToZeroAdvanceWidth();
        bool isCollapsedToZeroAdvanceWidth() const { return m_collapsedToZeroAdvanceWidth; }

        bool isCollapsible() const { return is<InlineTextItem>(m_inlineItem) && downcast<InlineTextItem>(m_inlineItem).isCollapsible(); }
        bool isWhitespace() const { return is<InlineTextItem>(m_inlineItem) && downcast<InlineTextItem>(m_inlineItem).isWhitespace(); }

        bool hasExpansionOpportunity() const { return isWhitespace() && !isCollapsedToZeroAdvanceWidth(); }

    private:
        const InlineItem& m_inlineItem;
        InlineLayoutUnit m_logicalLeft { 0 };
        InlineLayoutUnit m_logicalWidth { 0 };
        const Optional<Display::Run::TextContext> m_textContext;
        bool m_isCollapsed { false };
        bool m_collapsedToZeroAdvanceWidth { false };
    };

    using InlineItemRunList = Vector<InlineItemRun, 50>;

    struct TrimmableContent {
        TrimmableContent(InlineItemRunList&);

        void append(size_t runIndex);
        InlineLayoutUnit trim();
        InlineLayoutUnit trimTrailingRun();
        void reset();

        InlineLayoutUnit width() const { return m_width; }
        Optional<size_t> firstRunIndex() { return m_firstRunIndex; }
        bool isEmpty() const { return !m_firstRunIndex.hasValue(); }
        bool isTrailingRunFullyTrimmable() const { return m_lastRunIsFullyTrimmable; }
        bool isTrailingRunPartiallyTrimmable() const { return !isEmpty() && !isTrailingRunFullyTrimmable(); }

    private:
        InlineItemRunList& m_inlineitemRunList;
        Optional<size_t> m_firstRunIndex;
        InlineLayoutUnit m_width { 0 };
        bool m_lastRunIsFullyTrimmable { false };
    };

    const InlineFormattingContext& m_inlineFormattingContext;
    InlineItemRunList m_inlineItemRuns;
    TrimmableContent m_trimmableContent;
    Optional<Display::LineBox::Baseline> m_initialStrut;
    InlineLayoutUnit m_lineLogicalWidth { 0 };
    Optional<TextAlignMode> m_horizontalAlignment;
    bool m_skipAlignment { false };
    bool m_hasIntrusiveFloat { false };
    Display::LineBox m_lineBox;
    Optional<bool> m_lineIsVisuallyEmptyBeforeTrimmableContent;
};

inline void LineBuilder::TrimmableContent::reset()
{
    m_firstRunIndex = { };
    m_width = 0_lu;
    m_lastRunIsFullyTrimmable = false;
}

}
}
#endif
