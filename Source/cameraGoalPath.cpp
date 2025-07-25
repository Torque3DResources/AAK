//-----------------------------------------------------------------------------
// Copyright (C) 2008-2013 Ubiq Visuals, Inc. (http://www.ubiqvisuals.com/)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#include "cameraGoalPath.h"
#include "scene/pathManager.h"
#include "core/stream/bitStream.h"
#include "T3D/gameBase/gameConnection.h"
#include "gfx/sim/debugDraw.h"
#include "math/mathUtils.h"
#include "AAKUtils.h"

static bool sRenderCameraGoalPathRays = false;

IMPLEMENT_CO_NETOBJECT_V1(CameraGoalPath);

IMPLEMENT_CALLBACK(CameraGoalPath, onLeaveRange, void, (GameBase* obj), (obj),
   "@brief Called when an object leaves the maximum range of the CameraGoalPath.\n\n");

CameraGoalPath::CameraGoalPath()
{
	mNetFlags.clear(Ghostable);
	mTypeMask |= CameraObjectType;

	mPosition.set(0.0f, 0.0f, 0.0f);
	mRot.identity();

	mDelta.time = 0;
	mDelta.timeVec = 0;

	mT = 0;
    mPlayerPathIndex = SimPath::Path::NoPathIndex;
	mCameraPathIndex = SimPath::Path::NoPathIndex;
	mPlayerObject = NULL;
	mLookAtPlayer = false;
   mMaxRange = -1.0f;
	//choose our path manager (server/client)
	if(isServerObject())
		mPathManager = gServerPathManager;
	else
		mPathManager = gClientPathManager;
}

CameraGoalPath::~CameraGoalPath()
{
}

void CameraGoalPath::consoleInit()
{
   Con::addVariable("$CameraGoalPath::renderCameraGoalPathRays", TypeBool, &sRenderCameraGoalPathRays,
      "@brief Determines if the cameraGoal's collision interaction rays should be rendered.\n\n"
      "This is mainly used for the tools and debugging.\n"
      "@ingroup GameObjects\n");
}

F32 CameraGoalPath::getUpdatePriority(CameraScopeQuery *camInfo, U32 updateMask, S32 updateSkips)
{
	//we need a higher priority because
	//cameraGoalFollower depends on us
	return 5.0f;
}

