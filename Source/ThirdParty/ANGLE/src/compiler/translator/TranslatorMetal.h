//
// Copyright (c) 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// TranslatorMetal:
//   A GLSL-based translator that outputs shaders that fit GL_KHR_vulkan_glsl.
//   It takes into account some considerations for Metal backend also.
//   The shaders are then fed into glslang to spit out SPIR-V (libANGLE-side).
//   See: https://www.khronos.org/registry/vulkan/specs/misc/GL_KHR_vulkan_glsl.txt
//
//   The SPIR-V will then be translated to Metal Shading Language later in Metal backend.
//

#ifndef LIBANGLE_RENDERER_METAL_TRANSLATORMETAL_H_
#define LIBANGLE_RENDERER_METAL_TRANSLATORMETAL_H_

#include "compiler/translator/TranslatorVulkan.h"

namespace sh
{

class TranslatorMetal : public TranslatorVulkan
{
  public:
    TranslatorMetal(sh::GLenum type, ShShaderSpec spec);

  protected:
    ANGLE_NO_DISCARD bool translate(TIntermBlock *root,
                                    ShCompileOptions compileOptions,
                                    PerformanceDiagnostics *perfDiagnostics) override;
};

}  // namespace sh

#endif /* LIBANGLE_RENDERER_METAL_TRANSLATORMETAL_H_ */
