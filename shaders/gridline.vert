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

// Screen-space line expansion vertex shader for the Grid element.
// Snippet for Qt Quick 3D CustomMaterial (no #version line — the
// renderer prepends the correct profile plus all built-ins like
// VERTEX, MODELVIEWPROJECTION_MATRIX, POSITION, UV0).
//
// Each grid line is uploaded by
// TessGeometry::setLinesForExpandedQuads() as a flat degenerate
// quad: BOTH corners of one end share the same POSITION (the end
// point), and TexCoord0 carries per-vertex metadata:
//      uv.x = side  (-1 / +1)  — which side of the line centre
//      uv.y = axis  ( 0 = X line, 1 = Y line )
//
// The shader projects the quad corner and the world-space line
// axis into clip space, derives the on-screen line direction in
// NDC and displaces the corner perpendicular to it by exactly
// uHalfWidthPx * 2 / uViewportSize NDC, scaled by clip.w so the
// perspective division restores exact pixels.  Because the
// expansion happens in CLIP SPACE, the on-screen stroke has a
// CONSTANT pixel width in EVERY view — zoom, pan and rotation no
// longer trigger a geometry rebuild at all, and there is no
// Z-fighting with the z=0 scene because the quad stays flat in
// the XY plane (the model's 1 mm offset still wins the depth
// test).

void MAIN()
      {
      vec4 pos    = vec4(VERTEX, 1.0);
      vec4 clipPos = MODELVIEWPROJECTION_MATRIX * pos;

      // World-space line axis:  the geometry builder tags the quad
      // with uv.y = 0 for an X-running line (dir = e_x), 1 for a
      // Y-running line (dir = e_y).  Projecting the axis through the
      // same MVP matrix and taking the NDC difference of
      // (origin + axis) - origin yields the on-screen line
      // direction without needing the segment length.  Translation
      // cancels out, so the result is exact for orthographic and
      // near-exact for perspective projection (perspective varies
      // along the segment only, which is the desired behaviour).
      vec3 axisWorld = (UV0.y < 0.5) ? vec3(1.0, 0.0, 0.0)
                                     : vec3(0.0, 1.0, 0.0);
      vec4 clipRef   = MODELVIEWPROJECTION_MATRIX * vec4(axisWorld, 1.0);
      vec4 clipOrg   = MODELVIEWPROJECTION_MATRIX * vec4(0.0, 0.0, 0.0, 1.0);
      vec2 ndcDir    = (clipRef.xy / clipRef.w) - (clipOrg.xy / clipOrg.w);

      float len  = length(ndcDir);
      vec2 dir   = (len > 1e-8) ? ndcDir / len : vec2(1.0, 0.0);
      vec2 nrm   = vec2(-dir.y, dir.x);

      // Pixel-perfect expansion:  half-width in NDC units, scaled by
      // clip.w so the perspective division restores exact pixels.
      vec2 vpSize = vec2(uViewportWidth, uViewportHeight);
      vec2 offsetNdc = nrm * (uHalfWidthPx * 2.0 / vpSize) * UV0.x;
      clipPos.xy    += offsetNdc * clipPos.w;

      POSITION = clipPos;
      }
