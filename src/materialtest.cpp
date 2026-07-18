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

#include <QTextLine>
#include <QCoreApplication>
#include "materialtest.h"
#include "zcam.h"
#include "polygon.h"
#include "rectangle.h"
#include "text.h"
#include "cad.h"
#include "cam.h"
#include "laserlayer.h"
#include "fixture.h"
#include "framing.h"
#include "grid.h"
#include "stock.h"
#include "project.h"
#include "projectmanager.h"
#include "treemodel.h"
#include "logger.h"

//---------------------------------------------------------
//   MaterialTest
//---------------------------------------------------------

MaterialTest::MaterialTest(ZCam* zcam, Element* parent) : Element3d(zcam, parent) {
      createChildren();
      connect(this, &MaterialTest::rowsChanged, [this] { createChildren(); });
      connect(this, &MaterialTest::columnsChanged, [this] { createChildren(); });
      connect(this, &MaterialTest::boxHeightChanged, [this] { createChildren(); });
      connect(this, &MaterialTest::boxWidthChanged, [this] { createChildren(); });
      connect(this, &MaterialTest::showBorderChanged, [this] {
            borderL->set_show(showBorder());
            borderL->set_burn(showBorder());
            });
      connect(this, &MaterialTest::showTextChanged, [this] {
            textL->set_show(showText());
            textL->set_burn(showText());
            createChildren();
            });
      connect(this, &MaterialTest::materialLayerChanged, [this] { createChildren(); });
      connect(this, &MaterialTest::textLayerChanged, [this] { createChildren(); });
      connect(this, &MaterialTest::borderLayerChanged, [this] { createChildren(); });
      }

//---------------------------------------------------------
//   createFiberLaserProject  (static local helper)
//    Tear down the current project and create a fresh empty
//    project skeleton using ProjectManager::newProject(), then
//    strip the demo content so the test routines can populate
//    it with test-specific content.  Returns the new Project*.
//---------------------------------------------------------

static Project* createFiberLaserProject(ZCam* zcam) {
      ProjectManager* pm = zcam->projectManager();
      // newProject() clears the undo stack, destroys the old tree,
      // creates a fresh RootElement + Project with demo content.
      // The default machine from Config is applied by startNewProject().
      pm->startNewProject();

      Project* project = zcam->project();

      pm->endNewProject();
      return project;
      }

//---------------------------------------------------------
//   finalizeProject  (static local helper)
//    Common post-processing after a test project has been
//    populated: update the tree model, resolve the machine,
//    refresh CAM data, and clear the dirty/cam-dirty flags.
//---------------------------------------------------------

static void finalizeProject(ZCam* zcam) {
      // Use update() which correctly handles the root element relationship.
      // Do NOT call setRoot(zcam->rootElement()) directly because
      // rootElement() returns Project (a child of RootElement) while
      // treeModel->_root is RootElement — passing Project to setRoot()
      // would trigger deleteLater() on RootElement, destroying the
      // entire element tree.
      zcam->projectManager()->update();

      // Notify QML that the grid element may have changed.
      if (zcam->project())
            emit zcam->project()->gridElementChanged();

      if (zcam->project()) {
            zcam->project()->resolveMachine();
            zcam->project()->updateCadLayerVisibility();
            }
      // The CAM data is NOT refreshed automatically here because
      // processTileLines() may call createFill() for filled elements,
      // which can be extremely expensive on the main thread.
      // Instead, set camDirty so the user can trigger a refresh
      // manually via the Cam button.
      zcam->setCamDirty(true);
      }

//---------------------------------------------------------
//   createMaterialTest
//---------------------------------------------------------

