
#include "cameraBlocker.h"

#include "math/mathIO.h"
#include "scene/sceneRenderState.h"
#include "core/stream/bitStream.h"
#include "materials/sceneData.h"
#include "gfx/gfxDebugEvent.h"
#include "gfx/gfxTransformSaver.h"
#include "renderInstance/renderPassManager.h"
#include "gfx/gfxDrawUtil.h"

IMPLEMENT_CO_NETOBJECT_V1(CameraBlocker);

ConsoleDocClass(CameraBlocker,
   "@brief \n");

extern bool gEditingMission;
bool CameraBlocker::smRenderBlockers = false;

//-----------------------------------------------------------------------------
// Object setup and teardown
//-----------------------------------------------------------------------------
CameraBlocker::CameraBlocker()
{
   // Flag this object so that it will always
   // be sent across the network to clients
   mNetFlags.set(Ghostable | ScopeAlways);

   // Set it as a "static" object
   mTypeMask |= StaticObjectType;

   mCameraIgnores = false;
}

CameraBlocker::~CameraBlocker()
{
}

void CameraBlocker::consoleInit()
{
   Con::addVariable("$CameraBlocker::renderBlockers", TypeBool, &smRenderBlockers,
      "@brief Forces all CameraBlockers's to render.\n\n"
      "Used by the Tools and debug render modes.\n"
      "@ingroup gameObjects");
}

//-----------------------------------------------------------------------------
// Object Editing
//-----------------------------------------------------------------------------
void CameraBlocker::initPersistFields()
{
   // SceneObject already handles exposing the transform
   Parent::initPersistFields();
}

bool CameraBlocker::onAdd()
{
   if (!Parent::onAdd())
      return false;

   // Set up a 1x1x1 bounding box
   mObjBox.set(Point3F(-0.5f, -0.5f, -0.5f),
      Point3F(0.5f, 0.5f, 0.5f));

   resetWorldBox();

   // Add this object to the scene
   addToScene();

   return true;
}

void CameraBlocker::onRemove()
{
   // Remove this object from the scene
   removeFromScene();

   Parent::onRemove();
}

void CameraBlocker::setTransform(const MatrixF& mat)
{
   // Let SceneObject handle all of the matrix manipulation
   Parent::setTransform(mat);

   // Dirty our network mask so that the new transform gets
   // transmitted to the client object
   setMaskBits(TransformMask);
}

U32 CameraBlocker::packUpdate(NetConnection* conn, U32 mask, BitStream* stream)
{
   // Allow the Parent to get a crack at writing its info
   U32 retMask = Parent::packUpdate(conn, mask, stream);

   // Write our transform information
   if (stream->writeFlag(mask & TransformMask))
   {
      mathWrite(*stream, getTransform());
      mathWrite(*stream, getScale());
   }

   return retMask;
}

void CameraBlocker::unpackUpdate(NetConnection* conn, BitStream* stream)
{
   // Let the Parent read any info it sent
   Parent::unpackUpdate(conn, stream);

   if (stream->readFlag())  // TransformMask
   {
      mathRead(*stream, &mObjToWorld);
      mathRead(*stream, &mObjScale);

      setTransform(mObjToWorld);
   }
}

void CameraBlocker::prepRenderImage(SceneRenderState* state)
{
   if (!gEditingMission || (!CameraBlocker::smRenderBlockers && !isSelected()))
      return;

   ObjectRenderInst* ri = state->getRenderPass()->allocInst<ObjectRenderInst>();
   ri->renderDelegate.bind(this, &CameraBlocker::render);
   ri->type = RenderPassManager::RIT_Editor;
   ri->translucentSort = true;
   ri->defaultKey = 1;
   state->getRenderPass()->addInst(ri);
}

void CameraBlocker::render(ObjectRenderInst* ri, SceneRenderState* state, BaseMatInstance* overrideMat)
{
   if (overrideMat)
      return;

   GFXStateBlockDesc desc;
   desc.setZReadWrite(true, false);
   desc.setBlend(true);

   // Trigger polyhedrons are set up with outward facing normals and CCW ordering
   // so can't enable backface culling.
   desc.setCullMode(GFXCullNone);

   GFXTransformSaver saver;

   MatrixF mat = getRenderTransform();
   mat.scale(getScale());

   GFX->multWorld(mat);

   GFXDrawUtil* drawer = GFX->getDrawUtil();

   Box3F bounds = getObjBox();

   drawer->drawCube(desc, bounds, ColorI(0, 150, 150, 45));

   // Render wireframe.
   desc.setFillModeWireframe();
   drawer->drawCube(desc, bounds, ColorI::BLACK);
}

bool CameraBlocker::castRay(const Point3F& start, const Point3F& end, RayInfo* info)
{
   Box3F box = getObjBox();

   F32 t;
   Point3F n;
   if (box.collideLine(start, end, &t, &n))
   {
      info->normal = n;
      info->normal.normalizeSafe();
      getTransform().mulV(info->normal);

      info->t = t;
      info->object = this;
      info->point.interpolate(start, end, t);
      info->material = 0;
      return true;
   }

   return false;
}

bool CameraBlocker::castRayRendered(const Point3F& start, const Point3F& end, RayInfo* info)
{
   Box3F box = getObjBox();

   F32 t;
   Point3F n;
   if (box.collideLine(start, end, &t, &n))
   {
      info->normal = n;
      info->normal.normalizeSafe();
      getTransform().mulV(info->normal);

      info->t = t;
      info->object = this;
      info->point.interpolate(start, end, t);
      info->material = 0;
      return true;
   }

   return false;
}
