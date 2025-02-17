#pragma once

#ifndef _SHAPEBASE_H_
#include "T3D/shapeBase.h"
#endif

#ifndef _AAK_PLAYER_H_
#include "./AAKplayer.h"
#endif

#ifndef _SIMPATH_H_
#include "scene/simPath.h"
#endif

//----------------------------------------------------------------------------
// CameraGoalTarget
//
// This camera goal provides a view pointed towards a specific target, while 
// keeping both it and the player in view
//----------------------------------------------------------------------------
class CameraGoalTarget : public ShapeBase
{
private:
   typedef ShapeBase Parent;

   struct StateDelta {
      Point3F pos;
      Point3F rot;
      VectorF posVec;
      VectorF rotVec;
   };
   StateDelta mDelta;

   enum MaskBits {
      PlayerMask = Parent::NextFreeMask,
      TargetMask = Parent::NextFreeMask + 1,
      MoveMask = Parent::NextFreeMask + 2,
      SettingsMask = Parent::NextFreeMask + 3,
      NextFreeMask = Parent::NextFreeMask << 1
   };

   Point3F mRot;
   Point3F mPosition;

   void setPosition(const Point3F& pos, const Point3F& rot);
   void setRenderPosition(const Point3F& pos, const Point3F& rot);

   AAKPlayer* mPlayerObject;

   Point3F mTargetPosition;
   SimObjectPtr<SceneObject> mTargetObject;

   Point3F mCenterPosition;
   VectorF mCameraVector;

   F32 mPitch;
   F32 mYaw;
   F32 mRadius;

   F32 mPitchMin;
   F32 mPitchMax;
   
   F32 mTargetYaw;
   F32 mTargetPitch;
   F32 mDistanceOffset;
   F32 mTargetOffsetDistance;

public:
   DECLARE_CONOBJECT(CameraGoalTarget);

   CameraGoalTarget();
   ~CameraGoalTarget();

   F32 getUpdatePriority(CameraScopeQuery* focusObject, U32 updateMask, S32 updateSkips);

   bool setPlayerObject(AAKPlayer* obj);
   bool setTargetObject(SceneObject* obj);
   void setTargetPosition(const Point3F& targetPos);

   void setTargetYaw(const F32& yaw);
   void setTargetPitch(const F32& pitch);
   void setDistanceOffset(const F32& dist);
   void setTargetOffsetDistance(const F32& dist);

   void processTick(const Move*);
   void interpolateTick(F32 dt);
   void getCameraTransform(F32* pos, MatrixF* mat);
   U32  packUpdate(NetConnection*, U32 mask, BitStream* stream);
   void unpackUpdate(NetConnection*, BitStream* stream);
   void onDeleteNotify(SimObject* obj);
   DECLARE_CALLBACK(void, targetLost, ());
};