void ZCam::createMaterialTest() {
      ProjectManager* pm = projectManager();
      // Preserve the machine from the current project before
      // creating a new one.  The Machine* is owned by the
      // Machines asset (not by the Project), so the pointer
      // remains valid across project recreation.
      Machine* savedMachine = project() ? project()->machine() : nullptr;
      pm->startNewProject();

      // Restore the machine to the new project so the material
      // test pattern uses the same machine as the current project.
      if (savedMachine)
            project()->set_machine(savedMachine);

      project()->setName("Material-Test");

      Cad* cad               = project()->cad();
      Cam* cam               = project()->cam();
      Fixture* fixture       = project()->fixture();
      fixture->_saveChildren = false;

      cad->setExpanded(true);
      cam->setExpanded(true);
      fixture->setExpanded(false);

      // Create a Pattern layer holding the MaterialTest element
      auto layer = new Layer(this, cad);
      layer->setName("Pattern");
      layer->setExpanded(true);
      cad->addChild(layer);

      auto mtest           = new MaterialTest(this, layer);
      mtest->_saveChildren = false;
      mtest->setName("Testpattern");
      mtest->setExpanded(false);

      // Center the test pattern on the machine work area.
      // The machine work area spans (0,0) to (maxTravel.x, maxTravel.y),
      // so the centre is at (maxTravel.x/2, maxTravel.y/2).
      // createChildren() positions everything around the local origin,
      // so setting the element's pos to the work-area centre shifts
      // the entire pattern there.
      if (savedMachine) {
            QVector3D mt = savedMachine->maxTravel();
            mtest->set_pos(QVector3D(mt.x() * 0.5, mt.y() * 0.5, 0.0));
            }

      layer->addChild(mtest);

      // Create a LaserLayer linked to the pattern layer
      auto ll = new LaserLayer(this, fixture);
      ll->setName("LL-Pattern");
      ll->set_baseElement(layer);
      auto recipes = this->recipes();
      if (recipes && recipes->recipeCount() > 0)
            ll->set_recipe(recipes->recipePtr(0));
      fixture->addChild(ll);

      // Create a Grid covering the machine work area
      auto grid = new Grid(this, project());
      project()->addChild(grid);

      // Notify QML that the grid element is now available.
      emit project() -> gridElementChanged();

      //      finalizeProject(this);
      pm->markDirty();
      pm->endNewProject();
      }

//---------------------------------------------------------
//   genRowText
//---------------------------------------------------------

QString MaterialTest::genRowText(int row) const {
      QString s;
      double v = rowValue(row);
      switch (ParameterType(rowParameter())) {
            case ParameterType::Speed: s = QString::fromStdString(format("{:4.0f}", v)); break;
            case ParameterType::Power: s = QString::fromStdString(format("{:2.1f}", v)); break;
            case ParameterType::Interval: s = QString::fromStdString(format("{:0.3f}", v)); break;
            case ParameterType::Frequency: s = QString::fromStdString(format("{:4.0f}", v)); break;
            case ParameterType::Count: s = QString::fromStdString(format("{:2.0f}", v)); break;
            case ParameterType::Pulse: s = QString::fromStdString(format("{:4.0f}", v)); break;
            case ParameterType::None: break;
            }
      return s;
      }

//---------------------------------------------------------
//   genColText
//---------------------------------------------------------

QString MaterialTest::genColText(int col) const {
      QString s;
      double v = columnValue(col);
      switch (ParameterType(columnParameter())) {
            case ParameterType::Speed: s = QString::fromStdString(format("{:4.0f}", v)); break;
            case ParameterType::Power: s = QString::fromStdString(format("{:2.1f}", v)); break;
            case ParameterType::Interval: s = QString::fromStdString(format("{:0.3f}", v)); break;
            case ParameterType::Frequency: s = QString::fromStdString(format("{:4.0f}", v)); break;
            case ParameterType::Count: s = QString::fromStdString(format("{:2.0f}", v)); break;
            case ParameterType::Pulse: s = QString::fromStdString(format("{:4.0f}", v)); break;
            case ParameterType::None: break;
            }
      return s;
      }

static QString label(ParameterType t) {
      switch (t) {
            case ParameterType::None: return "None";
            case ParameterType::Speed: return "Speed (mm/s)";
            case ParameterType::Power: return "Power (%)";
            case ParameterType::Interval: return "Interval (µm)";
            case ParameterType::Frequency: return "Frequency (KHz)";
            case ParameterType::Count: return "Count";
            case ParameterType::Pulse: return "Pulse (nm)";
            }
      }

//---------------------------------------------------------
//   updateChildren
//---------------------------------------------------------

void MaterialTest::updateChildren() {
#if 0
      zcam->project()->beginReset();
      createChildren();
      wcam->projectTreeModel()->endReset();
#endif
      }

