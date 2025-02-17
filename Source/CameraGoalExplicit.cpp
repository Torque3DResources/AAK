#include "CameraGoalExplicit.h"
#include "scene/pathManager.h"
#include "core/stream/bitStream.h"
#include "T3D/gameBase/gameConnection.h"
#include "gfx/sim/debugDraw.h"
#include "math/mathUtils.h"
#include "math/mathIO.h"
#include "AAKUtils.h"

IMPLEMENT_CO_NETOBJECT_V1(CameraGoalExplicit);

CameraGoalExplicit::CameraGoalExplicit()
{
   mNetFlags.clear(Ghostable);
   mTypeMask |= CameraObjectType;

   mPosition.set(0.0f, 0.0f, 0.0f);
   mRot.set(0.0f, 0.0f, 0.0f);

   mTargetPosition = Point3F::Zero;
   mTargetVector = VectorF::Zero;

}

CameraGoalExplicit::~CameraGoalExplicit()
{
}

F32 CameraGoalExplicit::getUpdatePriority(CameraScopeQuery* camInfo, U32 updateMask, S32 updateSkips)
{
   //we need a higher priority because
   //cameraGoalFollower depends on us
   return 5.0f;
}

void CameraGoalExplicit::processTick(const Move*)
{
   mPosition = mTargetPosition;

   F32 yaw, pitch;
   MathUtils::getAnglesFromVector(mTargetVector, yaw, pitch);

   mRot.x = pitch;
   mRot.z = yaw;

   if (isClientObject())
   {
      mDelta.pos = mPosition;
      mDelta.rot = mRot;
      mDelta.posVec = mDelta.posVec - mDelta.pos;
      mDelta.rotVec = mDelta.rotVec - mDelta.rot;
   }

   /*#ifdef ENABLE_DEBUGDRAW
      Point3F debugCameraPos, debugPlayerPos; QuatF dummy;

      DebugDrawer::get()->drawLine(mPosition, mPosition + (mTargetVector * 10), ColorI::BLUE);
      DebugDrawer::get()->setLastTTL(TickMs);
      DebugDrawer::get()->drawBox(mPosition + Point3F(-0.5, -0.5, -0.5), mPosition + Point3F(0.5, 0.5, 0.5), ColorI::RED);
      DebugDrawer::get()->setLastTTL(TickMs);
   #endif*/

   setPosition(mPosition, mRot);
}

void CameraGoalExplicit::getCameraTransform(F32* pos, MatrixF* mat)
{
   getRenderEyeTransform(mat);
}

void CameraGoalExplicit::interpolateTick(F32 dt)
{
   Parent::interpolateTick(dt);
   Point3F rot = mDelta.rot + mDelta.rotVec * dt;
   Point3F pos = mDelta.pos + mDelta.posVec * dt;
   setRenderPosition(pos, rot);
}

void CameraGoalExplicit::setPosition(const Point3F& pos, const Point3F& rot)
{
   MatrixF xRot, zRot;
   xRot.set(EulerF(rot.x, 0.0f, 0.0f));
   zRot.set(EulerF(0.0f, 0.0f, rot.z));

   MatrixF temp;
   temp.mul(zRot, xRot);
   temp.setColumn(3, pos);
   Parent::setTransform(temp);

   mPosition = pos;
   mRot = rot;

   setMaskBits(MoveMask);
}

void CameraGoalExplicit::setRenderPosition(const Point3F& pos, const Point3F& rot)
{
   MatrixF xRot, zRot;
   xRot.set(EulerF(rot.x, 0, 0));
   zRot.set(EulerF(0, 0, rot.z));
   MatrixF temp;
   temp.mul(zRot, xRot);
   temp.setColumn(3, pos);
   Parent::setRenderTransform(temp);
}

U32 CameraGoalExplicit::packUpdate(NetConnection* con, U32 mask, BitStream* stream)
{
   U32 retMask = Parent::packUpdate(con, mask, stream);

   if (stream->writeFlag(mask & TargetMask))
   {
      mathWrite(*stream, mTargetPosition);
      mathWrite(*stream, mTargetVector);
   }

   if (stream->writeFlag(mask & MoveMask))
   {
      mathWrite(*stream, mPosition);
      mathWrite(*stream, mRot);
   }

   return retMask;
}

void CameraGoalExplicit::unpackUpdate(NetConnection* con, BitStream* stream)
{
   Parent::unpackUpdate(con, stream);

   //TargetMask
   if (stream->readFlag())
   {
      mathRead(*stream, &mTargetPosition);
      mathRead(*stream, &mTargetVector);
   }

   //MoveMask
   if (stream->readFlag())
   {
      mathRead(*stream, &mPosition);
      mathRead(*stream, &mRot);

      setPosition(mPosition, mRot);
      mDelta.pos = mPosition;
      mDelta.rot = mRot;
      mDelta.rotVec.set(0.0f, 0.0f, 0.0f);
      mDelta.posVec.set(0.0f, 0.0f, 0.0f);
   }
}

//===============================================================================

void CameraGoalExplicit::setTargetPosition(const Point3F& targetPos)
{
   mTargetPosition = targetPos;
   setMaskBits(TargetMask);
}

DefineEngineMethod(CameraGoalExplicit, setTargetPosition, void, (Point3F targetPos), (Point3F::Zero), "(Point3F targetPos)")
{
   object->setTargetPosition(targetPos);
}

void CameraGoalExplicit::setTargetVector(const Point3F& targetVec)
{
   mTargetVector = targetVec;
   mTargetVector.normalizeSafe();
   setMaskBits(TargetMask);
}

DefineEngineMethod(CameraGoalExplicit, setTargetVector, void, (Point3F targetVec), (Point3F::Zero), "(Point3F targetVec)")
{
   object->setTargetVector(targetVec);
}
