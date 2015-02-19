/*===================================================================

BlueBerry Platform

Copyright (c) German Cancer Research Center,
Division of Medical and Biological Informatics.
All rights reserved.

This software is distributed WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.

See LICENSE.txt or http://www.mitk.org for details.

===================================================================*/

#include "tweaklets/berryGuiWidgetsTweaklet.h"

#include "berryDragUtil.h"

#include "berryGeometry.h"
#include "berryQtTracker.h"

namespace berry
{

const QString DragUtil::DROP_TARGET_ID =
    "org.blueberry.ui.internal.dropTarget";

TestDropLocation::Pointer DragUtil::forcedDropTarget(0);

QList<IDragOverListener*> DragUtil::defaultTargets = QList<IDragOverListener*>();

DragUtil::TrackerMoveListener::TrackerMoveListener(Object::Pointer draggedItem,
    const QRect& sourceBounds, const QPoint& initialLocation,
    bool allowSnapping) :
  allowSnapping(allowSnapping), draggedItem(draggedItem), sourceBounds(
      sourceBounds), initialLocation(initialLocation)
{

}

GuiTk::IControlListener::Events::Types DragUtil::TrackerMoveListener::GetEventTypes() const
{
  return Events::MOVED;
}

void DragUtil::TrackerMoveListener::ControlMoved(
    GuiTk::ControlEvent::Pointer event)
{

  // Get the curslor location as a point
  QPoint location(event->x, event->y);

  // Select a drop target; use the global one by default
  IDropTarget::Pointer target;

  void* targetControl =
      Tweaklets::Get(GuiWidgetsTweaklet::KEY)->GetCursorControl();

  // Get the QtTracker which fired the event
  QtTracker* tracker = static_cast<QtTracker*> (event->item);

  // Get the drop target for this location
  target = DragUtil::GetDropTarget(targetControl, draggedItem, location,
      tracker->GetRectangle());

  // Set up the tracker feedback based on the target
  QRect snapTarget;
  if (target != 0)
  {
    snapTarget = target->GetSnapRectangle();

    tracker->SetCursor(target->GetCursor());
  }
  else
  {
    tracker->SetCursor(CURSOR_INVALID);
  }

  // If snapping then reset the tracker's rectangle based on the current drop target
  if (allowSnapping)
  {
    if (snapTarget.width() < 0 || snapTarget.height() < 0)
    {
      snapTarget = QRect(sourceBounds.x() + location.x() - initialLocation.x(),
          sourceBounds.y() + location.y() - initialLocation.y(), sourceBounds.width(),
          sourceBounds.height());
    }

    // Try to prevent flicker: don't change the rectangles if they're already in
    // the right location
    QRect currentRectangle = tracker->GetRectangle();

    if (!(currentRectangle == snapTarget))
    {
      tracker->SetRectangle(snapTarget);
    }
  }

}

DragUtil::TargetListType::Pointer DragUtil::GetTargetList(void* control)
{
  Object::Pointer data = Tweaklets::Get(GuiWidgetsTweaklet::KEY)->GetData(
      control, DROP_TARGET_ID);
  TargetListType::Pointer list = data.Cast<TargetListType> ();
  return list;
}

IDropTarget::Pointer DragUtil::GetDropTarget(const QList<IDragOverListener*>& toSearch,
                                             void* mostSpecificControl,
                                             Object::Pointer draggedObject,
                                             const QPoint& position,
                                             const QRect& dragRectangle)
{

  for (QList<IDragOverListener*>::const_iterator iter =
      toSearch.begin(); iter != toSearch.end(); ++iter)
  {
    IDragOverListener* next = *iter;

    IDropTarget::Pointer dropTarget = next->Drag(mostSpecificControl,
        draggedObject, position, dragRectangle);

    if (dropTarget != 0)
    {
      return dropTarget;
    }
  }

  return IDropTarget::Pointer(0);
}

void DragUtil::AddDragTarget(void* control, IDragOverListener* target)
{
  if (control == 0)
  {
    defaultTargets.push_back(target);
  }
  else
  {
    TargetListType::Pointer targetList = GetTargetList(control);

    if (targetList == 0)
    {
      targetList = new TargetListType();
      Tweaklets::Get(GuiWidgetsTweaklet::KEY)->SetData(control, DROP_TARGET_ID,
          targetList);
    }
    targetList->push_back(target);
  }
}

void DragUtil::RemoveDragTarget(void* control, IDragOverListener* target)
{
  if (control == 0)
  {
    defaultTargets.removeAll(target);
  }
  else
  {
    TargetListType::Pointer targetList = GetTargetList(control);
    if (targetList != 0)
    {
      targetList->removeAll(target);
      if (targetList->empty())
      {
        Tweaklets::Get(GuiWidgetsTweaklet::KEY)->SetData(control,
            DROP_TARGET_ID, Object::Pointer(0));
      }
    }
  }
}

QRect DragUtil::GetDisplayBounds(void* boundsControl)
{
  void* parent = Tweaklets::Get(GuiWidgetsTweaklet::KEY)->GetParent(
      boundsControl);
  if (parent == 0)
  {
    return Tweaklets::Get(GuiWidgetsTweaklet::KEY)->GetBounds(boundsControl);
  }

  QRect rect = Tweaklets::Get(GuiWidgetsTweaklet::KEY)->GetBounds(
      boundsControl);
  return Geometry::ToDisplay(parent, rect);
}

bool DragUtil::PerformDrag(Object::Pointer draggedItem,
    const QRect& sourceBounds, const QPoint& initialLocation,
    bool allowSnapping)
{

  IDropTarget::Pointer target = DragToTarget(draggedItem, sourceBounds,
      initialLocation, allowSnapping);

  if (target == 0)
  {
    return false;
  }

  target->Drop();
  target->DragFinished(true);

  return true;
}

void DragUtil::ForceDropLocation(TestDropLocation::Pointer forcedLocation)
{
  forcedDropTarget = forcedLocation;
}

IDropTarget::Pointer DragUtil::DragToTarget(Object::Pointer draggedItem,
    const QRect& sourceBounds, const QPoint& initialLocation,
    bool allowSnapping)
{
  //final Display display = Display.getCurrent();

  // Testing...immediately 'drop' onto the test target
  if (forcedDropTarget != 0)
  {
    QPoint location = forcedDropTarget->GetLocation();

    void* currentControl =
        Tweaklets::Get(GuiWidgetsTweaklet::KEY)->FindControl(
            forcedDropTarget->GetShells(), location);
    return GetDropTarget(currentControl, draggedItem, location, sourceBounds);
  }

  // Create a tracker.  This is just an XOR rect on the screen.
  // As it moves we notify the drag listeners.
  QtTracker tracker;
  //tracker.setStippled(true);

  GuiTk::IControlListener::Pointer trackerListener(new TrackerMoveListener(
      draggedItem, sourceBounds, initialLocation, allowSnapping));

  tracker.AddControlListener(trackerListener);

  // Setup...when the drag starts we might already be over a valid target, check this...
  // If there is a 'global' target then skip the check
  IDropTarget::Pointer target;
  void* startControl =
      Tweaklets::Get(GuiWidgetsTweaklet::KEY)->GetCursorControl();

  if (startControl != 0 && allowSnapping)
  {
    target = GetDropTarget(startControl, draggedItem, initialLocation,
        sourceBounds);
  }

  // Set up an initial tracker rectangle
  QRect startRect = sourceBounds;
  if (target != 0)
  {
    QRect rect = target->GetSnapRectangle();

    if (rect.width() != 0 && rect.height() != 0)
    {
      startRect = rect;
    }

    tracker.SetCursor(target->GetCursor());
  }

  if (startRect.width() != 0 && startRect.height() != 0)
  {
    tracker.SetRectangle(startRect);
  }

  // Tracking Loop...tracking is preformed on the 'SWT.Move' listener registered
  // against the tracker.

  //  // HACK:
  //  // Some control needs to capture the mouse during the drag or other
  //  // controls will interfere with the cursor
  //  Shell shell = PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell();
  //  if (shell != null)
  //  {
  //    shell.setCapture(true);
  //  }

  // Run tracker until mouse up occurs or escape key pressed.
  bool trackingOk = tracker.Open();

  //  // HACK:
  //  // Release the mouse now
  //  if (shell != null)
  //  {
  //    shell.setCapture(false);
  //  }

  // Done tracking...

  // Get the current drop target
  IDropTarget::Pointer dropTarget;
  QPoint finalLocation =
      Tweaklets::Get(GuiWidgetsTweaklet::KEY)->GetCursorLocation();
  void* targetControl =
      Tweaklets::Get(GuiWidgetsTweaklet::KEY)->GetCursorControl();
  dropTarget = GetDropTarget(targetControl, draggedItem, finalLocation,
      tracker.GetRectangle());

  // Cleanup...
  //delete tracker;

  // if we're going to perform a 'drop' then delay the issuing of the 'finished'
  // callback until after it's done...
  if (trackingOk)
  {
    return dropTarget;
  }
  else if (dropTarget != 0)
  {
    // If the target can handle a 'finished' notification then send one
    dropTarget->DragFinished(false);
  }

  return IDropTarget::Pointer(0);
}

IDropTarget::Pointer DragUtil::GetDropTarget(void* toSearch,
    Object::Pointer draggedObject, const QPoint& position,
    const QRect& dragRectangle)
{
  // Search for a listener by walking the control's parent hierarchy
  for (void* current = toSearch; current != 0; current = Tweaklets::Get(
      GuiWidgetsTweaklet::KEY)->GetParent(current))
  {
    TargetListType::Pointer targetList = GetTargetList(current);
    QList<IDragOverListener*> targets;
    if (targetList != 0)
      targets = *targetList;

    IDropTarget::Pointer dropTarget = GetDropTarget(targets, toSearch,
        draggedObject, position, dragRectangle);

    if (dropTarget != 0)
    {
      return dropTarget;
    }

    //          // Don't look to parent shells for drop targets
    //          if (current instanceof Shell) {
    //              break;
    //          }
  }

  // No controls could handle this event -- check for default targets
  return GetDropTarget(defaultTargets, toSearch, draggedObject, position,
      dragRectangle);
}

//QPoint DragUtil::GetEventLoc(GuiTk::ControlEvent::Pointer event)
//{
//  Control ctrl = (Control) event.widget;
//  return ctrl.toDisplay(new QPoint(event.x, event.y));
//}

}
