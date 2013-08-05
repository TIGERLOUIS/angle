#include "precompiled.h"
//
// Copyright (c) 2013 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// validationES.h: Validation functions for generic OpenGL ES entry point parameters

#include "libGLESv2/validationES.h"
#include "libGLESv2/Context.h"
#include "libGLESv2/Texture.h"
#include "libGLESv2/Framebuffer.h"
#include "libGLESv2/Renderbuffer.h"
#include "libGLESv2/formatutils.h"
#include "libGLESv2/main.h"

#include "common/mathutil.h"
#include "common/utilities.h"

namespace gl
{

bool validateRenderbufferStorageParameters(const gl::Context *context, GLenum target, GLsizei samples,
                                           GLenum internalformat, GLsizei width, GLsizei height,
                                           bool angleExtension)
{
    switch (target)
    {
      case GL_RENDERBUFFER:
        break;
      default:
        return gl::error(GL_INVALID_ENUM, false);
    }

    if (width < 0 || height < 0 || samples < 0)
    {
        return gl::error(GL_INVALID_VALUE, false);
    }

    if (!gl::IsValidInternalFormat(internalformat, context))
    {
        return gl::error(GL_INVALID_ENUM, false);
    }

    // ANGLE_framebuffer_multisample does not explicitly state that the internal format must be
    // sized but it does state that the format must be in the ES2.0 spec table 4.5 which contains
    // only sized internal formats. The ES3 spec (section 4.4.2) does, however, state that the
    // internal format must be sized and not an integer format if samples is greater than zero.
    if (!gl::IsSizedInternalFormat(internalformat, context->getClientVersion()))
    {
        return gl::error(GL_INVALID_ENUM, false);
    }

    if (gl::IsIntegerFormat(internalformat, context->getClientVersion()) && samples > 0)
    {
        return gl::error(GL_INVALID_OPERATION, false);
    }

    if (!gl::IsColorRenderingSupported(internalformat, context) &&
        !gl::IsDepthRenderingSupported(internalformat, context) &&
        !gl::IsStencilRenderingSupported(internalformat, context))
    {
        return gl::error(GL_INVALID_ENUM, false);
    }

    if (std::max(width, height) > context->getMaximumRenderbufferDimension())
    {
        return gl::error(GL_INVALID_VALUE, false);
    }

    // ANGLE_framebuffer_multisample states that the value of samples must be less than or equal
    // to MAX_SAMPLES_ANGLE (Context::getMaxSupportedSamples) while the ES3.0 spec (section 4.4.2)
    // states that samples must be less than or equal to the maximum samples for the specified
    // internal format.
    if (angleExtension)
    {
        if (samples > context->getMaxSupportedSamples())
        {
            return gl::error(GL_INVALID_VALUE, false);
        }
    }
    else
    {
        if (samples > context->getMaxSupportedFormatSamples(internalformat))
        {
            return gl::error(GL_INVALID_VALUE, false);
        }
    }

    GLuint handle = context->getRenderbufferHandle();
    if (handle == 0)
    {
        return gl::error(GL_INVALID_OPERATION, false);
    }

    return true;
}

bool validateBlitFramebufferParameters(gl::Context *context, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask,
                                       GLenum filter, bool fromAngleExtension)
{
    switch (filter)
    {
      case GL_NEAREST:
        break;
      case GL_LINEAR:
        if (fromAngleExtension)
        {
            return gl::error(GL_INVALID_ENUM, false);
        }
        break;
      default:
        return gl::error(GL_INVALID_ENUM, false);
    }

    if ((mask & ~(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)) != 0)
    {
        return gl::error(GL_INVALID_VALUE, false);
    }

    if (mask == 0)
    {
        // ES3.0 spec, section 4.3.2 specifies that a mask of zero is valid and no
        // buffers are copied.
        return false;
    }

    if (fromAngleExtension && (srcX1 - srcX0 != dstX1 - dstX0 || srcY1 - srcY0 != dstY1 - dstY0))
    {
        ERR("Scaling and flipping in BlitFramebufferANGLE not supported by this implementation.");
        return gl::error(GL_INVALID_OPERATION, false);
    }

    // ES3.0 spec, section 4.3.2 states that linear filtering is only available for the
    // color buffer, leaving only nearest being unfiltered from above
    if ((mask & ~GL_COLOR_BUFFER_BIT) != 0 && filter != GL_NEAREST)
    {
        return gl::error(GL_INVALID_OPERATION, false);
    }

    if (context->getReadFramebufferHandle() == context->getDrawFramebufferHandle())
    {
        if (fromAngleExtension)
        {
            ERR("Blits with the same source and destination framebuffer are not supported by this "
                "implementation.");
        }
        return gl::error(GL_INVALID_OPERATION, false);
    }

    gl::Framebuffer *readFramebuffer = context->getReadFramebuffer();
    gl::Framebuffer *drawFramebuffer = context->getDrawFramebuffer();
    if (!readFramebuffer || readFramebuffer->completeness() != GL_FRAMEBUFFER_COMPLETE ||
        !drawFramebuffer || drawFramebuffer->completeness() != GL_FRAMEBUFFER_COMPLETE)
    {
        return gl::error(GL_INVALID_FRAMEBUFFER_OPERATION, false);
    }

    if (drawFramebuffer->getSamples() != 0)
    {
        return gl::error(GL_INVALID_OPERATION, false);
    }

    gl::Rectangle sourceClippedRect, destClippedRect;
    bool partialCopy;
    if (!context->clipBlitFramebufferCoordinates(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
                                                 &sourceClippedRect, &destClippedRect, &partialCopy))
    {
        return gl::error(GL_INVALID_OPERATION, false);
    }

    bool sameBounds = srcX0 == dstX0 && srcY0 == dstY0 && srcX1 == dstX1 && srcY1 == dstY1;

    GLuint clientVersion = context->getClientVersion();

    if (mask & GL_COLOR_BUFFER_BIT)
    {
        gl::Renderbuffer *readColorBuffer = readFramebuffer->getReadColorbuffer();
        gl::Renderbuffer *drawColorBuffer = drawFramebuffer->getFirstColorbuffer();

        if (readColorBuffer && drawColorBuffer)
        {
            GLint readInternalFormat = readColorBuffer->getActualFormat();
            GLint drawInternalFormat = drawColorBuffer->getActualFormat();

            for (unsigned int i = 0; i < gl::IMPLEMENTATION_MAX_DRAW_BUFFERS; i++)
            {
                if (drawFramebuffer->isEnabledColorAttachment(i))
                {
                    GLint drawbufferAttachmentFormat = drawFramebuffer->getColorbuffer(i)->getActualFormat();

                    if (gl::IsNormalizedFixedPointFormat(readInternalFormat, clientVersion) &&
                        !gl::IsNormalizedFixedPointFormat(drawbufferAttachmentFormat, clientVersion))
                    {
                        return gl::error(GL_INVALID_OPERATION, false);
                    }

                    if (gl::IsUnsignedIntegerFormat(readInternalFormat, clientVersion) &&
                        !gl::IsUnsignedIntegerFormat(drawbufferAttachmentFormat, clientVersion))
                    {
                        return gl::error(GL_INVALID_OPERATION, false);
                    }

                    if (gl::IsSignedIntegerFormat(readInternalFormat, clientVersion) &&
                        !gl::IsSignedIntegerFormat(drawbufferAttachmentFormat, clientVersion))
                    {
                        return gl::error(GL_INVALID_OPERATION, false);
                    }

                    if (readColorBuffer->getSamples() > 0 && (readInternalFormat != drawbufferAttachmentFormat || !sameBounds))
                    {
                        return gl::error(GL_INVALID_OPERATION, false);
                    }
                }
            }

            if (gl::IsIntegerFormat(readInternalFormat, clientVersion) && filter == GL_LINEAR)
            {
                return gl::error(GL_INVALID_OPERATION, false);
            }

            if (fromAngleExtension)
            {
                const GLenum readColorbufferType = readFramebuffer->getReadColorbufferType();
                if (readColorbufferType != GL_TEXTURE_2D && readColorbufferType != GL_RENDERBUFFER)
                {
                    return gl::error(GL_INVALID_OPERATION, false);
                }

                for (unsigned int colorAttachment = 0; colorAttachment < gl::IMPLEMENTATION_MAX_DRAW_BUFFERS; colorAttachment++)
                {
                    if (drawFramebuffer->isEnabledColorAttachment(colorAttachment))
                    {
                        if (drawFramebuffer->getColorbufferType(colorAttachment) != GL_TEXTURE_2D &&
                            drawFramebuffer->getColorbufferType(colorAttachment) != GL_RENDERBUFFER)
                        {
                            return gl::error(GL_INVALID_OPERATION, false);
                        }

                        if (drawFramebuffer->getColorbuffer(colorAttachment)->getActualFormat() != readColorBuffer->getActualFormat())
                        {
                            return gl::error(GL_INVALID_OPERATION, false);
                        }
                    }
                }

                if (partialCopy && readFramebuffer->getSamples() != 0)
                {
                    return gl::error(GL_INVALID_OPERATION, false);
                }
            }
        }
    }

    if (mask & GL_DEPTH_BUFFER_BIT)
    {
        gl::Renderbuffer *readDepthBuffer = readFramebuffer->getDepthbuffer();
        gl::Renderbuffer *drawDepthBuffer = drawFramebuffer->getDepthbuffer();

        if (readDepthBuffer && drawDepthBuffer)
        {
            if (readDepthBuffer->getActualFormat() != drawDepthBuffer->getActualFormat())
            {
                return gl::error(GL_INVALID_OPERATION, false);
            }

            if (readDepthBuffer->getSamples() > 0 && !sameBounds)
            {
                return gl::error(GL_INVALID_OPERATION, false);
            }

            if (fromAngleExtension)
            {
                if (partialCopy)
                {
                    ERR("Only whole-buffer depth and stencil blits are supported by this implementation.");
                    return gl::error(GL_INVALID_OPERATION, false); // only whole-buffer copies are permitted
                }

                if (readDepthBuffer->getSamples() != 0 || drawDepthBuffer->getSamples() != 0)
                {
                    return gl::error(GL_INVALID_OPERATION, false);
                }
            }
        }
    }

    if (mask & GL_STENCIL_BUFFER_BIT)
    {
        gl::Renderbuffer *readStencilBuffer = readFramebuffer->getStencilbuffer();
        gl::Renderbuffer *drawStencilBuffer = drawFramebuffer->getStencilbuffer();

        if (fromAngleExtension && partialCopy)
        {
            ERR("Only whole-buffer depth and stencil blits are supported by this implementation.");
            return gl::error(GL_INVALID_OPERATION, false); // only whole-buffer copies are permitted
        }

        if (readStencilBuffer && drawStencilBuffer)
        {
            if (readStencilBuffer->getActualFormat() != drawStencilBuffer->getActualFormat())
            {
                return gl::error(GL_INVALID_OPERATION, false);
            }

            if (readStencilBuffer->getSamples() > 0 && !sameBounds)
            {
                return gl::error(GL_INVALID_OPERATION, false);
            }

            if (fromAngleExtension)
            {
                if (partialCopy)
                {
                    ERR("Only whole-buffer depth and stencil blits are supported by this implementation.");
                    return gl::error(GL_INVALID_OPERATION, false); // only whole-buffer copies are permitted
                }

                if (readStencilBuffer->getSamples() != 0 || drawStencilBuffer->getSamples() != 0)
                {
                    return gl::error(GL_INVALID_OPERATION, false);
                }
            }
        }
    }

    return true;
}

bool validateGetVertexAttribParameters(GLenum pname, int clientVersion)
{
    switch (pname)
    {
      case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
      case GL_VERTEX_ATTRIB_ARRAY_SIZE:
      case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
      case GL_VERTEX_ATTRIB_ARRAY_TYPE:
      case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
      case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
      case GL_CURRENT_VERTEX_ATTRIB:
        return true;

      case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
        // Don't verify ES3 context because GL_VERTEX_ATTRIB_ARRAY_DIVISOR_ANGLE uses
        // the same constant.
        META_ASSERT(GL_VERTEX_ATTRIB_ARRAY_DIVISOR == GL_VERTEX_ATTRIB_ARRAY_DIVISOR_ANGLE);
        return true;

      case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
        return ((clientVersion >= 3) ? true : gl::error(GL_INVALID_ENUM, false));

      default:
        return gl::error(GL_INVALID_ENUM, false);
    }
}

bool validateTexParamParameters(gl::Context *context, GLenum pname, GLint param)
{
    switch (pname)
    {
      case GL_TEXTURE_WRAP_R:
      case GL_TEXTURE_SWIZZLE_R:
      case GL_TEXTURE_SWIZZLE_G:
      case GL_TEXTURE_SWIZZLE_B:
      case GL_TEXTURE_SWIZZLE_A:
      case GL_TEXTURE_BASE_LEVEL:
      case GL_TEXTURE_MAX_LEVEL:
      case GL_TEXTURE_COMPARE_MODE:
      case GL_TEXTURE_COMPARE_FUNC:
      case GL_TEXTURE_MIN_LOD:
      case GL_TEXTURE_MAX_LOD:
        if (context->getClientVersion() < 3)
        {
            return gl::error(GL_INVALID_ENUM, false);
        }
        break;

      default: break;
    }

    switch (pname)
    {
      case GL_TEXTURE_WRAP_S:
      case GL_TEXTURE_WRAP_T:
      case GL_TEXTURE_WRAP_R:
        switch (param)
        {
          case GL_REPEAT:
          case GL_CLAMP_TO_EDGE:
          case GL_MIRRORED_REPEAT:
            return true;
          default:
            return gl::error(GL_INVALID_ENUM, false);
        }

      case GL_TEXTURE_MIN_FILTER:
        switch (param)
        {
          case GL_NEAREST:
          case GL_LINEAR:
          case GL_NEAREST_MIPMAP_NEAREST:
          case GL_LINEAR_MIPMAP_NEAREST:
          case GL_NEAREST_MIPMAP_LINEAR:
          case GL_LINEAR_MIPMAP_LINEAR:
            return true;
          default:
            return gl::error(GL_INVALID_ENUM, false);
        }
        break;

      case GL_TEXTURE_MAG_FILTER:
        switch (param)
        {
          case GL_NEAREST:
          case GL_LINEAR:
            return true;
          default:
            return gl::error(GL_INVALID_ENUM, false);
        }
        break;

      case GL_TEXTURE_USAGE_ANGLE:
        switch (param)
        {
          case GL_NONE:
          case GL_FRAMEBUFFER_ATTACHMENT_ANGLE:
            return true;
          default:
            return gl::error(GL_INVALID_ENUM, false);
        }
        break;

      case GL_TEXTURE_MAX_ANISOTROPY_EXT:
        if (!context->supportsTextureFilterAnisotropy())
        {
            return gl::error(GL_INVALID_ENUM, false);
        }

        // we assume the parameter passed to this validation method is truncated, not rounded
        if (param < 1)
        {
            return gl::error(GL_INVALID_VALUE, false);
        }
        return true;

      case GL_TEXTURE_MIN_LOD:
      case GL_TEXTURE_MAX_LOD:
        // any value is permissible
        return true;

      case GL_TEXTURE_COMPARE_MODE:
        switch (param)
        {
          case GL_NONE:
          case GL_COMPARE_REF_TO_TEXTURE:
            return true;
          default:
            return gl::error(GL_INVALID_ENUM, false);
        }
        break;

      case GL_TEXTURE_COMPARE_FUNC:
        switch (param)
        {
          case GL_LEQUAL:
          case GL_GEQUAL:
          case GL_LESS:
          case GL_GREATER:
          case GL_EQUAL:
          case GL_NOTEQUAL:
          case GL_ALWAYS:
          case GL_NEVER:
            return true;
          default:
            return gl::error(GL_INVALID_ENUM, false);
        }
        break;

      case GL_TEXTURE_SWIZZLE_R:
      case GL_TEXTURE_SWIZZLE_G:
      case GL_TEXTURE_SWIZZLE_B:
      case GL_TEXTURE_SWIZZLE_A:
      case GL_TEXTURE_BASE_LEVEL:
      case GL_TEXTURE_MAX_LEVEL:
        UNIMPLEMENTED();
        return true;

      default:
        return gl::error(GL_INVALID_ENUM, false);
    }
}

bool validateSamplerObjectParameter(GLenum pname)
{
    switch (pname)
    {
      case GL_TEXTURE_MIN_FILTER:
      case GL_TEXTURE_MAG_FILTER:
      case GL_TEXTURE_WRAP_S:
      case GL_TEXTURE_WRAP_T:
      case GL_TEXTURE_WRAP_R:
      case GL_TEXTURE_MIN_LOD:
      case GL_TEXTURE_MAX_LOD:
      case GL_TEXTURE_COMPARE_MODE:
      case GL_TEXTURE_COMPARE_FUNC:
        return true;

      default:
        return gl::error(GL_INVALID_ENUM, false);
    }
}

}