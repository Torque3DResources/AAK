#include "cameraGoalTarget.h"
#include "scene/pathManager.h"
#include "core/stream/bitStream.h"
#include "T3D/gameBase/gameConnection.h"
#include "gfx/sim/debugDraw.h"
#include "math/mathUtils.h"
#include "math/mathIO.h"
#include "AAKUtils.h"

IMPLEMENT_CO_NETOBJECT_V1(CameraGoalTarget);

IMPLEMENT_CALLBACK(CameraGoalTarget, targetLost, void, (), (), "");

CameraGoalTarget::CameraGoalTarget()
{
   mNetFlags.clear(Ghostable);
   mTypeMask |= CameraObjectType;

   mPosition.set(0.0f, 0.0f, 0.0f);
   mRot.set(0.0f, 0.0f, 0.0f);

   mPlayerObject = NULL;
   mTargetObject = NULL;
   mTargetPosition = Point3F::Zero;

   mPitch = 0;
   mYaw = 0;
   mRadius = 10;

   mPitchMin = -90;
   mPitchMax = 90;

   mTargetYaw = 0;
   mTargetPitch = 0;
   mDistanceOffset = 0;
   mTargetOffsetDistance = 0.5;
}

CameraGoalTarget::~CameraGoalTarget()
{
}

F32 CameraGoalTarget::getUpdatePriority(CameraScopeQuery* camInfo, U32 updateMask, S32 updateSkips)
{
   //we need a higher priority because
   //cameraGoalFollower depends on us
   return 5.0f;
}

void CameraGoalTarget::processTick(const Move*)
{
   if (!mPlayerObject)
      return;

   Point3F playerPos = mPlayerObject->getNodePosition("Cam");

   Point3F targetPos = mTargetPosition;
   if (mTargetObject.getPointer())
      targetPos = mTargetObject->getPosition();      

   //First we need to get the center position that the camera 
   mCenterPosition.interpolate(playerPos, targetPos, mTargetOffsetDistance);
   mCameraVector = playerPos - mCenterPosition;

   //To get the zoom distance, we first figure the distance from player to the target
   F32 dist = mCameraVector.len();
   dist += mDistanceOffset;

   mRadius = dist;

   //Re-zero based on our mainline vector between the player and target
   AAKUtils::getAnglesFromVector(mCameraVector, mYaw, mPitch);

   VectorF offsetCamVec;
   MathUtils::getVectorFromAngles(offsetCamVec, mYaw + mDegToRad(mTargetYaw), mPitch + mDegToRad(mTargetPitch));

   //maintain radius
   mCameraVector.normalize(mRadius);

   //maintain position
   mPosition = mCenterPosition + mCameraVector;

   mRot.x = mPitch + mDegToRad(mTargetPitch);
   mRot.z = mYaw + M_PI_F + mDegToRad(mTargetYaw);

   if (isClientObject())
   {
      mDelta.pos = mPosition;
      mDelta.rot = mRot;
      mDelta.posVec = mDelta.posVec - mDelta.pos;
      mDelta.rotVec = mDelta.rotVec - mDelta.rot;
   }

/*#ifdef ENABLE_DEBUGDRAW
   Point3F debugCameraPos, debugPlayerPos; QuatF dummy;
   mPathManager->getPathPosition(mCameraPathIndex, mT, debugCameraPos, dummy);
   mPathManager->getPathPosition(mPlayerPathIndex, mT, debugPlayerPos, dummy);
   DebugDrawer::get()->drawLine(playerPos, debugPlayerPos, ColorI::BLUE);
   DebugDrawer::get()->setLastTTL(TickMs);
   DebugDrawer::get()->drawLine(debugPlayerPos, debugCameraPos, ColorI::RED);
   DebugDrawer::get()->setLastTTL(TickMs);
#endif*/

   setPosition(mPosition, mRot);
}

void CameraGoalTarget::getCameraTransform(F32* pos, MatrixF* mat)
{
   getRenderEyeTransform(mat);
}