void CameraGoalPath::processTick(const Move*)
{
	if (!mPathManager->isValidPath(mCameraPathIndex)
		|| !mPathManager->isValidPath(mPlayerPathIndex)
		|| !mPlayerObject)
		return;

	mDelta.timeVec = mT;

	Point3F playerPos = mPlayerObject->getNodePosition("Cam");
	mT = mPathManager->getClosestTimeToPoint(mPlayerPathIndex, playerPos);
	mPathManager->getPathPosition(mCameraPathIndex, mT, mPosition, mRot);

#ifdef ENABLE_DEBUGDRAW
   if (sRenderCameraGoalPathRays)
   {
      Point3F debugCameraPos, debugPlayerPos; QuatF dummy;
      mPathManager->getPathPosition(mCameraPathIndex, mT, debugCameraPos, dummy);
      mPathManager->getPathPosition(mPlayerPathIndex, mT, debugPlayerPos, dummy);
      DebugDrawer::get()->drawLine(playerPos, debugPlayerPos, ColorI::BLUE);
      DebugDrawer::get()->setLastTTL(TickMs);
      DebugDrawer::get()->drawLine(debugPlayerPos, debugCameraPos, ColorI::RED);
      
      U32 numCamPathPoints = mPathManager->getPathNumWaypoints(mCameraPathIndex);
      U32 numPlayerathPoints = mPathManager->getPathNumWaypoints(mPlayerPathIndex);

      if (numCamPathPoints == numPlayerathPoints)
      {
         for (U32 i = 0; i < numCamPathPoints; i++)
         {
            U32 camPathPointTime = mPathManager->getWaypointTime(mCameraPathIndex, i);
            U32 playerPathPointTime = mPathManager->getWaypointTime(mPlayerPathIndex, i);

            Point3F camPathPointPos, playerPathPointPos;

            mPathManager->getPathPosition(mCameraPathIndex, camPathPointTime, camPathPointPos, dummy);
            mPathManager->getPathPosition(mPlayerPathIndex, playerPathPointTime, playerPathPointPos, dummy);

            VectorF vec = playerPathPointPos - camPathPointPos;
            F32 vecLen = vec.len();
            vec.normalizeSafe();

            Point3F mid = camPathPointPos + (vec * (vecLen * 0.5f));

            ColorI lineColor = ColorI::GREEN;
            if(vecLen > mMaxRange)
               lineColor = ColorI::RED;

            DebugDrawer::get()->drawLine(camPathPointPos, playerPathPointPos, lineColor);
            DebugDrawer::get()->drawText(mid, Con::getFloatArg(vecLen));
         }
      }

      DebugDrawer::get()->setLastTTL(TickMs);
   }
#endif

	//look at player
	if(mLookAtPlayer)
	{
		Point3F camToPlayerVec = playerPos - mPosition;
      Point3F loc;

      bool invalidPos = (mMaxRange > 0 && camToPlayerVec.len() > mMaxRange);
      /*if (!MathUtils::mProjectWorldToScreen(playerPos, &loc, GFX->getViewport(), GFX->getWorldMatrix(), GFX->getProjectionMatrix()))
         invalidPos = true;*/

      if (invalidPos == true)
         onLeaveRange_callback(mPlayerObject);

		camToPlayerVec.normalizeSafe();
		F32 camToPlayerYaw, camToPlayerPitch;
      AAKUtils::getAnglesFromVector(camToPlayerVec, camToPlayerYaw, camToPlayerPitch);

		//stuff that back into a quat
		MatrixF xRot, zRot;
		xRot.set(EulerF(-camToPlayerPitch, 0.0f, 0.0f));
		zRot.set(EulerF(0.0f, 0.0f, camToPlayerYaw));
		MatrixF temp;
		temp.mul(zRot, xRot);
		mRot.set(temp);
	}

	setPosition(mPosition, mRot);

	// Set frame interpolation
	mDelta.time = mT;
	mDelta.timeVec -= mT;
}

void CameraGoalPath::getCameraTransform(F32* pos,MatrixF* mat)
{
	getRenderEyeTransform(mat);
}

void CameraGoalPath::interpolateTick(F32 dt)
{
	if (!mPathManager->isValidPath(mCameraPathIndex)
		|| !mPathManager->isValidPath(mPlayerPathIndex)
		|| !mPlayerObject)
		return;

	Parent::interpolateTick(dt);
	MatrixF mat;
	interpolateMat(mDelta.time + (mDelta.timeVec * dt),&mat);
	Parent::setRenderTransform(mat);
}

void CameraGoalPath::setPosition(const Point3F& pos, const QuatF& rot)
{
	MatrixF mat;
	rot.setMatrix(&mat);
	mat.setColumn(3,pos);
	Parent::setTransform(mat);
}

void CameraGoalPath::setRenderPosition(const Point3F& pos, const QuatF& rot)
{
	MatrixF mat;
	rot.setMatrix(&mat);
	mat.setColumn(3,pos);
	Parent::setRenderTransform(mat);
}

void CameraGoalPath::interpolateMat(F64 t, MatrixF* mat)
{
	Point3F pos; QuatF rot;
	gClientPathManager->getPathPosition(mCameraPathIndex, t, pos, rot);
	if(mLookAtPlayer)
		mRot.setMatrix(mat);
	else
		rot.setMatrix(mat);
	mat->setPosition(pos);
}

void CameraGoalPath::onDeleteNotify(SimObject *obj)
{
	Parent::onDeleteNotify(obj);

	if (obj == (SimObject*)mPlayerObject)
	{
		mPlayerObject = NULL;
	}
}

