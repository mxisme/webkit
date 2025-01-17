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

#include "DisplayInlineContent.h"

namespace WebCore {

namespace LineLayoutTraversal {

static FloatPoint linePosition(float logicalLeft, float logicalTop)
{
    return FloatPoint(logicalLeft, roundf(logicalTop));
}

class DisplayRunPath {
public:
    DisplayRunPath(const Display::InlineContent& inlineContent, size_t startIndex, size_t endIndex)
        : m_inlineContent(&inlineContent)
        , m_endIndex(endIndex)
        , m_runIndex(startIndex)
    { }
    DisplayRunPath(DisplayRunPath&&) = default;
    DisplayRunPath(const DisplayRunPath&) = default;
    DisplayRunPath& operator=(const DisplayRunPath&) = default;
    DisplayRunPath& operator=(DisplayRunPath&&) = default;

    FloatRect rect() const { return logicalRect(); }
    FloatRect logicalRect() const
    {
        auto logicalRect = run().logicalRect();
        FloatPoint position = linePosition(logicalRect.left(), logicalRect.top());
        return { position, logicalRect.size() };
    }

    bool isLeftToRightDirection() const { return true; }
    bool dirOverride() const { return false; }
    bool isLineBreak() const { return run().isLineBreak(); }

    bool hasHyphen() const { return false; } // FIXME: Implement.
    StringView text() const { return run().textContext()->content(); }
    unsigned localStartOffset() const { return run().textContext()->start(); }
    unsigned localEndOffset() const { return run().textContext()->end(); }
    unsigned length() const { return run().textContext()->length(); }

    bool isLastOnLine() const
    {
        if (isLast())
            return true;
        auto& next = runs()[m_runIndex + 1];
        return run().lineIndex() != next.lineIndex();
    }
    bool isLast() const
    {
        return m_runIndex + 1 == m_endIndex;
    };

    void traverseNextTextBoxInVisualOrder()
    {
        ASSERT(!atEnd());
        ++m_runIndex;
    }
    void traverseNextTextBoxInTextOrder() {  traverseNextTextBoxInVisualOrder(); }

    bool operator==(const DisplayRunPath& other) const { return m_inlineContent == other.m_inlineContent && m_runIndex == other.m_runIndex; }
    bool atEnd() const { return m_runIndex == m_endIndex; }

private:
    const Display::InlineContent::Runs& runs() const { return m_inlineContent->runs; }
    const Display::Run& run() const { return runs()[m_runIndex]; }

    RefPtr<const Display::InlineContent> m_inlineContent;
    size_t m_endIndex { 0 };
    size_t m_runIndex { 0 };
};

}
}

#endif
