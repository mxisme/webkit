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

#include "LayoutPoint.h"
#include "LineLayoutTraversal.h"

namespace WebCore {

class GraphicsContext;
class RenderBlockFlow;
class RenderLineBreak;
struct PaintInfo;

namespace Display {
struct InlineContent;
}

namespace Layout {
class LayoutTreeContent;
class LayoutState;
}

namespace LayoutIntegration {

class LineLayout {
    WTF_MAKE_FAST_ALLOCATED;
public:
    LineLayout(const RenderBlockFlow&);
    ~LineLayout();

    static bool canUseFor(const RenderBlockFlow&);

    void layout();

    LayoutUnit contentLogicalHeight() const { return m_contentLogicalHeight; }
    size_t lineCount() const;

    LayoutUnit firstLineBaseline() const;
    LayoutUnit lastLineBaseline() const;

    void collectOverflow(RenderBlockFlow&);

    const Display::InlineContent* displayInlineContent() const;

    void paint(PaintInfo&, const LayoutPoint& paintOffset);

    LineLayoutTraversal::TextBoxIterator textBoxesFor(const RenderText&) const;
    LineLayoutTraversal::ElementBoxIterator elementBoxFor(const RenderLineBreak&) const;

private:
    const Layout::Container& rootLayoutBox() const;
    void prepareRootGeometryForLayout();
    ShadowData* debugTextShadow();

    const RenderBlockFlow& m_flow;
    std::unique_ptr<const Layout::LayoutTreeContent> m_treeContent;
    std::unique_ptr<Layout::LayoutState> m_layoutState;
    LayoutUnit m_contentLogicalHeight;
};

}
}

#endif
