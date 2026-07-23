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

#include "undo.h"
#include "element.h"
#include "logger.h"
#include "zcam.h"

//---------------------------------------------------------
//   UndoCommand
//---------------------------------------------------------

UndoCommand::~UndoCommand() {
      for (auto c : childList)
            delete c;
      }

//---------------------------------------------------------
//   UndoCommand::cleanup
//---------------------------------------------------------

void UndoCommand::cleanup(bool undo) {
      for (auto c : childList)
            c->cleanup(undo);
      }

//---------------------------------------------------------
//   undo
//---------------------------------------------------------

void UndoCommand::undo() {
//      Debug("==");
      int n = childList.size();
      for (int i = n - 1; i >= 0; --i) {
//            Debug("{}", childList[i]->description());
            childList[i]->undo();
            }
      flip();
      }

//---------------------------------------------------------
//   redo
//---------------------------------------------------------

void UndoCommand::redo() {
//      Debug("==");
      int n = childList.size();
      for (int i = 0; i < n; ++i) {
//            Debug("{}", childList[i]->description());
            childList[i]->redo();
            }
      flip();
      }

//---------------------------------------------------------
//   unwind
//---------------------------------------------------------

void UndoCommand::unwind() {
      while (!childList.isEmpty()) {
            UndoCommand* c = childList.takeLast();
            c->undo();
            delete c;
            }
      }

//---------------------------------------------------------
//   UndoStack
//---------------------------------------------------------

UndoStack::~UndoStack() {
      delete curCmd;
      // Items in macroStack are already owned by their parent macros
      // (or are the top-level curCmd). They will be deleted when the
      // list is cleared below, or were already deleted as children.
      macroStack.clear();
      int idx = 0;
      for (auto c : list)
            c->cleanup(idx++ < curIdx);
      qDeleteAll(list);
      }

//---------------------------------------------------------
//   beginMacro
//---------------------------------------------------------

void UndoStack::beginMacro() {
      if (inUndoRedo) {
            Critical("processing undo/redo");
            return;
            }
      // Support nesting: if a macro is already active, push the current
      // command onto the macro stack so the new macro becomes a child.
      if (curCmd) {
            Fatal("already active");
            return;
            }
      curCmd = new UndoCommand(zcam);
      }

//-------------------------------------------------------------------
//   endMacro
//    the undo macro will not be recorded if rollback is true
//-------------------------------------------------------------------

void UndoStack::endMacro(bool rollback) {
      if (inUndoRedo) {
            Critical("processing undo/redo");
            return;
            }
      if (curCmd == 0) {
            Fatal("not active");
            return;
            }

      if (rollback || curCmd->childCount() == 0)
            delete curCmd;
      else {
            // remove redo stack
            while (list.size() > curIdx) {
                  UndoCommand* cmd = list.takeLast();
                  cmd->cleanup(false); // delete elements for which UndoCommand() holds ownership
                  delete cmd;
                  }
            list.append(curCmd);
            ++curIdx;
            }
      curCmd = nullptr;
      emit undoChanged();
      emit dirtyChanged();
      }

//---------------------------------------------------------
//   push
//---------------------------------------------------------

void UndoStack::push(UndoCommand* cmd) {
      Debug("{}: <{}>", inUndoRedo, cmd->description());
      if (_active) // do not record command if not active
            push1(cmd);
      cmd->redo(); // execute command
      }

//---------------------------------------------------------
//   push1
//---------------------------------------------------------

void UndoStack::push1(UndoCommand* cmd) {
      if (inUndoRedo) {
            Critical("processing undo/redo");
            return;
            }
      if (!curCmd)
            Fatal("no active command");
      else
            curCmd->appendChild(cmd);
      }

//---------------------------------------------------------
//   pop
//---------------------------------------------------------

void UndoStack::pop() {
      if (!curCmd) {
            Debug("no active command");
            }
      else {
            UndoCommand* cmd = curCmd->removeChild();
            cmd->undo();
            }
      }

//---------------------------------------------------------
//   undo
//---------------------------------------------------------

void UndoStack::undo() {
      inUndoRedo = true;
      if (canUndo())
            list[--curIdx]->undo();
      else
            Critical("cannot");
      inUndoRedo = false;
      emit undoChanged();
      emit dirtyChanged();
      }

//---------------------------------------------------------
//   redo
//---------------------------------------------------------

void UndoStack::redo() {
      inUndoRedo = true;
      if (canRedo())
            list[curIdx++]->redo();
      else
            Critical("cannot");
      inUndoRedo = false;
      emit undoChanged();
      emit dirtyChanged();
      }

//---------------------------------------------------------
//   reset
//---------------------------------------------------------

void UndoStack::reset() {
      delete curCmd;
      curCmd = nullptr;
      macroStack.clear();
      qDeleteAll(list);
      list.clear();
      curIdx = cleanIdx = 0;
      _active           = true;
      emit undoChanged();
      emit dirtyChanged();
      }
