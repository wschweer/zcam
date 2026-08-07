//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2025-2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

// Grid line fragment shader — constant colour, no lighting.
// Snippet for Qt Quick 3D CustomMaterial (no #version line).
// Anti-aliasing is provided by the MSAA pipeline (VeryHigh in the
// SceneEnvironment); the vertex shader has already expanded the
// quad to exact pixel width, so this stage only fills the colour.
// Output is fully PREMULTIPLIED, per Qt Quick 3D specification.

void MAIN()
      {
      vec4 c = uColor;
      c.rgb *= c.a;
      FRAGCOLOR = c;
      }