//---------------------------------------------------------
//   createChildren
//---------------------------------------------------------

void MaterialTest::createChildren() {
      // First delete the fixture LaserLayers whose baseElement is a
      // child of this MaterialTest (i.e. the ones created by previous
      // createChildren() calls).  Preserve LaserLayers created
      // externally (e.g. LL-Pattern whose baseElement is the parent
      // Pattern layer).
      //
      // This MUST be done before deleting the MaterialTest children,
      // because the LaserLayers hold raw baseElement pointers to those
      // Layer children.  Deleting the Layers first would leave dangling
      // pointers, causing use-after-free when we try to check
      // baseElement()->parent().
      //
      // emit remove3dElement BEFORE deleting so QML can synchronously
      // destroy the corresponding Model node while the C++ element is
      // still alive (QML needs the pointer to find the matching node).
      Fixture* fixture = zcam->project()->fixture();
      if (!fixture) {
            Critical("no fixture");
            return;
            }
      if (fixture) {
            QList<Element*> toDelete;
            for (Element* c : fixture->children()) {
                  auto* ll = qobject_cast<LaserLayer*>(c);
                  if (ll && ll->baseElement() && ll->baseElement()->parent() == this)
                        toDelete.append(c);
                  }
            for (Element* c : toDelete)
                  emit zcam->remove3dElement(qobject_cast<Element3d*>(c));
            for (Element* c : toDelete) {
                  fixture->removeChild(c);
                  delete c;
                  }
            }

      // Tell QML to remove each child's Model node before deleting
      // the C++ element (QML needs the pointer to find the node).
      QList<Element*> oldChildren = children();
      for (Element* c : oldChildren)
            emit zcam->remove3dElement(qobject_cast<Element3d*>(c));

      // Now safe to delete all children of this MaterialTest element
      while (!children().isEmpty()) {
            Element* c = children().takeLast();
            delete c;
            }

      double xd = boxWidth() * .1; // distance between rectangles
      double yd = boxHeight() * .1;

      double w = xd + boxWidth(); // cell width
      double h = yd + boxHeight();

      float tw = (columns() * w) + xd;
      float th = (rows() * h) + yd;

      // Debug("tw {} columns {}  boxWidth  {} xd {}", tw, columns(), boxWidth(), xd);
      // Debug("th {} rows    {}  boxHeight {} yd {}", th, rows(),    boxHeight(), yd);

      //      double textWidth  = w * 2;
      //      double textHeight = h * 3;

      float xtext = showText() ? 10 : 0;
      float ytext = showText() ? 20 : 0;
      QVector2D boxSize {tw + xtext, th + ytext};
      QRectF samples(0, 0, tw, th);
      samples.moveCenter(QPointF());

      // samples has inverted y axis as qt coordinates are different from wcam
      // top is negative, bottom is positive

      // Debug("samples top {}  bottom {}", samples.top(), samples.bottom());

      auto sampleSize = QVector2D(boxWidth(), boxHeight());
      for (int row = 0; row < rows(); ++row) {
            double rv = rowValue(row);
            for (int column = 0; column < columns(); ++column) {

                  //=====================================================================
                  //    - create an Layer
                  //    - add a Rectangle to Layer
                  //    - create LaserLayer for Layer
                  //=====================================================================

                  Layer* layer = new Layer(zcam, this);
                  layer->setName(format("layer-{}-{}", row, column).c_str());
                  layer->setExpanded(false);
                  addChild(layer);

                  double cv      = columnValue(column);
                  LaserLayer* ll = new LaserLayer(zcam, fixture);
                  ll->setName(format("ll-{}-{}", row, column).c_str());
                  ll->set_recipe(materialLayer());
                  ll->set_baseElement(layer);
                  ll->set_overrideType1(int(rowParameter()));
                  ll->set_overrideValue1(rv);
                  ll->set_overrideType2(int(columnParameter()));
                  ll->set_overrideValue2(cv);
                  fixture->addChild(ll);

                  auto r = new Rectangle(zcam, layer);
                  r->set_lockSize(int(LockScaleMode::Off));
                  r->setName("rectangle");
                  r->set_size(sampleSize);
                  double xx = samples.left() + xd + boxWidth() * .5;
                  double yy = samples.top() + yd + boxHeight() * .5;
                  r->set_pos(QVector3D(xx + column * w, yy + row * h, 0.0));
                  r->set_fill(true);
                  r->setColor(QColor("gray"));
                  layer->addChild(r);
                  }
            }
      if (showBorder()) {
            borderL = new Layer(zcam, this);
            borderL->setName("layer-border");
            addChild(borderL);

            LaserLayer* ll = new LaserLayer(zcam, fixture);
            ll->setName("ll-border");
            ll->set_baseElement(borderL);
            ll->set_recipe(borderLayer());
            fixture->addChild(ll);

            auto r = new Rectangle(zcam, borderL);
            r->set_lockSize(int(LockScaleMode::Off));
            r->setName("border");
            r->set_size(boxSize);
            r->set_pos(QVector3D(xtext * .5, 0, 0));
            r->set_lineWidth(0.5);
            r->set_fill(true);
            borderL->addChild(r);
            }
      if (showText()) {
            textL = new Layer(zcam, this);
            textL->setName("layer-text");
            addChild(textL);

            const Recipe* lls   = materialLayer();
            const LaserPass* ls = nullptr;
            if (lls) {
                  // the laser values of the first pass are marked on the
                  // material test card
                  ls = lls->layer(0);
                  }
            else
                  Debug("no LaserSettings");

            QString font = "Noto Sans";
            double x     = -(columns() * w * .5) + 1.0; // 1mm left border
            double y     = (rows() * h * .5) + 5;
            struct InfoItem {
                  ParameterType type;
                  bool create;
                  };
            // desired infos for header text
            InfoItem infos[5] = {
                     {    ParameterType::Power, true},
                     {    ParameterType::Speed, true},
                     {ParameterType::Frequency, true},
                     {    ParameterType::Pulse, true},
                     { ParameterType::Interval, true}
                  };
            // switch off parameters which are in row/column
            for (auto& i : infos) {
                  i.create =
                      i.type != ParameterType(rowParameter()) && i.type != ParameterType(columnParameter());
                  }

            QString title   = description();
            QString title2  = QDateTime::currentDateTime().toString("dd.MM.yy hh:mm");
            title          += " - " + title2;
            QString s;
            if (ls) {
                  for (const auto& i : infos) {
                        if (!i.create)
                              continue;
                        if (!s.isEmpty())
                              s += " ";
                        switch (i.type) {
                              default:
                              case ParameterType::Power: s += QString("%1%").arg(ls->power()); break;
                              case ParameterType::Speed: s += QString("%1mm/s").arg(ls->speed()); break;
                              case ParameterType::Frequency:
                                    s += QString("%1KHz").arg(ls->frequency());
                                    break;
                              case ParameterType::Pulse: s += QString("%1ns").arg(ls->pulseWidth()); break;
                              case ParameterType::Interval:
                                    s += QString("%1µ").arg(ls->interval() * 1000);
                                    break;
                              }
                        }
                  }
            addText(x, y, title, textL, 8.0, 0.0);
            addText(x, y - 4, s, textL, 6.0, 0.0);

            for (int row = 0; row < rows(); ++row)
                  addText(samples.right(), samples.top() + row * h + h * .5, genRowText(row), textL, 6.0,
                          0.0);
            for (int col = 0; col < columns(); ++col)
                  addText(samples.left() + col * w + w * .5, samples.top(), genColText(col), textL, 6.0,
                          -90.0);

            auto colLabel    = label(ParameterType(columnParameter()));
            double textWidth = 15; // TODO: text width of colLabel
            addText(samples.left() + samples.width() * .5 - textWidth * .5, samples.top() - 9, colLabel,
                    textL, 6.0, 0.0);
            // textWidth
            addText(samples.right() + 9, samples.top() + samples.height() * .5 - textWidth * .5,
                    label(ParameterType(rowParameter())), textL, 6.0, 90.0);

            LaserLayer* ll = new LaserLayer(zcam, fixture);
            ll->setName("ll-text");
            ll->set_baseElement(textL);
            ll->set_recipe(textLayer());
            fixture->addChild(ll);
            }

      // Refresh CAD display: tell QML to add Model nodes for each
      // newly created element, update tree model, layer visibility,
      // and cam data.
      // We use emit add3dElement() for each new child (and its
      // sub-children) so ProjectTree.qml synchronously creates
      // Model nodes bound to the new C++ elements.
      for (Element* c : children()) {
            auto* e3d = qobject_cast<Element3d*>(c);
            if (e3d) {
                  emit zcam->add3dElement(e3d);
                  for (Element* sc : c->children()) {
                        auto* se3d = qobject_cast<Element3d*>(sc);
                        if (se3d)
                              emit zcam->addSubElement(se3d, e3d);
                        }
                  }
            }
      // Also emit add3dElement for new fixture LaserLayers
      if (fixture) {
            for (Element* c : fixture->children()) {
                  auto* ll = qobject_cast<LaserLayer*>(c);
                  if (ll && ll->baseElement() && ll->baseElement()->parent() == this)
                        emit zcam->add3dElement(ll);
                  }
            }
      zcam->treeModel()->resetModel();
      if (zcam->project())
            zcam->project()->updateCadLayerVisibility();
      zcam->setCamDirty(true);
      }

