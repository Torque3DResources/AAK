#ifndef CAMERA_TRIGGER_H
#define CAMERA_TRIGGER_H

#ifndef _GAMEBASE_H_
#include "T3D/gameBase/gameBase.h"
#endif
#ifndef _MBOX_H_
#include "math/mBox.h"
#endif
#ifndef _EARLYOUTPOLYLIST_H_
#include "collision/earlyOutPolyList.h"
#endif
#ifndef _MPOLYHEDRON_H_
#include "math/mPolyhedron.h"
#endif

#ifndef _MISSIONMARKER_H_
#include "T3D/missionMarker.h"
#endif

#ifndef _H_TRIGGER
#include "T3D/trigger.h"
#endif


class CameraTrigger : public Trigger
{
   typedef Trigger Parent;
public:
   F32 mTime = 1500;
   F32 mForcedPitch = 45;
   F32 mForcedRadius = 5;
   F32 mForcedYaw = 0;
   MissionMarker* mOrientBase = NULL;

   const char* mObjectLinkId = NULL;
   bool mSync = false;

   CameraTrigger() {};
   ~CameraTrigger() {};

   static void initPersistFields();
   static bool _setFieldLink(void* object, const char* index, const char* data);
   static bool _sync(void* object, const char* index, const char* data);

   void syncOrientation();

   DECLARE_CONOBJECT(CameraTrigger);
};
#endif // _H_TRIGGER

