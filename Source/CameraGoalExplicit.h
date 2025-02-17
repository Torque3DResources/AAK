#pragma once
#include "AAKplayer.h"
#include "T3D/shapeBase.h"

class CameraGoalExplicit : public ShapeBase
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
      TargetMask = Parent::NextFreeMask,
      MoveMask = Parent::NextFreeMask << 1,

      NextFreeMask = Parent::NextFreeMask << 2
   };

   Point3F mRot;
   Point3F mPosition;

   void setPosition(const Point3F& pos, const Point3F& rot);
   void setRenderPosition(const Point3F& pos, const Point3F& rot);

   Point3F mTargetPosition;
   VectorF mTargetVector;

public:
   DECLARE_CONOBJECT(CameraGoalExplicit);

   CameraGoalExplicit();
   ~CameraGoalExplicit();

   F32 getUpdatePriority(CameraScopeQuery* focusObject, U32 updateMask, S32 updateSkips) override;

   void setTargetPosition(const Point3F& targetPos);
   void setTargetVector(const Point3F& targetVec);

   void processTick(const Move*) override;
   void interpolateTick(F32 dt) override;
   void getCameraTransform(F32* pos, MatrixF* mat) override;
   U32  packUpdate(NetConnection*, U32 mask, BitStream* stream) override;
   void unpackUpdate(NetConnection*, BitStream* stream) override;
};
