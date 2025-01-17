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
#include "InlineTextItem.h"

#if ENABLE(LAYOUT_FORMATTING_CONTEXT)

#include "BreakLines.h"
#include "FontCascade.h"
#include "InlineSoftLineBreakItem.h"

namespace WebCore {
namespace Layout {

static inline bool isWhitespaceCharacter(char character, bool preserveNewline)
{
    return character == ' ' || character == '\t' || (character == '\n' && !preserveNewline);
}

static unsigned moveToNextNonWhitespacePosition(const StringView& textContent, unsigned startPosition, bool preserveNewline)
{
    auto nextNonWhiteSpacePosition = startPosition;
    while (nextNonWhiteSpacePosition < textContent.length() && isWhitespaceCharacter(textContent[nextNonWhiteSpacePosition], preserveNewline))
        ++nextNonWhiteSpacePosition;
    return nextNonWhiteSpacePosition - startPosition;
}

static unsigned moveToNextBreakablePosition(unsigned startPosition, LazyLineBreakIterator lineBreakIterator, const RenderStyle& style)
{
    auto findNextBreakablePosition = [&](auto startPosition) {
        auto keepAllWordsForCJK = style.wordBreak() == WordBreak::KeepAll;
        auto breakNBSP = style.autoWrap() && style.nbspMode() == NBSPMode::Space;

        if (keepAllWordsForCJK) {
            if (breakNBSP)
                return nextBreakablePositionKeepingAllWords(lineBreakIterator, startPosition);
            return nextBreakablePositionKeepingAllWordsIgnoringNBSP(lineBreakIterator, startPosition);
        }

        if (lineBreakIterator.mode() == LineBreakIteratorMode::Default) {
            if (breakNBSP)
                return WebCore::nextBreakablePosition(lineBreakIterator, startPosition);
            return nextBreakablePositionIgnoringNBSP(lineBreakIterator, startPosition);
        }

        if (breakNBSP)
            return nextBreakablePositionWithoutShortcut(lineBreakIterator, startPosition);
        return nextBreakablePositionIgnoringNBSPWithoutShortcut(lineBreakIterator, startPosition);
    };

    auto textLength = lineBreakIterator.stringView().length();
    auto startPositionForNextBreakablePosition = startPosition;
    while (startPositionForNextBreakablePosition < textLength) {
        auto nextBreakablePosition = findNextBreakablePosition(startPositionForNextBreakablePosition);
        // Oftentimes the next breakable position comes back as the start position (most notably hyphens).
        if (nextBreakablePosition != startPosition)
            return nextBreakablePosition - startPosition;
        ++startPositionForNextBreakablePosition;
    }
    return textLength - startPosition;
}

void InlineTextItem::createAndAppendTextItems(InlineItems& inlineContent, const Box& inlineBox)
{
    auto& textContext = *inlineBox.textContext();
    auto text = textContext.content;
    if (!text.length())
        return inlineContent.append(InlineTextItem::createEmptyItem(inlineBox));

    auto& style = inlineBox.style();
    auto& font = style.fontCascade();
    LazyLineBreakIterator lineBreakIterator(text);
    unsigned currentPosition = 0;

    auto inlineItemWidth = [&](auto startPosition, auto length) -> Optional<InlineLayoutUnit> {
        if (!textContext.canUseSimplifiedContentMeasuring)
            return { };
        return TextUtil::width(inlineBox, startPosition, startPosition + length);
    };

    while (currentPosition < text.length()) {
        auto isSegmentBreakCandidate = [](auto character) {
            return character == '\n';
        };

        // Segment breaks with preserve new line style (white-space: pre, pre-wrap, break-spaces and pre-line) compute to forced line break.
        if (isSegmentBreakCandidate(text[currentPosition]) && style.preserveNewline()) {
            inlineContent.append(InlineSoftLineBreakItem::createSoftLineBreakItem(inlineBox, currentPosition));
            ++currentPosition;
            continue;
        }

        if (isWhitespaceCharacter(text[currentPosition], style.preserveNewline())) {
            auto length = moveToNextNonWhitespacePosition(text, currentPosition, style.preserveNewline());
            auto simpleSingleWhitespaceContent = textContext.canUseSimplifiedContentMeasuring && (length == 1 || style.collapseWhiteSpace());
            auto width = simpleSingleWhitespaceContent ? makeOptional(InlineLayoutUnit { font.spaceWidth() }) : inlineItemWidth(currentPosition, length);
            inlineContent.append(InlineTextItem::createWhitespaceItem(inlineBox, currentPosition, length, width));
            currentPosition += length;
            continue;
        }

        auto length = moveToNextBreakablePosition(currentPosition, lineBreakIterator, style);
        inlineContent.append(InlineTextItem::createNonWhitespaceItem(inlineBox, currentPosition, length, inlineItemWidth(currentPosition, length)));
        currentPosition += length;
    }
}

std::unique_ptr<InlineTextItem> InlineTextItem::createWhitespaceItem(const Box& inlineBox, unsigned start, unsigned length, Optional<InlineLayoutUnit> width)
{
    return makeUnique<InlineTextItem>(inlineBox, start, length, width, TextItemType::Whitespace);
}

std::unique_ptr<InlineTextItem> InlineTextItem::createNonWhitespaceItem(const Box& inlineBox, unsigned start, unsigned length, Optional<InlineLayoutUnit> width)
{
    return makeUnique<InlineTextItem>(inlineBox, start, length, width, TextItemType::NonWhitespace);
}

std::unique_ptr<InlineTextItem> InlineTextItem::createEmptyItem(const Box& inlineBox)
{
    return makeUnique<InlineTextItem>(inlineBox);
}

InlineTextItem::InlineTextItem(const Box& inlineBox, unsigned start, unsigned length, Optional<InlineLayoutUnit> width, TextItemType textItemType)
    : InlineItem(inlineBox, Type::Text)
    , m_start(start)
    , m_length(length)
    , m_width(width)
    , m_textItemType(textItemType)
{
}

InlineTextItem::InlineTextItem(const Box& inlineBox)
    : InlineItem(inlineBox, Type::Text)
{
}

std::unique_ptr<InlineTextItem> InlineTextItem::left(unsigned length) const
{
    RELEASE_ASSERT(length <= this->length());
    ASSERT(m_textItemType != TextItemType::Undefined);
    return makeUnique<InlineTextItem>(layoutBox(), start(), length, WTF::nullopt, m_textItemType);
}

std::unique_ptr<InlineTextItem> InlineTextItem::right(unsigned length) const
{
    RELEASE_ASSERT(length <= this->length());
    ASSERT(m_textItemType != TextItemType::Undefined);
    return makeUnique<InlineTextItem>(layoutBox(), end() - length, length, WTF::nullopt, m_textItemType);
}

}
}
#endif
