#include "platform/platform.h"
#include "cameraTrigger.h"

#include "scene/sceneRenderState.h"
#include "console/consoleTypes.h"
#include "console/engineAPI.h"
#include "collision/boxConvex.h"

#include "core/stream/bitStream.h"
#include "math/mathIO.h"
#include "gfx/gfxTransformSaver.h"
#include "renderInstance/renderPassManager.h"
#include "gfx/gfxDrawUtil.h"
#include "T3D/physics/physicsPlugin.h"
#include "T3D/physics/physicsBody.h"
#include "T3D/physics/physicsCollision.h"

#include "math/mathUtils.h"

IMPLEMENT_CO_NETOBJECT_V1(CameraTrigger);
void CameraTrigger::initPersistFields()
{
   addField("Time", TypeF32, Offset(mTime, CameraTrigger), "How long to pan");
   addField("ForcedPitch", TypeF32, Offset(mForcedPitch, CameraTrigger), "Pitch");
   addField("ForcedRadius", TypeF32, Offset(mForcedRadius, CameraTrigger), "Distance from controlled body");
   addField("ForcedYaw", TypeF32, Offset(mForcedYaw, CameraTrigger), "Yaw");
   //addField("OrientBase", TYPEID<CameraBookmark>(), Offset(mOrientBase, CameraTrigger), "");

   addProtectedField("link", TypeString, Offset(mObjectLinkId, CameraTrigger),
      &_setFieldLink, &defaultProtectedGetFn,
      "@Object to use to set values.\n\n");

   addProtectedField("sync", TypeBool, Offset(mSync, CameraTrigger),
      &_sync, &defaultProtectedGetFn, "sync values to tracked object", AbstractClassRep::FieldFlags::FIELD_ComponentInspectors);

   Parent::initPersistFields();
}

bool CameraTrigger::_setFieldLink(void* object, const char* index, const char* data)
{
   CameraTrigger* so = static_cast<CameraTrigger*>(object);
   if (so)
   {
      Con::setData(TypeS32, &so->mObjectLinkId, 0, 1, &data);
      return true;
   }
   return false;
}

bool CameraTrigger::_sync(void* object, const char* index, const char* data)
{
   CameraTrigger* so = reinterpret_cast<CameraTrigger*>(object);
   so->syncOrientation();
   return false;
}

void CameraTrigger::syncOrientation()
{
   if (!Sim::findObject(mObjectLinkId, mOrientBase))
      return;

   MathUtils::getAnglesFromVector(mOrientBase->getTransform().getForwardVector(), mForcedYaw, mForcedPitch);
   mForcedYaw = mRadToDeg(mForcedYaw) + 180;
   mForcedPitch = mRadToDeg(-mForcedPitch);
   mForcedRadius = Point3F(mOrientBase->getTransform().getPosition() - getPosition()).len();
}

DefineEngineMethod(CameraTrigger, syncOrientation, void, (), ,
   "@brief grab the transfrom of a tracked object, and use it's transform to set pitch and yaw\n")
{
   object->syncOrientation();
}
