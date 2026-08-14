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

// Varying from the vertex shader: signed side of the stroke.
VARYING float vSide;

void MAIN()
      {
      // Analytic anti-aliasing:  the rasterizer interpolates vSide
      // linearly across the expanded quad (0 = line centre, ±1 =
      // edge).  Fade the alpha to 0 over the outer 40% of the
      // stroke so the line edge is smooth instead of a hard,
      // stair-stepped cutoff.  For a 1 px minor line this yields a
      // graceful sub-pixel falloff; for the 2 px major line it
      // gives visibly soft edges.
      float dist  = abs(vSide);
      float alpha = 1.0 - smoothstep(0.6, 1.0, dist);

      vec4 c = uColor;
      c.a   *= alpha;        // Modulate alpha with the edge fade
      c.rgb *= c.a;          // Premultiplied alpha per Qt Quick 3D
      FRAGCOLOR = c;
      }
