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

#pragma once

// This header shields the OpenCASCADE includes from the project's
// own logging macros (Log, Printf, Debug, etc.) which collide with
// OCCT's Standard:: namespace functions.
//
// ALWAYS include this header INSTEAD of the raw OCCT headers when
// including from ZCam project code that also includes "logger.h".

// Save current macro definitions so we can restore them after the
// OCCT headers have been included.
#ifdef Log
#define ZCAM_OCCT_SHIELD_RESTORE_LOG 1
#pragma push_macro("Log")
#undef Log
#endif

#ifdef Printf
#define ZCAM_OCCT_SHIELD_RESTORE_PRINTF 1
#pragma push_macro("Printf")
#undef Printf
#endif

#ifdef Debug
#define ZCAM_OCCT_SHIELD_RESTORE_DEBUG 1
#pragma push_macro("Debug")
#undef Debug
#endif

#ifdef Warning
#define ZCAM_OCCT_SHIELD_RESTORE_WARNING 1
#pragma push_macro("Warning")
#undef Warning
#endif

#ifdef Critical
#define ZCAM_OCCT_SHIELD_RESTORE_CRITICAL 1
#pragma push_macro("Critical")
#undef Critical
#endif

#ifdef Fatal
#define ZCAM_OCCT_SHIELD_RESTORE_FATAL 1
#pragma push_macro("Fatal")
#undef Fatal
#endif

#ifdef Info
#define ZCAM_OCCT_SHIELD_RESTORE_INFO 1
#pragma push_macro("Info")
#undef Info
#endif

#ifdef Assert
#define ZCAM_OCCT_SHIELD_RESTORE_ASSERT 1
#pragma push_macro("Assert")
#undef Assert
#endif

// Now include the OCCT headers.  These are not affected by our macros.
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopLoc_Location.hxx>
#include <Geom_Curve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <HLRBRep_PolyAlgo.hxx>
#include <HLRBRep_PolyHLRToShape.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_Array1OfTriangle.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <Standard_ErrorHandler.hxx>

// Restore project macros.
#ifdef ZCAM_OCCT_SHIELD_RESTORE_LOG
#pragma pop_macro("Log")
#undef ZCAM_OCCT_SHIELD_RESTORE_LOG
#endif

#ifdef ZCAM_OCCT_SHIELD_RESTORE_PRINTF
#pragma pop_macro("Printf")
#undef ZCAM_OCCT_SHIELD_RESTORE_PRINTF
#endif

#ifdef ZCAM_OCCT_SHIELD_RESTORE_DEBUG
#pragma pop_macro("Debug")
#undef ZCAM_OCCT_SHIELD_RESTORE_DEBUG
#endif

#ifdef ZCAM_OCCT_SHIELD_RESTORE_WARNING
#pragma pop_macro("Warning")
#undef ZCAM_OCCT_SHIELD_RESTORE_WARNING
#endif

#ifdef ZCAM_OCCT_SHIELD_RESTORE_CRITICAL
#pragma pop_macro("Critical")
#undef ZCAM_OCCT_SHIELD_RESTORE_CRITICAL
#endif

#ifdef ZCAM_OCCT_SHIELD_RESTORE_FATAL
#pragma pop_macro("Fatal")
#undef ZCAM_OCCT_SHIELD_RESTORE_FATAL
#endif

#ifdef ZCAM_OCCT_SHIELD_RESTORE_INFO
#pragma pop_macro("Info")
#undef ZCAM_OCCT_SHIELD_RESTORE_INFO
#endif

#ifdef ZCAM_OCCT_SHIELD_RESTORE_ASSERT
#pragma pop_macro("Assert")
#undef ZCAM_OCCT_SHIELD_RESTORE_ASSERT
#endif