void CameraGoalTarget::interpolateTick(F32 dt)
{
   Parent::interpolateTick(dt);
   Point3F rot = mDelta.rot + mDelta.rotVec * dt;
   Point3F pos = mDelta.pos + mDelta.posVec * dt;
   setRenderPosition(pos, rot);
}

void CameraGoalTarget::setPosition(const Point3F& pos, const Point3F& rot)
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

void CameraGoalTarget::setRenderPosition(const Point3F& pos, const Point3F& rot)
{
   MatrixF xRot, zRot;
   xRot.set(EulerF(rot.x, 0, 0));
   zRot.set(EulerF(0, 0, rot.z));
   MatrixF temp;
   temp.mul(zRot, xRot);
   temp.setColumn(3, pos);
   Parent::setRenderTransform(temp);
}

void CameraGoalTarget::onDeleteNotify(SimObject* obj)
{
   Parent::onDeleteNotify(obj);

   if (obj == (SimObject*)mPlayerObject)
   {
      mPlayerObject = NULL;
   }
   else if (obj == (SimObject*)mTargetObject)
   {
      mTargetObject = NULL;
      targetLost_callback();
   }
}

U32 CameraGoalTarget::packUpdate(NetConnection* con, U32 mask, BitStream* stream)
{
   U32 retMask = Parent::packUpdate(con, mask, stream);

   if (stream->writeFlag((mask & PlayerMask) && mPlayerObject))
   {
      S32 id = con->getGhostIndex(mPlayerObject);
      stream->write(id);

      //hack to keep sending updates until player is ghosted
      if (id == -1)
         setMaskBits(PlayerMask);
   }

   if (stream->writeFlag(mask & TargetMask))
   {
      mathWrite(*stream, mTargetPosition);

      if (stream->writeFlag(mTargetObject))
      {
         S32 id = con->getGhostIndex(mTargetObject);
         stream->write(id);

         //hack to keep sending updates until player is ghosted
         if (id == -1)
            setMaskBits(TargetMask);
      }
   }

   if (stream->writeFlag(mask & MoveMask))
   {
      stream->write(mYaw);
      stream->write(mPitch);
      stream->write(mRadius);
      mathWrite(*stream, mPosition);
      mathWrite(*stream, mRot);
   }

   if (stream->writeFlag(mask & SettingsMask))
   {
      stream->write(mPitchMin);
      stream->write(mPitchMax);

      stream->write(mTargetYaw);
      stream->write(mTargetPitch);
      stream->write(mDistanceOffset);
      stream->write(mTargetOffsetDistance);
   }

   return retMask;
}

void CameraGoalTarget::unpackUpdate(NetConnection* con, BitStream* stream)
{
   Parent::unpackUpdate(con, stream);

   //PlayerMask
   if (stream->readFlag())
   {
      S32 id;
      stream->read(&id);
      if (id > 0)
      {
         AAKPlayer* playerObject = static_cast<AAKPlayer*>(con->resolveGhost(id));
         setPlayerObject(playerObject);
      }
   }

   //TargetMask
   if (stream->readFlag())
   {
      mathRead(*stream, &mTargetPosition);

      if (stream->readFlag())
      {
         S32 id;
         stream->read(&id);
         if (id > 0)
         {
            SceneObject* targetObject = static_cast<SceneObject*>(con->resolveGhost(id));
            setTargetObject(targetObject);
         }
      }
      else
      {
         //make sure we clear the targetObject if we don't have one set on the server
         setTargetObject(nullptr);
      }
   }

   //MoveMask
   if (stream->readFlag())
   {
      stream->read(&mYaw);
      stream->read(&mPitch);
      stream->read(&mRadius);
      mathRead(*stream, &mPosition);
      mathRead(*stream, &mRot);

      setPosition(mPosition, mRot);
      mDelta.pos = mPosition;
      mDelta.rot = mRot;
      mDelta.rotVec.set(0.0f, 0.0f, 0.0f);
      mDelta.posVec.set(0.0f, 0.0f, 0.0f);
   }

   //SettingsMask
   if (stream->readFlag())
   {
      stream->read(&mPitchMin);
      stream->read(&mPitchMax);

      stream->read(&mTargetYaw);
      stream->read(&mTargetPitch);
      stream->read(&mDistanceOffset);
      stream->read(&mTargetOffsetDistance);
   }
}

