//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "machinegcode.h"

//---------------------------------------------------------
//   properties
//---------------------------------------------------------

const std::string_view MachineGCode::properties() const {
      return R"json({
                      "class": "Machine",
                      "rows": [
                        {
                          "label": " ",
                          "cells": [
                            {
                              "name": "name",
                              "sublabel": "Name",
                              "type": "string"
                            },
                            {
                              "name": "type",
                              "sublabel": "Type",
                              "type": "machineType"
                            },
                            {
                              "name": "boardType",
                              "sublabel": "Board",
                              "type": "boardType"
                            }
                          ]
                        },
                        {
                          "label": "Description",
                          "cells": [
                            {
                              "name": "description",
                              "type": "multiline"
                            }
                          ]
                        },
                        {
                          "cells": [
                            {
                              "name": "line",
                              "type": "line"
                            }
                          ]
                        },
                        {
                          "columns": 2,
                          "cells": [
                            {
                              "name": "maxTravel",
                              "label": "Travel",
                              "type": "vector3d",
                              "unit": "mm",
                              "default": [
                                100.0,
                                100.0,
                                100.0
                              ]
                            },
                            {
                              "name": "travelSpeed",
                              "label": "Travel Speed",
                              "type": "float",
                              "unit": "mm/s",
                              "min": 0.0,
                              "max": 100000.0,
                              "default": 2000.0
                            },
                            {
                              "name": "framingSpeed",
                              "label": "Framing Speed",
                              "type": "float",
                              "unit": "mm/s",
                              "min": 0.0,
                              "max": 100000.0,
                              "default": 0.0
                            },
                            {
                              "label": "Safe Dist",
                              "cells": [
                                {
                                  "name": "safeDist1",
                                  "sublabel": "Safe 1",
                                  "type": "float",
                                  "unit": "mm",
                                  "min": 0.0,
                                  "max": 1000.0,
                                  "default": 0.0
                                },
                                {
                                  "name": "safeDist2",
                                  "sublabel": "Safe 2",
                                  "type": "float",
                                  "unit": "mm",
                                  "min": 0.0,
                                  "max": 1000.0,
                                  "default": 0.0
                                }
                              ]
                            },
                            {
                              "name": "maxFeed",
                              "label": "Max Feed",
                              "type": "vector3d",
                              "unit": "mm/s",
                              "default": [
                                0.0,
                                0.0,
                                0.0
                              ]
                            },
                            {
                              "name": "maxAcceleration",
                              "label": "Max Accel",
                              "type": "vector3d",
                              "unit": "mm/s²",
                              "default": [
                                0.0,
                                0.0,
                                0.0
                              ]
                            },
                            {
                              "label": "Spindle",
                              "cells": [
                                {
                                  "name": "minSpindle",
                                  "sublabel": "Min",
                                  "type": "float",
                                  "unit": "rpm",
                                  "min": 0.0,
                                  "max": 1000000.0,
                                  "default": 0.0
                                },
                                {
                                  "name": "maxSpindle",
                                  "sublabel": "Max",
                                  "type": "float",
                                  "unit": "rpm",
                                  "min": 0.0,
                                  "max": 1000000.0,
                                  "default": 0.0
                                }
                              ]
                            },
                            {
                              "name": "line",
                              "type": "line",
                              "colSpan": 2
                            },
                            {
                              "label": "Precision",
                              "cells": [
                                {
                                  "name": "precision",
                                  "sublabel": "Prec",
                                  "type": "float",
                                  "unit": "mm",
                                  "min": 0.001,
                                  "max": 10.0,
                                  "precision": 3,
                                  "default": 0.001
                                },
                                {
                                  "name": "ncPrecision",
                                  "sublabel": "NC Prec",
                                  "type": "float",
                                  "unit": "mm",
                                  "min": 0.001,
                                  "max": 10.0,
                                  "precision": 3,
                                  "default": 0.001
                                }
                              ]
                            },
                            {
                              "name": "circlePrecision",
                              "label": "Circle Prec",
                              "type": "float",
                              "unit": "mm",
                              "min": 0.001,
                              "max": 10.0,
                              "precision": 3,
                              "default": 0.001
                            },
                            {
                              "name": "line",
                              "type": "line",
                              "colSpan": 2
                            }
                          ]
                        }
                      ]
                          })json";
      }