U32 CameraGoalPath::packUpdate(NetConnection *con, U32 mask, BitStream *stream)
{
	U32 retMask = Parent::packUpdate(con, mask, stream);

	if (stream->writeFlag(mask & ModeMask))
	{
		stream->write(mLookAtPlayer);
	}

	if(stream->writeFlag((mask & TMask)))
	{
		stream->write(mT);
	}

	if(stream->writeFlag((mask & PlayerPathMask)))
	{
		stream->write(mPlayerPathIndex);
	}

	if(stream->writeFlag((mask & CameraPathMask)))
	{
		stream->write(mCameraPathIndex);
	}

	if(stream->writeFlag((mask & PlayerMask) && mPlayerObject))
	{
		S32 id = con->getGhostIndex(mPlayerObject);
		stream->write(id);

		//hack to keep sending updates until player is ghosted
		if(id == -1)
			setMaskBits(PlayerMask);
	}

	return retMask;
}

void CameraGoalPath::unpackUpdate(NetConnection *con, BitStream *stream)
{
	Parent::unpackUpdate(con,stream);

	//ModeMask
	if (stream->readFlag())
	{
		stream->read(&mLookAtPlayer);
	}

	//TMask
	if (stream->readFlag())
	{
		stream->read(&mT);
		mDelta.time = mT;
		mDelta.timeVec = 0;
	}

	//PlayerPathMask
	if (stream->readFlag())
	{
		stream->read(&mPlayerPathIndex);
	}

	//CameraPathMask
	if (stream->readFlag())
	{
		stream->read(&mCameraPathIndex);
	}

	//PlayerMask
	if (stream->readFlag())
	{
		S32 id;
		stream->read(&id);
		if(id > 0)
		{
         AAKPlayer* playerObject = static_cast<AAKPlayer*>(con->resolveGhost(id));
			setPlayerObject(playerObject);
		}
	}
}

//===============================================================================

bool CameraGoalPath::setPlayerPathObject(SimPath::Path *obj)
{
	if(!obj)
		return false;

	mPlayerPathIndex = obj->getPathIndex();
	setMaskBits(PlayerPathMask);
	return true;
}

DefineEngineMethod( CameraGoalPath, setPlayerPathObject, bool, (SimPath::Path* pathObj), (nullAsType<SimPath::Path*>()), "(Path object)")
{   
	if(pathObj == nullptr)
	{
		Con::errorf("CameraGoalPath::setPlayerPathObject - failed to find object");
		return false;
	}

	return object->setPlayerPathObject(pathObj);
}

//===============================================================================

bool CameraGoalPath::setCameraPathObject(SimPath::Path *obj, F32 maxRange)
{
 	if(!obj)
		return false;

   mMaxRange = maxRange;
	mCameraPathIndex = obj->getPathIndex();
	setMaskBits(CameraPathMask);
	return true;
}

DefineEngineMethod( CameraGoalPath, setCameraPathObject, bool, (SimPath::Path* pathObj, F32 maxRange), (nullAsType<SimPath::Path*>(),-1.0f), "(Path object)")
{
	if(pathObj == nullptr)
	{
		Con::errorf("CameraGoalPath::setCameraPathObject - failed to find object");
		return false;
	}

	return object->setCameraPathObject(pathObj, maxRange);
}

//===============================================================================

bool CameraGoalPath::setPlayerObject(AAKPlayer*obj)
{
	if(!obj)
		return false;

	// reset current object if not null
	if(bool(mPlayerObject))
	{
		clearProcessAfter();
		clearNotify(mPlayerObject);
	}

	mPlayerObject = obj;
	setMaskBits(PlayerMask);

	if(bool(mPlayerObject))
	{
		processAfter(mPlayerObject);
		deleteNotify(mPlayerObject);
	}
	return true;
}

DefineEngineMethod( CameraGoalPath, setPlayerObject, bool, (AAKPlayer* playerObj), (nullAsType<AAKPlayer*>()), "(AAKPlayer object)")
{
	if(playerObj == nullptr)
	{
		Con::errorf("CameraGoalPath::setPlayerObject - failed to find object");
		return false;
	}

	return object->setPlayerObject(playerObj);
}

//===============================================================================

void CameraGoalPath::setLookAtPlayer(bool on)
{
	mLookAtPlayer = on;
	setMaskBits(ModeMask);
}

DefineEngineMethod( CameraGoalPath, setLookAtPlayer, void, (bool lookAt), (false), "(bool)")
{
	object->setLookAtPlayer(lookAt);
}