//---------------------------------------------------------
//   addText
//---------------------------------------------------------

void MaterialTest::addText(double x, double y, const QString& s, Layer* layer, double pt, double rot) {
      auto r = new Text(zcam, layer);
      r->setName("text");
      r->set_text(s);
      r->set_pointSize(pt);
      //      r->setWeight(800);
      r->set_fontFamily("Noto Sans");
      r->set_pos(QVector3D(x, y, 0));
      r->set_rot(QVector3D(0, 0, rot));
      r->update();
      layer->addChild(r);
      }

//---------------------------------------------------------
//   rowValue
//---------------------------------------------------------

double MaterialTest::rowValue(int row) const {
      switch (ParameterType(rowParameter())) {
            case ParameterType::Pulse: {
                  int val                        = (rowMax() - rowMin()) / (rows() - 1) * row + rowMin();
                  const std::vector<Pulse33>& pt = zcam->laser()->engine()->pulseTable();
                  for (int i = 0; i < sizeof(pt); ++i) {
                        double tp = pt[i].pulseWidth;
                        if (val < tp) {
                              if (i == 0)
                                    return tp;
                              if ((tp - val) < (pt[i - 1].pulseWidth) - val)
                                    return tp;
                              else
                                    return pt[i - 1].pulseWidth;
                              }
                        }
                  return pt[sizeof(pt) - 1].pulseWidth;
                  }
            default: return (rowMax() - rowMin()) / (rows() - 1) * row + rowMin();
            }
      }