//===============================================================================

bool CameraGoalTarget::setPlayerObject(AAKPlayer* obj)
{
   if (!obj)
      return false;

   // reset current object if not null
   if (bool(mPlayerObject))
   {
      clearProcessAfter();
      clearNotify(mPlayerObject);
   }

   mPlayerObject = obj;
   setMaskBits(PlayerMask);

   if (bool(mPlayerObject))
   {
      processAfter(mPlayerObject);
      deleteNotify(mPlayerObject);
   }
   return true;
}

DefineEngineMethod(CameraGoalTarget, setPlayerObject, bool, (AAKPlayer* playerObj), (nullAsType<AAKPlayer*>()), "(AAKPlayer object)")
{
   if (playerObj == nullptr)
   {
      Con::errorf("CameraGoalTarget::setPlayerObject - failed to find object");
      return false;
   }

   return object->setPlayerObject(playerObj);
}

//===============================================================================

bool CameraGoalTarget::setTargetObject(SceneObject* obj)
{
   if (!obj)
      return false;

   // reset current object if not null
   if (bool(mTargetObject))
   {
      clearProcessAfter();
      clearNotify(mTargetObject);
   }

   mTargetObject = obj;
   setMaskBits(TargetMask);

   if (bool(mTargetObject))
   {
      processAfter(mTargetObject);
      deleteNotify(mTargetObject);
   }
   return true;
}

DefineEngineMethod(CameraGoalTarget, setTargetObject, bool, (SceneObject* targetObj), (nullAsType<SceneObject*>()), "(SceneObject object)")
{
   if (targetObj == nullptr)
   {
      Con::errorf("CameraGoalTarget::setTargetObject - failed to find object");
      return false;
   }

   return object->setTargetObject(targetObj);
}

//===============================================================================

void CameraGoalTarget::setTargetPosition(const Point3F& targetPos)
{
   mTargetPosition = targetPos;
   setMaskBits(TargetMask);
}

DefineEngineMethod(CameraGoalTarget, setTargetPosition, void, (Point3F targetPos), (Point3F::Zero), "(Point3F targetPos)")
{
   object->setTargetPosition(targetPos);
}

//===============================================================================

void CameraGoalTarget::setTargetYaw(const F32& yaw)
{
   mTargetYaw = yaw;
   setMaskBits(SettingsMask);
}

DefineEngineMethod(CameraGoalTarget, setTargetYaw, void, (F32 yaw), (0), "(F32 yaw)")
{
   object->setTargetYaw(yaw);
}
//===============================================================================

void CameraGoalTarget::setTargetPitch(const F32& pitch)
{
   mTargetPitch = pitch;
   setMaskBits(SettingsMask);
}

DefineEngineMethod(CameraGoalTarget, setTargetPitch, void, (F32 pitch), (0), "(F32 pitch)")
{
   object->setTargetPitch(pitch);
}
//===============================================================================

void CameraGoalTarget::setDistanceOffset(const F32& offset)
{
   mDistanceOffset = offset;
   setMaskBits(SettingsMask);
}

DefineEngineMethod(CameraGoalTarget, setDistanceOffset, void, (F32 distanceOffset), (0), "(F32 distanceOffset)")
{
   object->setDistanceOffset(distanceOffset);
}
//===============================================================================

void CameraGoalTarget::setTargetOffsetDistance(const F32& dist)
{
   mTargetOffsetDistance = dist;
   setMaskBits(SettingsMask);
}

DefineEngineMethod(CameraGoalTarget, setTargetOffsetDistance, void, (F32 targetOffsetDist), (0.5), "(F32 targetOffsetDist)")
{
   object->setTargetOffsetDistance(targetOffsetDist);
}
