/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
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
#include "InlineLineBreaker.h"

#if ENABLE(LAYOUT_FORMATTING_CONTEXT)

#include "FontCascade.h"
#include "Hyphenation.h"
#include "InlineItem.h"
#include "InlineTextItem.h"
#include "TextUtil.h"

namespace WebCore {
namespace Layout {

static inline bool isContentWrappingAllowed(const LineBreaker::Content::Run& run)
{
    if (!run.inlineItem.isText()) {
        // Can't split horizontal spacing -> e.g. <span style="padding-right: 100px;">textcontent</span>, if the [container end] is the overflown inline item
        // we need to check if there's another inline item beyond the [container end] to split.
        return false;
    }
    auto& style = run.inlineItem.style();
    // Do not try to split 'pre' and 'no-wrap' content.
    return style.whiteSpace() != WhiteSpace::Pre && style.whiteSpace() != WhiteSpace::NoWrap;
}

static inline bool isTrailingWhitespaceWithPreWrap(const InlineItem& trailingInlineItem)
{
    if (!trailingInlineItem.isText())
        return false;
    return trailingInlineItem.style().whiteSpace() == WhiteSpace::PreWrap && downcast<InlineTextItem>(trailingInlineItem).isWhitespace();
}

LineBreaker::BreakingContext LineBreaker::breakingContextForInlineContent(const Content& candidateRuns, const LineStatus& lineStatus)
{
    ASSERT(!candidateRuns.isEmpty());
    if (candidateRuns.width() <= lineStatus.availableWidth)
        return { BreakingContext::ContentWrappingRule::Keep, { } };
    if (candidateRuns.hasTrailingTrimmableContent()) {
        // First check if the content fits without the trailing trimmable part.
        if (candidateRuns.nonTrimmableWidth() <= lineStatus.availableWidth)
            return { BreakingContext::ContentWrappingRule::Keep, { } };
        // Now check if we can trim the line too.
        if (lineStatus.lineHasFullyTrimmableTrailingRun && candidateRuns.isTrailingContentFullyTrimmable()) {
            // If this new content is fully trimmable, it shoud surely fit.
            return { BreakingContext::ContentWrappingRule::Keep, { } };
        }
    } else if (lineStatus.trimmableWidth && candidateRuns.hasNonContentRunsOnly()) {
        // Let's see if the non-content runs fit when the line has trailing trimmable content
        // "text content <span style="padding: 1px"></span>" <- the <span></span> runs could fit after trimming the trailing whitespace.
        if (candidateRuns.width() <= lineStatus.availableWidth + lineStatus.trimmableWidth)
            return { BreakingContext::ContentWrappingRule::Keep, { } };
    }

    if (candidateRuns.hasTextContentOnly()) {
        auto& runs = candidateRuns.runs();
        if (auto partialTrailingContent = wordBreakingBehavior(runs, lineStatus.availableWidth))
            return { BreakingContext::ContentWrappingRule::Split, partialTrailingContent };
        // If we did not manage to break this content, we still need to decide whether keep it or push it to the next line.
        auto contentShouldOverflow = lineStatus.lineIsEmpty || !isContentWrappingAllowed(runs[0]);
        // FIXME: white-space: pre-wrap needs clarification. According to CSS Text Module Level 3, content wrapping is as 'normal' but apparently
        // we need to keep the overlapping whitespace on the line (and hang it I'd assume).
        if (isTrailingWhitespaceWithPreWrap(runs.last().inlineItem))
            contentShouldOverflow = true;
        return { contentShouldOverflow ? BreakingContext::ContentWrappingRule::Keep : BreakingContext::ContentWrappingRule::Push, { } };
    }
    // First non-text inline content always stays on line.
    return { lineStatus.lineIsEmpty ? BreakingContext::ContentWrappingRule::Keep : BreakingContext::ContentWrappingRule::Push, { } };
}

bool LineBreaker::shouldWrapFloatBox(InlineLayoutUnit floatLogicalWidth, InlineLayoutUnit availableWidth, bool lineIsEmpty)
{
    return !lineIsEmpty && floatLogicalWidth > availableWidth;
}

Optional<LineBreaker::BreakingContext::PartialTrailingContent> LineBreaker::wordBreakingBehavior(const Content::RunList& runs, InlineLayoutUnit availableWidth) const
{
    // Check where the overflow occurs and use the corresponding style to figure out the breaking behaviour.
    // <span style="word-break: normal">first</span><span style="word-break: break-all">second</span><span style="word-break: normal">third</span>
    InlineLayoutUnit accumulatedRunWidth = 0;
    unsigned index = 0;
    while (index < runs.size()) {
        auto& run = runs[index];
        ASSERT(run.inlineItem.isText() || run.inlineItem.isContainerStart() || run.inlineItem.isContainerEnd());
        if (accumulatedRunWidth + run.logicalWidth > availableWidth && isContentWrappingAllowed(run)) {
            // At this point the available width can very well be negative e.g. when some part of the continuous text content can not be broken into parts ->
            // <span style="word-break: keep-all">textcontentwithnobreak</span><span>textcontentwithyesbreak</span>
            // When the first span computes longer than the available space, by the time we get to the second span, the adjusted available space becomes negative.
            auto adjustedAvailableWidth = std::max<InlineLayoutUnit>(0, availableWidth - accumulatedRunWidth);
            if (auto leftSide = tryBreakingTextRun(run, adjustedAvailableWidth))
                return BreakingContext::PartialTrailingContent { index, leftSide->length, leftSide->logicalWidth, leftSide->needsHyphen };
            // If this run is not breakable, we need to check if any previous run is breakable
            break;
        }
        accumulatedRunWidth += run.logicalWidth;
        ++index;
    }
    // We did not manage to break the run that actually overflows the line.
    // Let's try to find the first breakable run and wrap it at the content boundary (as it surely fits).
    while (index--) {
        auto& run = runs[index];
        if (isContentWrappingAllowed(run)) {
            ASSERT(run.inlineItem.isText());
            return BreakingContext::PartialTrailingContent { index, downcast<InlineTextItem>(run.inlineItem).length(), run.logicalWidth, false };
        }
    }
    // Give up, there's no breakable run in here.
    return { };
}

Optional<LineBreaker::LeftSide> LineBreaker::tryBreakingTextRun(const Content::Run& overflowRun, InlineLayoutUnit availableWidth) const
{
    ASSERT(overflowRun.inlineItem.isText());
    auto& style = overflowRun.inlineItem.style();
    auto breakWords = style.wordBreak();
    if (breakWords == WordBreak::KeepAll)
        return { };
    auto& inlineTextItem = downcast<InlineTextItem>(overflowRun.inlineItem);
    if (breakWords == WordBreak::BreakAll) {
        // FIXME: Pass in the content logical left to be able to measure tabs.
        auto splitData = TextUtil::split(inlineTextItem.layoutBox(), inlineTextItem.start(), inlineTextItem.length(), overflowRun.logicalWidth, availableWidth, { });
        return LeftSide { splitData.length, splitData.logicalWidth, false };
    }
    // Find the hyphen position as follows:
    // 1. Split the text by taking the hyphen width into account
    // 2. Find the last hyphen position before the split position
    if (n_hyphenationIsDisabled || style.hyphens() != Hyphens::Auto || !canHyphenate(style.locale()))
        return { };

    auto runLength = inlineTextItem.length();
    unsigned limitBefore = style.hyphenationLimitBefore() == RenderStyle::initialHyphenationLimitBefore() ? 0 : style.hyphenationLimitBefore();
    unsigned limitAfter = style.hyphenationLimitAfter() == RenderStyle::initialHyphenationLimitAfter() ? 0 : style.hyphenationLimitAfter();
    // Check if this run can accommodate the before/after limits at all before start measuring text.
    if (limitBefore >= runLength || limitAfter >= runLength || limitBefore + limitAfter > runLength)
        return { };

    auto& fontCascade = style.fontCascade();
    // FIXME: We might want to cache the hyphen width.
    auto hyphenWidth = InlineLayoutUnit { fontCascade.width(TextRun { StringView { style.hyphenString() } }) };
    auto availableWidthExcludingHyphen = availableWidth - hyphenWidth;

    // For spaceWidth() see webkit.org/b/169613
    if (availableWidthExcludingHyphen <= 0 || !enoughWidthForHyphenation(availableWidthExcludingHyphen + fontCascade.spaceWidth(), fontCascade.pixelSize()))
        return { };

    auto splitData = TextUtil::split(inlineTextItem.layoutBox(), inlineTextItem.start(), runLength, overflowRun.logicalWidth, availableWidthExcludingHyphen, { });
    if (splitData.length < limitBefore)
        return { };

    auto textContent = inlineTextItem.layoutBox().textContext()->content;
    // Adjust before index to accommodate the limit-after value (it's the last potential hyphen location in this run).
    auto hyphenBefore = std::min(splitData.length, runLength - limitAfter) + 1;
    unsigned hyphenLocation = lastHyphenLocation(StringView(textContent).substring(inlineTextItem.start(), inlineTextItem.length()), hyphenBefore, style.locale());
    if (!hyphenLocation || hyphenLocation < limitBefore)
        return { };
    return LeftSide { hyphenLocation, TextUtil::width(inlineTextItem.layoutBox(), inlineTextItem.start(), hyphenLocation), true };
}

bool LineBreaker::Content::isAtContentBoundary(const InlineItem& inlineItem, const Content& content)
{
    // https://drafts.csswg.org/css-text-3/#line-break-details
    // Figure out if the new incoming content puts the uncommitted content on commit boundary.
    // e.g. <span>continuous</span> <- uncomitted content ->
    // [inline container start][text content][inline container end]
    // An incoming <img> box would enable us to commit the "<span>continuous</span>" content
    // while additional text content would not.
    ASSERT(!inlineItem.isFloat() && !inlineItem.isLineBreak());
    if (content.isEmpty()) {
        // Can't decide it yet.
        return false;
    }
    auto* lastUncomittedContent = &content.runs().last().inlineItem;
    if (inlineItem.isText()) {
        // any content' ' -> whitespace is always a commit boundary.
        if (downcast<InlineTextItem>(inlineItem).isWhitespace())
            return true;
        // <span>text -> the inline container start and the text content form an unbreakable continuous content.
        if (lastUncomittedContent->isContainerStart())
            return false;
        // </span>text -> need to check what's before the </span>.
        // text</span>text -> continuous content
        // <img></span>text -> commit bounday
        if (lastUncomittedContent->isContainerEnd()) {
            auto& runs = content.runs();
            // text</span><span></span></span>text -> check all the way back until we hit either a box or some text
            for (auto i = content.size(); i--;) {
                auto& previousInlineItem = runs[i].inlineItem;
                if (previousInlineItem.isContainerStart() || previousInlineItem.isContainerEnd())
                    continue;
                ASSERT(previousInlineItem.isText() || previousInlineItem.isBox());
                lastUncomittedContent = &previousInlineItem;
                break;
            }
            // Did not find any content (e.g. <span></span>text)
            if (lastUncomittedContent->isContainerEnd())
                return false;
        }
        // texttext -> continuous content.
        // text-text -> commit boundary.
        // ' 'text -> commit boundary.
        if (lastUncomittedContent->isText()) {
            auto& previousInlineTextItem = downcast<InlineTextItem>(*lastUncomittedContent);
            if (previousInlineTextItem.isWhitespace())
                return true;
            // When both these non-whitespace runs belong to the same layout box, it's guaranteed that
            // they are split at a soft breaking opportunity. See InlineTextItem::moveToNextBreakablePosition.
            if (&inlineItem.layoutBox() == &lastUncomittedContent->layoutBox())
                return true;
            // FIXME: check if <span>text-</span><span>text</span> should be handled here as well.
            return false;
        }
        // <img>text -> the inline box is on a commit boundary.
        if (lastUncomittedContent->isBox())
            return true;
        ASSERT_NOT_REACHED();
    }

    if (inlineItem.isBox()) {
        // <span><img> -> the inline container start and the content form an unbreakable continuous content.
        if (lastUncomittedContent->isContainerStart())
            return false;
        // </span><img> -> ok to commit the </span>.
        if (lastUncomittedContent->isContainerEnd())
            return true;
        // <img>text and <img><img> -> these combinations are ok to commit.
        if (lastUncomittedContent->isText() || lastUncomittedContent->isBox())
            return true;
        ASSERT_NOT_REACHED();
    }

    if (inlineItem.isContainerStart() || inlineItem.isContainerEnd()) {
        // <span><span> or </span><span> -> can't commit the previous content yet.
        if (lastUncomittedContent->isContainerStart() || lastUncomittedContent->isContainerEnd())
            return false;
        // ' '<span> -> let's commit the whitespace
        // text<span> -> but not yet the non-whitespace; we need to know what comes next (e.g. text<span>text or text<span><img>).
        if (lastUncomittedContent->isText())
            return downcast<InlineTextItem>(*lastUncomittedContent).isWhitespace();
        // <img><span> -> it's ok to commit the inline box content.
        // <img></span> -> the inline box and the closing inline container form an unbreakable continuous content.
        if (lastUncomittedContent->isBox())
            return inlineItem.isContainerStart();
        ASSERT_NOT_REACHED();
    }

    ASSERT_NOT_REACHED();
    return true;
}

void LineBreaker::Content::append(const InlineItem& inlineItem, InlineLayoutUnit logicalWidth)
{
    ASSERT(!inlineItem.isFloat());
    ASSERT(inlineItem.isLineBreak() || !isAtContentBoundary(inlineItem, *this));
    m_continousRuns.append({ inlineItem, logicalWidth });
    m_width += logicalWidth;
    // Figure out the trailing trimmable state.
    if (inlineItem.isBox() || inlineItem.isLineBreak())
        m_trailingTrimmableContent.reset();
    else if (inlineItem.isText()) {
        auto& inlineTextItem = downcast<InlineTextItem>(inlineItem);
        auto isFullyTrimmable = [&] {
            return inlineTextItem.isWhitespace() && !TextUtil::shouldPreserveTrailingWhitespace(inlineTextItem.style());
        };
        if (isFullyTrimmable()) {
            m_trailingTrimmableContent.width += logicalWidth;
            m_trailingTrimmableContent.isFullyTrimmable = true;
        } else if (auto trimmableWidth = inlineTextItem.style().letterSpacing()) {
            m_trailingTrimmableContent.width = trimmableWidth;
            m_trailingTrimmableContent.isFullyTrimmable = false;
        } else
            m_trailingTrimmableContent.reset();
    }
}

void LineBreaker::Content::reset()
{
    m_continousRuns.clear();
    m_trailingTrimmableContent.reset();
    m_width = 0_lu;
}

void LineBreaker::Content::trim(unsigned newSize)
{
    for (auto i = m_continousRuns.size(); i--;)
        m_width -= m_continousRuns[i].logicalWidth;
    m_continousRuns.shrink(newSize);
}

bool LineBreaker::Content::hasTextContentOnly() const
{
    // <span>text</span> is considered a text run even with the [container start][container end] inline items.
    // Due to commit boundary rules, we just need to check the first non-typeless inline item (can't have both [img] and [text])
    for (auto& run : m_continousRuns) {
        auto& inlineItem = run.inlineItem;
        if (inlineItem.isContainerStart() || inlineItem.isContainerEnd())
            continue;
        return inlineItem.isText();
    }
    return false;
}

bool LineBreaker::Content::hasNonContentRunsOnly() const
{
    // <span></span> <- non content runs.
    for (auto& run : m_continousRuns) {
        auto& inlineItem = run.inlineItem;
        if (inlineItem.isContainerStart() || inlineItem.isContainerEnd())
            continue;
        return false;
    }
    return true;
}

void LineBreaker::Content::TrailingTrimmableContent::reset()
{
    isFullyTrimmable = false;
    width = 0_lu;
}


}
}
#endif