//---------------------------------------------------------
//   columnValue
//---------------------------------------------------------

double MaterialTest::columnValue(int col) const {
      switch (ParameterType(columnParameter())) {
            case ParameterType::Pulse: {
                  int val = (columnMax() - columnMin()) / (columns() - 1) * col + columnMin();
                  const std::vector<Pulse33>& pt = zcam->laser()->engine()->pulseTable();
                  for (int i = 0; i < sizeof(pt); ++i) {
                        double tp = pt[i].pulseWidth;
                        if (val < tp) {
                              if (i == 0)
                                    return tp;
                              if ((tp - val) < (pt[i - 1].pulseWidth) - val)
                                    return tp;
                              else
                                    return pt[i - 1].pulseWidth;
                              }
                        }
                  return pt[sizeof(pt) - 1].pulseWidth;
                  }
            default: return (columnMax() - columnMin()) / (columns() - 1) * col + columnMin();
            }
      }

//---------------------------------------------------------
//   createGalvoTest
//    Create a new project with a galvo calibration test pattern.
//    The pattern consists of a crosshair of horizontal and vertical
//    lines, a border rectangle, and a set of concentric squares
//    to verify galvo alignment and linearity.
//---------------------------------------------------------

void ZCam::createGalvoTest() {

      Project* top = createFiberLaserProject(this);
      if (!top)
            return;
      top->setName("Galvo-Test");

      Cad* cad         = top->cad();
      Cam* cam         = top->cam();
      Fixture* fixture = top->fixture();
      if (!cad || !cam || !fixture)
            return;

      cad->setExpanded(true);
      cam->setExpanded(true);
      fixture->setExpanded(false);

      // Create a single layer for the galvo test pattern
      auto layer = new Layer(this, cad);
      layer->setName("GalvoPattern");
      layer->setExpanded(true);
      cad->addChild(layer);

      // Border rectangle — 100×100 mm, centered at origin
      auto border = new Rectangle(this, layer);
      border->setName("border");
      border->set_size(QVector2D(100.0, 100.0));
      border->set_pos(QVector3D(-50.0, -50.0, 0.0));
      border->set_fill(false);
      border->set_lineWidth(0.2);
      border->setColor(QColor("yellow"));
      border->update();
      layer->addChild(border);

      // Crosshair: horizontal line and vertical line through center
      // Each line is a thin rectangle
      auto hLine = new Rectangle(this, layer);
      hLine->setName("hLine");
      hLine->set_size(QVector2D(100.0, 0.5));
      hLine->set_pos(QVector3D(-50.0, -0.25, 0.0));
      hLine->set_fill(true);
      hLine->setColor(QColor("cyan"));
      hLine->update();
      layer->addChild(hLine);

      auto vLine = new Rectangle(this, layer);
      vLine->setName("vLine");
      vLine->set_size(QVector2D(0.5, 100.0));
      vLine->set_pos(QVector3D(-0.25, -50.0, 0.0));
      vLine->set_fill(true);
      vLine->setColor(QColor("cyan"));
      vLine->update();
      layer->addChild(vLine);

      // Concentric squares for linearity check
      const double squares[] = {20.0, 40.0, 60.0, 80.0};
      for (double sz : squares) {
            auto sq = new Rectangle(this, layer);
            sq->setName(format("square-{}", int(sz)).c_str());
            sq->set_size(QVector2D(sz, sz));
            sq->set_pos(QVector3D(-sz * 0.5, -sz * 0.5, 0.0));
            sq->set_fill(false);
            sq->set_lineWidth(0.1);
            sq->setColor(QColor("green"));
            sq->update();
            layer->addChild(sq);
            }

      // Diagonal lines (two thin rectangles at 45°)
      // Diagonal from top-left to bottom-right: rotate -45° around center
      auto diag1 = new Rectangle(this, layer);
      diag1->setName("diag1");
      diag1->set_size(QVector2D(141.42, 0.3)); // sqrt(100^2 + 100^2)
      diag1->set_pos(QVector3D(-70.71, -0.15, 0.0));
      diag1->set_rot(QVector3D(0, 0, -45.0));
      diag1->set_fill(true);
      diag1->setColor(QColor("magenta"));
      diag1->update();
      layer->addChild(diag1);

      auto diag2 = new Rectangle(this, layer);
      diag2->setName("diag2");
      diag2->set_size(QVector2D(141.42, 0.3));
      diag2->set_pos(QVector3D(-70.71, -0.15, 0.0));
      diag2->set_rot(QVector3D(0, 0, 45.0));
      diag2->set_fill(true);
      diag2->setColor(QColor("magenta"));
      diag2->update();
      layer->addChild(diag2);

      // Label text
      auto label = new Text(this, layer);
      label->setName("label");
      label->set_text("Galvo Test");
      label->set_pointSize(8.0);
      label->set_fontFamily("Noto Sans");
      label->set_pos(QVector3D(-50.0, 52.0, 0.0));
      label->setColor(QColor("yellow"));
      label->update();
      layer->addChild(label);

      // Create a LaserLayer linked to the galvo pattern layer
      auto ll = new LaserLayer(this, fixture);
      ll->setName("LL-GalvoPattern");
      ll->set_baseElement(layer);
      auto recipes = this->recipes();
      if (recipes && recipes->recipeCount() > 0)
            ll->set_recipe(recipes->recipePtr(0));
      fixture->addChild(ll);

      finalizeProject(this);
      _projectManager->markDirty();
      }

//---------------------------------------------------------
//   fixup
//    this is called after loading the project
//---------------------------------------------------------

void MaterialTest::fixup() {
      createChildren();
      zcam->project()->fixture()->_saveChildren = false;
      _saveChildren = false;
      }
