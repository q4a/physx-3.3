/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
#include "RendererFoundation.h"
#include <Renderer.h>
#include <RendererDesc.h>
#include "ogl/OGLRenderer.h"
#include "gles2/GLES2Renderer.h"
#include "d3d9/D3D9Renderer.h"
#if defined(RENDERER_ENABLE_LIBGNM)
#include "ps4/GnmRenderer.h"
#endif
#if defined(RENDERER_ENABLE_LIBGXM)
#include "psp2/GXMRenderer.h"
#endif
#include "d3d11/D3D11Renderer.h"
#include "null/NULLRenderer.h"
#include <RendererMesh.h>
#include <RendererMeshContext.h>

#include <RendererMaterial.h>
#include <RendererMaterialDesc.h>
#include <RendererMaterialInstance.h>
#include <RendererProjection.h>

#include <RendererTarget.h>
#include <RendererTexture2D.h>
#include <RendererTexture2DDesc.h>

#include <RendererVertexBuffer.h>
#include <RendererVertexBufferDesc.h>
#include <RendererMesh.h>
#include <RendererMeshDesc.h>

#include <RendererLight.h>
#include "PsUtilities.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "PsUtilities.h"
#include "extensions/PxDefaultStreams.h"

#if defined(RENDERER_ANDROID)
#include <android/AndroidSampleUserInputIds.h>
#endif

#ifdef PHYSX_PROFILE_SDK
#include "physxprofilesdk/PxProfileZone.h"
#include "physxprofilesdk/PxProfileEventSender.h"
#endif

physx::PxProfileZone*		  SampleRenderer::Renderer::mProfileZone = NULL;

// for PsString.h
namespace physx
{
	namespace string
	{}
}
#include <PsString.h>
#include <PsFile.h>

using namespace SampleRenderer;


/* Screen pads for tablets.
 Initialize two controls - second one is the vertically mirrored version of the first one with respect to the screen center 
 All values are in relative units, given that bottom-left point is (-1.0f, -1.0f), center is (0.0f, 0.0f) and top-right point is (1.0f, 1.0f) */
#if defined(RENDERER_TABLET)
const PxReal gControlSizeRelative = 0.16f;
const PxReal gControlMarginRelative = 0.08f;
const PxU8 CONTROL_COUNT = 2;

const PxU8 TEXT_HEIGHT = 16;
const PxU8 TEXT_VERTICAL_SPACING = TEXT_HEIGHT / 4;
#endif

Renderer *Renderer::createRenderer(const RendererDesc &desc, const char* assetDir, bool enableMaterialCaching)
{
	Renderer *renderer = 0;
	const bool valid = desc.isValid();
	if(valid)
	{
		switch(desc.driver)
		{
		case DRIVER_GLES2:
#if defined(RENDERER_ENABLE_GLES2)
			renderer = new GLES2Renderer(desc, assetDir);
			// disable material caching, not implemented!
			enableMaterialCaching = false;
#endif
			break;

		case DRIVER_OPENGL:
#if defined(RENDERER_ENABLE_OPENGL)
			renderer = new OGLRenderer(desc, assetDir);
#endif
			break;

		case DRIVER_DIRECT3D9:
#if defined(RENDERER_ENABLE_DIRECT3D9)
			renderer = new D3D9Renderer(desc, assetDir);
#endif
			break;

		case DRIVER_DIRECT3D11:
#if defined(RENDERER_ENABLE_DIRECT3D11)
			renderer = new D3D11Renderer(desc, assetDir);
#endif
			break;

		case DRIVER_LIBGCM:
#if defined(RENDERER_ENABLE_LIBGCM)
			// not implemented, PS3 should use OpenGL
#endif
			break;

		case DRIVER_LIBGNM:
#if defined(RENDERER_ENABLE_LIBGNM)
			renderer = new GnmRenderer(desc, assetDir);
			// disable material caching, not implemented!
			enableMaterialCaching = false;
#endif
			break;

		case DRIVER_LIBGXM:
#if defined(RENDERER_ENABLE_LIBGXM)
			renderer = new GXMRenderer(desc, assetDir);
			// disable material caching, not implemented!
			enableMaterialCaching = false;
#endif
			break;

		case DRIVER_NULL:
			renderer = new NullRenderer(desc,assetDir);
			break;
		}

		if(renderer)
		{
			renderer->setEnableMaterialCaching(enableMaterialCaching);
		}
	}
	if(renderer && !renderer->isOk())
	{
		renderer->release();
		renderer = 0;
	}
	RENDERER_ASSERT(renderer, "Failed to create renderer!");
	return renderer;
}

const char *Renderer::getDriverTypeName(DriverType type)
{
	const char *name = 0;
	switch(type)
	{
	case DRIVER_OPENGL:     name = "OpenGL";			break;
	case DRIVER_GLES2:      name = "OpenGL ES 2.0";     break;
	case DRIVER_DIRECT3D9:  name = "Direct3D9";			break;
	case DRIVER_DIRECT3D11: name = "Direct3D11"; break;
	case DRIVER_LIBGCM:     name = "LibGCM";			break;
	case DRIVER_LIBGNM:		name = "LibGNM";	 break;
	case DRIVER_LIBGXM:		name = "LibGXM";	 break;
	case DRIVER_NULL:		name = "NullRenderer";	 break;
	}
	RENDERER_ASSERT(name, "Unable to find Name String for Renderer Driver Type.");
	return name;
}


Renderer::Renderer(DriverType driver, PxErrorCallback* errorCallback, const char* assetDir) :
	m_driver							(driver),
	m_errorCallback						(errorCallback),
	m_textMaterial						(NULL),
	m_textMaterialInstance				(NULL),
	m_screenquadOpaqueMaterial			(NULL),
	m_screenquadOpaqueMaterialInstance	(NULL),
	m_screenquadAlphaMaterial			(NULL),
	m_screenquadAlphaMaterialInstance	(NULL),
	m_useShadersForTextRendering		(true),
	m_assetDir							(assetDir),
	m_cacheDir							(NULL),
	m_enableTessellation				(false),
	m_enableWireframe					(false),
	m_enableBlendingOverride			(false),
	m_enableBlendingCull				(false),
	mEnableMaterialCaching				(true)
{
	m_pixelCenterOffset = 0;
	setAmbientColor(RendererColor(64,64,64, 255));
	setFog(RendererColor(0,0,10,255), 20000.0f);
	setClearColor(RendererColor(133,153,181,255));
	physx::string::strncpy_s(m_deviceName, sizeof(m_deviceName), "UNKNOWN", sizeof(m_deviceName));
#ifdef RENDERER_TABLET
	m_buttons.reserve(16);
#endif
}

Renderer::~Renderer(void)
{
	PX_ASSERT(!m_screenquadOpaqueMaterial);
	PX_ASSERT(!m_screenquadOpaqueMaterialInstance);
	PX_ASSERT(!m_screenquadAlphaMaterial);
	PX_ASSERT(!m_screenquadAlphaMaterialInstance);

	PX_ASSERT(!m_textMaterial);
	PX_ASSERT(!m_textMaterialInstance);

	for (tMaterialCache::iterator it = m_materialCache.begin(); it != m_materialCache.end(); ++it)
	{
		::free((void*)it->first.vertexShaderPath);
		::free((void*)it->first.fragmentShaderPath);
		::free((void*)it->first.geometryShaderPath);
		::free((void*)it->first.domainShaderPath);
		::free((void*)it->first.hullShaderPath);

		PX_ASSERT(it->second == NULL);
	}
}


void Renderer::release(void)
{
	closeScreenquad();
	delete this;
}

// Create a 2D or 3D texture, depending on texture depth
RendererTexture* SampleRenderer::Renderer::createTexture(const RendererTextureDesc &desc)
{
	return desc.depth > 1 ? createTexture3D(desc) : createTexture2D(desc);
}

// get the driver type for this renderer.
Renderer::DriverType Renderer::getDriverType(void) const
{
	return m_driver;
}

// get the offset to the center of a pixel relative to the size of a pixel (so either 0 or 0.5).
PxF32 Renderer::getPixelCenterOffset(void) const
{
	return m_pixelCenterOffset;
}

// get the name of the hardware device.
const char *Renderer::getDeviceName(void) const
{
	return m_deviceName;
}

const char *Renderer::getAssetDir() 
{ 
	return m_assetDir.c_str(); 
}

void Renderer::setAssetDir(const char * assetDir) 
{ 
	m_assetDir = assetDir; 
}

// adds a mesh to the render queue.
void Renderer::queueMeshForRender(RendererMeshContext &mesh)
{
	RENDERER_ASSERT( mesh.isValid(),  "Mesh Context is invalid.");
	if(mesh.isValid())
	{
		if (mesh.screenSpace)
		{
			m_screenSpaceMeshes.push_back(mesh);
		}
		else
		{
			switch (mesh.material->getType())
			{
			case  RendererMaterial::TYPE_LIT:
				if (mesh.material->getBlending())
					m_visibleLitTransparentMeshes.push_back(mesh);
				else
					m_visibleLitMeshes.push_back(mesh);
				break;
			default: //case RendererMaterial::TYPE_UNLIT:
				m_visibleUnlitMeshes.push_back(mesh);
				//	break;
			}
		}
	}
}

struct RenderMeshContextHasMesh
{
	RenderMeshContextHasMesh(const RendererMesh* mesh) : mMesh(mesh) { }

	bool operator()(const RendererMeshContext& meshContext) 
	{
		return meshContext.mesh == mMesh;
	}

	const RendererMesh* mMesh;
};

void SampleRenderer::Renderer::removeMeshFromRenderQueue(RendererMesh& mesh)
{
	MeshVector* meshContextQueues[] = { &m_visibleLitMeshes, 
	                                    &m_visibleUnlitMeshes,
	                                    &m_screenSpaceMeshes,
	                                    &m_visibleLitTransparentMeshes };

	// All queues must be searched, as a single mesh can be used in multiple contexts
	for (PxU32 i = 0; i < PX_ARRAY_SIZE(meshContextQueues); ++i)
	{
		MeshVector& meshContextQueue = *meshContextQueues[i];
		meshContextQueue.erase(std::remove_if(meshContextQueue.begin(), 
		                                      meshContextQueue.end(), 
		                                      RenderMeshContextHasMesh(&mesh)), 
		                       meshContextQueue.end());
	}
}

// adds a light to the render queue.
void Renderer::queueLightForRender(RendererLight &light)
{
	RENDERER_ASSERT(!light.isLocked(), "Light is already locked to a Renderer.");
	if(!light.isLocked())
	{
		light.m_renderer = this;
		m_visibleLights.push_back(&light);
	}
}

void SampleRenderer::Renderer::removeLightFromRenderQueue(RendererLight &light)
{
	m_visibleLights.erase(std::remove(m_visibleLights.begin(), m_visibleLights.end(), &light));
}

// renders the current scene to the offscreen buffers. empties the render queue when done.
void Renderer::render(const physx::PxMat44 &eye, const RendererProjection &proj, RendererTarget *target, bool depthOnly)
{
	RENDERER_PERFZONE(Renderer_render);
	const PxU32 numLights = (PxU32)m_visibleLights.size();
	if(target)
	{
		target->bind();
	}
	// TODO: Sort meshes by material..
	ScopedRender renderSection(*this);
	if(renderSection)
	{
		if(!depthOnly)
		{
			// YOU CAN PASS THE PROJECTION MATIX RIGHT INTO THIS FUNCTION!
			// TODO: Get rid of this.
			if (m_screenSpaceMeshes.size())
			{
				physx::PxMat44 id = physx::PxMat44(PxIdentity);
				bindViewProj(id, RendererProjection(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f));	//TODO: pass screen space matrices
				renderMeshes(m_screenSpaceMeshes, RendererMaterial::PASS_UNLIT);	//render screen space stuff first so stuff we occlude doesn't waste time on shading.
			}
		}

		sortMeshes(eye);

		if(depthOnly)
		{
			RENDERER_PERFZONE(Renderer_render_depthOnly);
			bindAmbientState(RendererColor(0,0,0,255));
			bindViewProj(eye, proj);
			renderMeshes(m_visibleLitMeshes,   RendererMaterial::PASS_DEPTH);
			renderMeshes(m_visibleUnlitMeshes, RendererMaterial::PASS_DEPTH);
		}
		else  if(numLights > RENDERER_DEFERRED_THRESHOLD)
		{
			RENDERER_PERFZONE(Renderer_render_deferred);
			bindDeferredState();
			bindViewProj(eye, proj);
			renderMeshes(m_visibleLitMeshes,   RendererMaterial::PASS_UNLIT);
			renderMeshes(m_visibleUnlitMeshes, RendererMaterial::PASS_UNLIT);
			renderMeshes(m_visibleLitTransparentMeshes,   RendererMaterial::PASS_UNLIT);
			renderDeferredLights();
		}
		else if(numLights > 0)
		{
			RENDERER_PERFZONE(Renderer_render_lit);
			bindAmbientState(m_ambientColor);
			bindFogState(m_fogColor, m_fogDistance);
			bindViewProj(eye, proj);

#if RENDERER_ENABLE_SINGLE_PASS_LIGHTING
			for(PxU32 i=0; i<numLights; i++)
			{
				m_visibleLights[i]->bind(i);
			}
			renderMeshes(m_visibleLitMeshes, m_visibleLights[0]->getPass());
			for(PxU32 i=0; i<numLights; i++)
			{
				m_visibleLights[i]->m_renderer = NULL;
			}
#else
			RendererLight &light0 = *m_visibleLights[0];
			light0.bind();
			renderMeshes(m_visibleLitMeshes, light0.getPass());

			bindAmbientState(RendererColor(0,0,0,255));
			beginMultiPass();
			for(PxU32 i=1; i<numLights; i++)
			{
				RendererLight &light = *m_visibleLights[i];
				light.bind();
				renderMeshes(m_visibleLitMeshes, light.getPass());
			}
			endMultiPass();
#endif
			renderMeshes(m_visibleUnlitMeshes, RendererMaterial::PASS_UNLIT);

			///////////////////////////////////////////////////////////////////////////
			if (m_visibleLitTransparentMeshes.size() > 0) 
			{
				light0.bind();
				renderMeshes(m_visibleLitTransparentMeshes, light0.getPass());

				bindAmbientState(RendererColor(0,0,0,255));
				beginTransparentMultiPass();
				for(PxU32 i=1; i<numLights; i++)
				{
					RendererLight &light = *m_visibleLights[i];
					light.bind();
					renderMeshes(m_visibleLitTransparentMeshes, light.getPass());
				}
				endTransparentMultiPass();
			}
			///////////////////////////////////////////////////////////////////////////

			for (physx::PxU32 i = 0; i < numLights; ++i)
				m_visibleLights[i]->m_renderer = 0;
		}
		else
		{
			RENDERER_PERFZONE(Renderer_render_unlit);
			bindAmbientState(RendererColor(0,0,0,255));
			bindViewProj(eye, proj);
			renderMeshes(m_visibleLitMeshes,   RendererMaterial::PASS_UNLIT);
			renderMeshes(m_visibleUnlitMeshes, RendererMaterial::PASS_UNLIT);
			renderMeshes(m_visibleLitTransparentMeshes, RendererMaterial::PASS_UNLIT);
		}
	}
	if(target) target->unbind();
	m_visibleLitMeshes.clear();
	m_visibleUnlitMeshes.clear();
	m_visibleLitTransparentMeshes.clear();
	m_screenSpaceMeshes.clear();
	m_visibleLights.clear();
}

// sets the ambient lighting color.
void Renderer::setAmbientColor(const RendererColor &ambientColor)
{
	m_ambientColor   = ambientColor;
	m_ambientColor.a = 255;
}

void Renderer::setFog(const RendererColor &fogColor, float fogDistance)
{
	m_fogColor    = fogColor;
	m_fogDistance = fogDistance;
}

void Renderer::setClearColor(const RendererColor &clearColor)
{
	m_clearColor   = clearColor;
	m_clearColor.a = 255;
}

Renderer::TessellationParams::TessellationParams()
{
	setDefault();
}

void Renderer::TessellationParams::setDefault()
{
	tessFactor                = PxVec4(6.f, 6.f, 3.f, 100.f);
	tessMinMaxDistance[0]     = 5.0f; 
	tessMinMaxDistance[1]     = 50;
	tessHeightScaleAndBias[0] = 1.0f;
	tessHeightScaleAndBias[1] = 0.5f;
	tessUVScale[0]            = 1.0f;
	tessUVScale[1]            = 1.0f;
}

std::string SampleRenderer::Renderer::TessellationParams::toString()
{
	std::stringstream ss;
	ss << "TessParams =  "      << std::endl;
	ss << "\tTessFactor:      " << tessFactor.x << " " <<  tessFactor.y << " " << tessFactor.z << " " << tessFactor.w << std::endl;
	ss << "\tTessMinMax:      " << tessMinMaxDistance[0] << " " << tessMinMaxDistance[1] << std::endl;
	ss << "\tTessHeightScale: " << tessHeightScaleAndBias[0] << " " << tessHeightScaleAndBias[1] << std::endl;
	ss << "\tTessUVScale:     " << tessUVScale[0] << " " << tessUVScale[1] << std::endl;
	return ss.str();
}

void Renderer::setTessellationParams(const TessellationParams& params)
{
	m_tessellationParams = params;
}

static PX_INLINE PxU32 createPixel(PxU8 r, PxU8 g, PxU8 b)
{
	// G = bits 0-7
	// B = bits 8-15
	// R = bits 16-23
	return (r << 16) | (g << 8) | b;
}

void Renderer::formatScreenshot(PxU32 width, PxU32 height, PxU32 sizeInBytes, int rPosition, int gPosition, int bPosition, bool bFlipY, void* screenshotData)
{
	if (width <= 0 || height <= 0 || sizeInBytes <= 0)
		return;

	PxU32 srcByteStride = sizeInBytes / (width * height);
	RENDERER_ASSERT((srcByteStride > 0) && (srcByteStride <= sizeof(PxU32)), "Invalid image format.");
	if (srcByteStride <= 0 || srcByteStride > sizeof(PxU32))
		return;

	PxU32* pSrc    = static_cast<PxU32*>(screenshotData);
	PxU8* pSrcByte = static_cast<PxU8*>(screenshotData);
	if (bFlipY)
	{
		for (PxU32 i = 0; i < height/2; ++i)
		{
			PxU32* pSrc2     = static_cast<PxU32*>(screenshotData) +                 width * (height - i - 1);
			PxU8*  pSrc2Byte = static_cast<PxU8*>(screenshotData)  + srcByteStride * width * (height - i - 1);
			for (PxU32 j = 0; j < width; ++j, pSrcByte+=srcByteStride, pSrc2Byte+=srcByteStride, ++pSrc, ++pSrc2)
			{
				PxU32 temp = createPixel(pSrcByte[rPosition], pSrcByte[gPosition], pSrcByte[bPosition]);
				*pSrc      = createPixel(pSrc2Byte[rPosition], pSrc2Byte[gPosition], pSrc2Byte[bPosition]);
				*pSrc2     = temp;
			}
		}
		// Format the middle scanline
		if (height % 2 == 1)
		{
			for (PxU32 j = 0; j < width; ++j, pSrcByte+=srcByteStride,++pSrc)
			{
				*pSrc = createPixel(pSrcByte[rPosition], pSrcByte[gPosition], pSrcByte[bPosition]);
			}
		}
	}
	else
	{
		for (PxU32 i = 0; i < height; ++i)
		{
			for (PxU32 j = 0; j < width; ++j, pSrcByte+=srcByteStride,++pSrc)
			{
				*pSrc = createPixel(pSrcByte[rPosition], pSrcByte[gPosition], pSrcByte[bPosition]);
			}
		}
	}
}

void Renderer::renderMeshes(std::vector<RendererMeshContext>& meshes, RendererMaterial::Pass pass)
{
	RENDERER_PERFZONE(Renderer_renderMeshes);

	RendererMaterial         *lastMaterial         = 0;
	RendererMaterialInstance *lastMaterialInstance = 0;
	const RendererMesh       *lastMesh             = 0;

	const PxU32 numMeshes = (PxU32)meshes.size();
	for(PxU32 i=0; i<numMeshes; i++)
	{
		RendererMeshContext& context = meshes[i];

		if(!context.mesh->willRender())
		{
			continue;
		}

		// the mesh context should be bound before the material, this is 
		// necessary because the materials read state from the graphics device
		// for instance to determine if two-sided lighting should be enabled
		// for gles 2 this doesn't work yet due to internal hacks
#if !defined(RENDERER_ENABLE_GLES2)
		bindMeshContext(context);
#endif
		
		bool instanced = context.mesh->getInstanceBuffer()?true:false;

		if(context.materialInstance && context.materialInstance != lastMaterialInstance)
		{
			if(lastMaterial) lastMaterial->unbind();
			lastMaterialInstance =  context.materialInstance;
			lastMaterial         = &context.materialInstance->getMaterial();
			lastMaterial->bind(pass, lastMaterialInstance, instanced);
		}
		else if(context.material != lastMaterial)
		{
			if(lastMaterial) lastMaterial->unbind();
			lastMaterialInstance = 0;
			lastMaterial         = context.material;
			lastMaterial->bind(pass, lastMaterialInstance, instanced);
		}

#if defined(RENDERER_ENABLE_GLES2)
		bindMeshContext(context);
#endif

		if(lastMaterial) lastMaterial->bindMeshState(instanced);
		if(context.mesh != lastMesh)
		{
			if(lastMesh) lastMesh->unbind();
			lastMesh = context.mesh;
			if(lastMesh) lastMesh->bind();
		}
		if(lastMesh) context.mesh->render(context.material);
#ifdef RENDERER_PSP2
		// on PSP2 we need to will the environment again
		lastMaterial = NULL;
		lastMaterialInstance = NULL;
#endif
	}
	if(lastMesh)     lastMesh->unbind();
	if(lastMaterial) lastMaterial->unbind();
}

struct CompareMeshCameraDist
{
	CompareMeshCameraDist(const physx::PxVec3& t_) : t(t_) { }
	bool operator()(const RendererMeshContext& c1, const RendererMeshContext& c2) const
	{
		// Equivalent, return false
		if (NULL == c1.transform && NULL == c2.transform)
			return false;
		// NULL transforms will be rendered first
		if (NULL == c1.transform)
			return true;
		if (NULL == c2.transform)
			return false;
		// A larger magnitude means further away, which should be ordered first
		// TODO: Use bones
		return (c1.transform->getPosition() - t).magnitudeSquared() > (c2.transform->getPosition() - t).magnitudeSquared();
	}
	physx::PxVec3 t;
};

void Renderer::sortMeshes(const physx::PxMat44& eye)
{
	CompareMeshCameraDist meshComp(eye.getPosition());
	std::sort(m_visibleLitTransparentMeshes.begin(), m_visibleLitTransparentMeshes.end(), meshComp);
}

void Renderer::renderDeferredLights(void)
{
	RENDERER_PERFZONE(Renderer_renderDeferredLights);
	const PxU32 numLights = (PxU32)m_visibleLights.size();
	for(PxU32 i=0; i<numLights; i++)
	{
		renderDeferredLight(*m_visibleLights[i]);
	}
}

bool Renderer::CompareRenderMaterialDesc::operator()(const RendererMaterialDesc& desc1, const RendererMaterialDesc& desc2) const
{
	if (desc1.type != desc2.type)
		return desc1.type < desc2.type;

	if (desc1.alphaTestFunc != desc2.alphaTestFunc)
		return desc1.alphaTestFunc < desc2.alphaTestFunc;

	if (desc1.alphaTestRef != desc2.alphaTestRef)
		return desc1.alphaTestRef < desc2.alphaTestRef;

	if (desc1.blending != desc2.blending)
		return desc1.blending < desc2.blending;

	if (desc1.srcBlendFunc != desc2.srcBlendFunc)
		return desc1.srcBlendFunc < desc2.srcBlendFunc;

	if (desc1.dstBlendFunc != desc2.dstBlendFunc)
		return desc1.dstBlendFunc < desc2.dstBlendFunc;

	int result = physx::string::stricmp(desc1.vertexShaderPath, desc2.vertexShaderPath);
	if (result != 0)
		return result < 0;

	result = physx::string::stricmp(desc1.fragmentShaderPath, desc2.fragmentShaderPath);
	if (result != 0)
		return result < 0;

	// Geometry, hull and domain shaders are optional, so only check when present
	//    * stricmp only works properly for well defined pointers, so only test when both present
	//    * if one material has a particular shader when the other material does not, the one WITH should be first
	if (desc1.geometryShaderPath || desc2.geometryShaderPath)
	{
		if (desc1.geometryShaderPath && desc2.geometryShaderPath)
			result = physx::string::stricmp(desc1.geometryShaderPath, desc2.geometryShaderPath);
		else
			result = desc1.geometryShaderPath ? -1 : 1;
	}

	if (0 == result && (desc1.hullShaderPath || desc2.hullShaderPath))
	{
		if (desc1.hullShaderPath && desc2.hullShaderPath)
			result = physx::string::stricmp(desc1.hullShaderPath, desc2.hullShaderPath);
		else
			result = desc1.hullShaderPath ? -1 : 1;
	}
	
	if (0 == result && (desc1.domainShaderPath || desc2.domainShaderPath))
	{
		if (desc1.domainShaderPath && desc2.domainShaderPath)
			result = physx::string::stricmp(desc1.domainShaderPath, desc2.domainShaderPath);
		else
			result = desc1.domainShaderPath ? -1 : 1;
	}

	return result < 0;
}

RendererMaterial* Renderer::hasMaterialAlready(const RendererMaterialDesc& desc)
{
	if (mEnableMaterialCaching)
	{
		tMaterialCache::iterator it = m_materialCache.find(desc);

		if (it != m_materialCache.end())
		{
			PX_ASSERT(it->second != NULL);
			it->second->incRefCount();
			return it->second;
		}
	}

	return NULL;
}



static const char* local_strdup(const char* input)
{
	if (input == NULL)
	{
		return NULL;
	}

	unsigned int strLen = (unsigned int)strlen(input) + 1;
	char* retStr = (char*)::malloc(strLen);

	PX_ASSERT(retStr);

	if (NULL != retStr)
	{
#ifdef PX_WINDOWS
		strcpy_s(retStr, strLen, input);
#else
		strncpy(retStr, input, strLen);
#endif
	}
	return retStr;
}



void Renderer::registerMaterial(const RendererMaterialDesc& desc, RendererMaterial* mat)
{
	if (mEnableMaterialCaching)
	{
		tMaterialCache::iterator it = m_materialCache.find(desc);

		if (it == m_materialCache.end())
		{
			RendererMaterialDesc descCopy = desc;
			descCopy.vertexShaderPath = local_strdup(desc.vertexShaderPath);
			descCopy.fragmentShaderPath = local_strdup(desc.fragmentShaderPath);
			descCopy.geometryShaderPath = local_strdup(desc.geometryShaderPath);
			descCopy.domainShaderPath = local_strdup(desc.domainShaderPath);
			descCopy.hullShaderPath = local_strdup(desc.hullShaderPath);

			PX_ASSERT(mat->m_refCount == 1);
			m_materialCache[descCopy] = mat;
		}
		else
		{
			PX_ASSERT(it->second == NULL);
			it->second = mat;
		}
	}
}



void Renderer::releaseAllMaterials()
{
	for (tMaterialCache::iterator it = m_materialCache.begin(); it != m_materialCache.end(); ++it)
	{
		PX_ASSERT(it->second != NULL);
		delete it->second;

		it->second = NULL;
	}
}



///////////////////////////////////////////////////////////////////////////////


#include "RendererMemoryMacros.h"
#include "RendererMaterialDesc.h"
#include "RendererTexture2DDesc.h"

// PT: text stuff from ICE. Adapted quickly, should be cleaned up when we have some time.

struct FntInfo
{
	PxReal		u0;
	PxReal		v0;
	PxReal		u1;
	PxReal		v1;
	PxU32		dx;
	PxU32		dy;
};

class FntData
{
public:
	FntData();
	~FntData();

	void			Reset();
	PxU32			ComputeSize(const char* text, PxReal& width, PxReal& height, PxReal scale, bool forceFixWidthNumbers) const;

	bool			Load(Renderer& renderer, const char* filename);

	PX_FORCE_INLINE	PxU32			GetNbFnts()	const	{ return mNbFnts;	}
	PX_FORCE_INLINE	const FntInfo*	GetFnts()	const	{ return mFnts;		}
	PX_FORCE_INLINE	PxU32			GetMaxDx()	const	{ return mMaxDx;	}
	PX_FORCE_INLINE	PxU32			GetMaxDy()	const	{ return mMaxDy;	}
	PX_FORCE_INLINE	const PxU8*		GetXRef()	const	{ return mXRef;		}
	PX_FORCE_INLINE PxU32			GetMaxDxNumbers()	const	{ return mMaxDxNumbers;	}
	PX_FORCE_INLINE bool			IsFixWidthCharacter(unsigned char c) const { return mFixWidthCharacters[c]; }

private:
	PxU32			mNbFnts;
	FntInfo*		mFnts;
	PxU32			mMaxDx, mMaxDy;
	PxU8			mXRef[256];
	bool			mFixWidthCharacters[256];
	PxU32			mMaxDxNumbers;
public:
	RendererTexture2D*			mTexture;
};

FntData::FntData()
{
	mNbFnts	= 0;
	mFnts	= NULL;
	mMaxDx	= 0;
	mMaxDy	= 0;
	memset(mXRef, 0, 256*sizeof(PxU8));
	memset(mFixWidthCharacters, 0, 256 * sizeof(bool));
	for (int i = '0'; i <= '9'; i++)
	{
		mFixWidthCharacters[i] = true;
	}
	//mFixWidthCharacters[size_t(' ')] = true;
	mFixWidthCharacters[size_t('.')] = true;
	mFixWidthCharacters[size_t('+')] = true;
	mFixWidthCharacters[size_t('-')] = true;
	mFixWidthCharacters[size_t('*')] = true;
	mFixWidthCharacters[size_t('/')] = true;
	mMaxDxNumbers = 0;
	mTexture	= NULL;
}

FntData::~FntData()
{
	Reset();
}

void FntData::Reset()
{
	SAFE_RELEASE(mTexture);
	DELETEARRAY(mFnts);
	mNbFnts	= 0;
	mMaxDx	= 0;
	mMaxDy	= 0;
	memset(mXRef, 0, 256*sizeof(PxU8));
	memset(mFixWidthCharacters, 0, 256 * sizeof(bool));
	mMaxDxNumbers = 0;
}

// Compute size of a given text
PxU32 FntData::ComputeSize(const char* text, PxReal& width, PxReal& height, PxReal scale, bool forceFixWidthNumbers) const
{
	// Get and check length
	if(!text)	return 0;
	PxU32 Nb = (PxU32)strlen((const char*)text);
	if(!Nb) return 0;

	PxReal x = 0.0f;
	PxReal y = 0.0f;

	width = -1.0f;
	height = -1.0f;

	// Loop through characters
	for(PxU32 j=0;j<Nb;j++)
	{
		if(text[j]!='\n')
		{
			// Catch current character index
			const PxU32 i = mXRef[(unsigned char)(text[j])];

			// Catch size of current character
			const PxReal sx = PxReal((forceFixWidthNumbers && mFixWidthCharacters[(size_t)text[j]]) ? mMaxDxNumbers : mFnts[i].dx) * scale;
			const PxReal sy = PxReal(mFnts[i].dy) * scale;

			// Keep track of greatest dimensions
			if((x+sx)>width)	width = x+sx;
			if((y+sy)>height)	height = y+sy;

			// Adjust x for next character
			x += sx + 1.0f;
		}
		else
		{
			// Jump to next line
			x = 0.0f;
			y += PxReal(mMaxDy) * scale;
		}
	}
	return Nb;
}

#include "PsFile.h"

#if defined (PX_X86) || defined (PX_X64)
static const bool gFlip = false;
#elif defined(PX_PPC)
static const bool gFlip = true;
#elif defined(PX_PS3)
static const bool gFlip = true;
#elif defined(PX_X360)
static const bool gFlip = true;
#elif defined(PX_WII)
static const bool gFlip = true;
#elif defined(PX_ARM) || defined(PX_A64)
static const bool gFlip = false;
#elif defined(PX_PSP2)
static const bool gFlip = false;
#elif defined(PX_WIIU)
static const bool gFlip = true;
#else
#error Unknown platform!
#endif

PX_INLINE void Flip(PxU32& v)
{
	PxU8* b = (PxU8*)&v;

	PxU8 temp = b[0];
	b[0] = b[3];
	b[3] = temp;
	temp = b[1];
	b[1] = b[2];
	b[2] = temp;
}

PX_INLINE void Flip(PxI32& v)
{
	Flip((PxU32&)v);
}

PX_INLINE void Flip(PxF32& v)
{
	Flip((PxU32&)v);
}

static PxU32 read32(File* fp)
{
	PxU32 data;
	size_t numRead = fread(&data, 1, 4, fp);
	if(numRead != 4) { fclose(fp); return 0; }
	if(gFlip)
		Flip(data);
	return data;
}

bool FntData::Load(Renderer& renderer, const char* filename)
{
	File* fp = NULL;
	Fnd::fopen_s(&fp, filename, "rb");
	if(!fp)
		return false;

	// Init texture
	{
		const PxU32 width	= read32(fp);
		const PxU32 height	= read32(fp);
		PxU8* data = new PxU8[width*height*4];
		const size_t size = width*height*4;
		size_t numRead = fread(data, 1, size, fp);
		if(numRead != size) { fclose(fp); return false; }
		
		/*		if(gFlip)	
		{
		PxU32* data32 = (PxU32*)data;
		for(PxU32 i=0;i<width*height;i++)
		{
		Flip(data32[i]);
		}
		}*/

		RendererTexture2DDesc tdesc;
		tdesc.format	= RendererTexture2D::FORMAT_B8G8R8A8;
		tdesc.width		= width;
		tdesc.height	= height;
		tdesc.filter	= RendererTexture2D::FILTER_ANISOTROPIC;
		tdesc.numLevels	= 1;
		/*
		tdesc.filter;
		tdesc.addressingU;
		tdesc.addressingV;
		tdesc.renderTarget;
		*/
		PX_ASSERT(tdesc.isValid());
		mTexture = renderer.createTexture2D(tdesc);
		PX_ASSERT(mTexture);
		if(!mTexture)
		{
			DELETEARRAY(data);
			fclose(fp);
			return false;
		}

		const PxU32 componentCount = 4;

		if(mTexture)
		{
			PxU32 pitch = 0;
			void* buffer = mTexture->lockLevel(0, pitch);
			PX_ASSERT(buffer);
			if(buffer)
			{
				PxU8* levelDst			= (PxU8*)buffer;
				const PxU8* levelSrc	= (PxU8*)data;
				const PxU32 levelWidth	= mTexture->getWidthInBlocks();
				const PxU32 levelHeight	= mTexture->getHeightInBlocks();
				PX_ASSERT(levelWidth * mTexture->getBlockSize() <= pitch); // the pitch can't be less than the source row size.
				for(PxU32 row=0; row<levelHeight; row++)
				{ 
					// copy per pixel to handle RBG case, based on component count
					for(PxU32 col=0; col<levelWidth; col++)
					{
						*levelDst++ = levelSrc[0];
						*levelDst++ = levelSrc[1];
						*levelDst++ = levelSrc[2];
						*levelDst++ = levelSrc[3];
						levelSrc += componentCount;
					}
				}
			}
			mTexture->unlockLevel(0);
		}
		DELETEARRAY(data);
	}

	mNbFnts = read32(fp);

	mFnts	= new FntInfo[mNbFnts];
	const size_t size = mNbFnts*sizeof(FntInfo);
	size_t numRead = fread(mFnts, 1, size, fp);
	if(numRead != size) { fclose(fp); return false; }
	if(gFlip)
	{
		for(PxU32 i=0;i<mNbFnts;i++)
		{
			Flip(mFnts[i].u0);
			Flip(mFnts[i].v0);
			Flip(mFnts[i].u1);
			Flip(mFnts[i].v1);
			Flip(mFnts[i].dx);
			Flip(mFnts[i].dy);
		}
	}

	mMaxDx	= read32(fp);
	mMaxDy	= read32(fp);

	const size_t xRefSize = 256*sizeof(PxU8);
	numRead = fread(mXRef, 1, xRefSize, fp);
	if(numRead != xRefSize) { fclose(fp); return false; }

	for (unsigned int c = 0; c < 256; c++)
	{
		if (mFixWidthCharacters[c])
		{
			mMaxDxNumbers = PxMax(mFnts[mXRef[c]].dx, mMaxDxNumbers);
		}
	}

	fclose(fp);
	return true;
}


struct ClipBox
{
	PxReal	mXMin;
	PxReal	mYMin;
	PxReal	mXMax;
	PxReal	mYMax;
};

static bool ClipQuad(Renderer::TextVertex* /*quad*/, const ClipBox& /*clip_box*/)
{
	return true;
}

static bool GenerateTextQuads(	const char* text, PxU32 nb_characters,
	Renderer::TextVertex* fnt_verts, PxU16* fnt_indices, const ClipBox& clip_box, const FntData* fnt_data, PxReal& x, PxReal& y, PxReal scale_x, PxReal scale_y, PxU32 color,
	PxReal* x_min, PxReal* y_min, PxReal* x_max, PxReal* y_max, PxU32* nb_lines, PxU32* nb_active_characters, bool forceFixWidthNumbers)
{
	// Checkings
	if(!text || !fnt_verts || !fnt_indices || !fnt_data)	return false;

	PxReal mX = x;

	//////////

	Renderer::TextVertex*	V = fnt_verts;
	PxU16*					I = fnt_indices;
	PxU16					Offset = 0;
	PxU32					NbActiveCharacters	= 0;	// Number of non-NULL characters (the ones to render)

	PxReal					XMin = 100000.0f, XMax = -100000.0f;
	PxReal					YMin = 100000.0f, YMax = -100000.0f;

	PxU32					NbLines = 1;	// Number of lines

	// Loop through characters
	for(PxU32 j=0;j<nb_characters;j++)
	{
		if(text[j]!='\n')
		{
			PxU32 i = fnt_data->GetXRef()[(size_t)text[j]];
			const FntInfo character = fnt_data->GetFnts()[i];

			// Character size
			const PxReal sx = PxReal(character.dx) * scale_x;
			const PxReal sy = PxReal(character.dy) * scale_y;

			if (forceFixWidthNumbers && fnt_data->IsFixWidthCharacter(text[j]))
			{
				// move forward half the distance
				x += (fnt_data->GetMaxDxNumbers() - character.dx) * scale_x * 0.5f;
			}

			if(text[j]!=' ')
			{
				const PxReal rhw = 1.0f;


				// Initialize the vertices
				V[0].p.x = x;    V[0].p.y = y+sy; V[0].u = character.u0; V[0].v = character.v1;
				V[1].p.x = x;    V[1].p.y = y;    V[1].u = character.u0; V[1].v = character.v0;
				V[2].p.x = x+sx; V[2].p.y = y+sy; V[2].u = character.u1; V[2].v = character.v1;
				V[3].p.x = x+sx; V[3].p.y = y;    V[3].u = character.u1; V[3].v = character.v0;
				V[0].rhw = V[1].rhw = V[2].rhw = V[3].rhw = rhw;
				V[0].p.z = V[1].p.z = V[2].p.z = V[3].p.z = 0.0f;
				V[0].color = V[1].color = V[2].color = V[3].color = color;

				if(ClipQuad(V, clip_box))
				{
					V+=4;

					// Initialize the indices
					*I++ = Offset+0;
					*I++ = Offset+1;
					*I++ = Offset+2;
					*I++ = Offset+2;
					*I++ = Offset+1;
					*I++ = Offset+3;
					Offset+=4;

					NbActiveCharacters++;
				}
			}

			if (forceFixWidthNumbers && fnt_data->IsFixWidthCharacter(text[j]))
			{
				// move forward theo ther half of the distance
				x += (fnt_data->GetMaxDxNumbers() - character.dx) * scale_x * 0.5f;
			}

			//
			if((x+sx)>XMax)	XMax = x+sx;	if(x<XMin)	XMin = x;
			if((y+sy)>YMax)	YMax = y+sy;	if(y<YMin)	YMin = y;

			x += sx + 1.0f;
		}
		else
		{
			// Jump to next line
			x = mX;
			y += PxReal(fnt_data->GetMaxDy()) * scale_y;
			NbLines++;
		}
	}

	if(x_min)					*x_min = XMin;
	if(y_min)					*y_min = YMin;
	if(x_max)					*x_max = XMax;
	if(y_max)					*y_max = YMax;
	if(nb_lines)				*nb_lines = NbLines;
	if(nb_active_characters)	*nb_active_characters = NbActiveCharacters;

	return true;
}

enum RenderTextQuadFlag_
{
	RTQF_ALIGN_LEFT		= 0,
	RTQF_ALIGN_CENTER	= (1<<0),
	RTQF_ALIGN_RIGHT	= (1<<1),
};

struct RenderTextData
{
	RenderTextData(Renderer::TextVertex* pFntVerts,  PxU16* pFntIndices) 
		: mpFntVerts(pFntVerts), mpFntIndices(pFntIndices) { }

	~RenderTextData()
	{
		DELETEARRAY(mpFntVerts);
		DELETEARRAY(mpFntIndices);
	}

	Renderer::TextVertex* mpFntVerts;
	PxU16*                mpFntIndices;

private:
	// Disable default, copy and assign
	RenderTextData();
	RenderTextData(const RenderTextData&);
	void operator=(const RenderTextData&);
};

static void RenderTextQuads(Renderer* textRender,
	const FntData* fnts,
	const char* text, PxReal x, PxReal y, PxU32 text_color, PxU32 shadow_color,
	PxReal scale_x, PxReal scale_y,
	PxU32 align_flags, PxReal max_length,
	PxReal shadow_offset,
	PxReal* nx, PxReal* ny,
	const ClipBox* clip_box,
	PxReal text_y_offset,
	bool use_max_dy,
	bool forceFixWidthNumbers,
	RendererMaterial* material
	)
{
	// We want to render the whole text in one run...
	const PxReal text_x = x;
	const PxReal text_y = y;

	// Compute text size
	PxReal Width, Height;
	const PxU32 NbCharacters = fnts->ComputeSize(text, Width, Height, 1.0f, forceFixWidthNumbers);

	// Prepare clip box
	ClipBox CB;
	if(clip_box)
	{
		CB = *clip_box;
	}
	else
	{
		PxU32 width, height;
		textRender->getWindowSize(width, height);

		const PxReal Margin = 0.0f;
		CB.mXMin = Margin;
		CB.mYMin = Margin;
		CB.mXMax = PxReal(width) - Margin;
		CB.mYMax = PxReal(height) - Margin;
	}

	// Allocate space for vertices
	RenderTextData FntData(new Renderer::TextVertex[NbCharacters*4], new PxU16[NbCharacters*6]);

	// Generate quads
	PxReal XMin, YMin, XMax, YMax;
	PxU32 NbLines, NbActiveCharacters;
	GenerateTextQuads(text, NbCharacters, FntData.mpFntVerts, FntData.mpFntIndices, CB, fnts, x, y, scale_x, scale_y, text_color, &XMin, &YMin, &XMax, &YMax, &NbLines, &NbActiveCharacters, forceFixWidthNumbers);

	for(PxU32 i=0;i<NbActiveCharacters*4;i++)
		FntData.mpFntVerts[i].p.y += text_y_offset;

	if(use_max_dy)
		YMax = YMin + (PxReal)fnts->GetMaxDy();

	const bool centered = (align_flags & RTQF_ALIGN_CENTER)!=0;
	const bool align_right = (align_flags & RTQF_ALIGN_RIGHT)!=0;

	if(centered || align_right)
	{
		const PxReal L = XMax - XMin;
		XMax = XMin + max_length;
		const PxReal Offset = centered ? (-FntData.mpFntVerts[0].p.x + XMin + (max_length - L)*0.5f) :
			(-FntData.mpFntVerts[0].p.x + XMax - L);
		//										(-FntVerts[0].p.x + XMin + (max_length - L));
		for(PxU32 i=0;i<NbActiveCharacters*4;i++)
			FntData.mpFntVerts[i].p.x += Offset;
	}

	textRender->setupTextRenderStates();

	// Handle shadow
	if(shadow_offset!=0.0f)
	{
		// Allocate space for vertices
		RenderTextData SFntData(new Renderer::TextVertex[NbCharacters*4], new PxU16[NbCharacters*6]);

		// Generate quads
		PxReal SXMin, SYMin, SXMax, SYMax;
		PxU32 SNbLines, SNbActiveCharacters;
		PxReal ShX = text_x + shadow_offset;
		PxReal ShY = text_y + shadow_offset;
		GenerateTextQuads(text, NbCharacters, SFntData.mpFntVerts, SFntData.mpFntIndices, CB, fnts, ShX, ShY, scale_x, scale_y, shadow_color, &SXMin, &SYMin, &SXMax, &SYMax, &SNbLines, &SNbActiveCharacters, forceFixWidthNumbers);

		for(PxU32 i=0;i<SNbActiveCharacters*4;i++)
			SFntData.mpFntVerts[i].p.y += text_y_offset;

		if(centered || align_right)
		{
			const PxReal L = SXMax - SXMin;
			SXMax = SXMin + max_length;
			const PxReal Offset = centered ? (-SFntData.mpFntVerts[0].p.x + SXMin + (max_length - L)*0.5f) : 
				(-SFntData.mpFntVerts[0].p.x + SXMax - L);
			for(PxU32 i=0;i<SNbActiveCharacters*4;i++)
				SFntData.mpFntVerts[i].p.x += Offset;
		}

		textRender->renderTextBuffer(SFntData.mpFntVerts, 4*SNbActiveCharacters, SFntData.mpFntIndices, 6*SNbActiveCharacters, material);
	}

	textRender->renderTextBuffer(FntData.mpFntVerts, 4*NbActiveCharacters, FntData.mpFntIndices, 6*NbActiveCharacters, material);

	textRender->resetTextRenderStates();
}


static FntData*	gFntData = NULL;


bool Renderer::initTexter()
{
	if(gFntData)
		return true;

	char filename[1024];
	physx::string::strcpy_s(filename, sizeof(filename), getAssetDir());
	physx::string::strcat_s(filename, sizeof(filename), "fonts/arial_black.bin");

	gFntData = new FntData;
	if(!gFntData->Load(*this, filename))
	{
		closeTexter();
		return false;
	}

	{
		RendererMaterialDesc matDesc;
		matDesc.alphaTestFunc		= RendererMaterial::ALPHA_TEST_ALWAYS;
		matDesc.alphaTestRef		= 0.0f;
		matDesc.type				= RendererMaterial::TYPE_UNLIT;
		matDesc.blending			= true;
		matDesc.srcBlendFunc	    = RendererMaterial::BLEND_SRC_ALPHA;
		matDesc.dstBlendFunc	    = RendererMaterial::BLEND_ONE_MINUS_SRC_ALPHA;
		matDesc.geometryShaderPath	= NULL;
		matDesc.vertexShaderPath	= "vertex/text.cg";
		matDesc.fragmentShaderPath	= "fragment/text.cg";
		PX_ASSERT(matDesc.isValid());

		m_textMaterial = createMaterial(matDesc);
		if(!m_textMaterial)
		{
			closeTexter();
			return false;
		}

		m_textMaterialInstance = new RendererMaterialInstance(*m_textMaterial);
		if(!m_textMaterialInstance)
		{
			closeTexter();
			return false;
		}

		const RendererMaterial::Variable* var = m_textMaterial->findVariable("diffuseTexture", RendererMaterial::VARIABLE_SAMPLER2D);
		if(!var)
		{
			closeTexter();
			return false;
		}
		m_textMaterialInstance->writeData(*var, &(gFntData->mTexture));
	}

	return true;
}

void Renderer::closeTexter()
{
	DELETESINGLE(m_textMaterialInstance);
	SAFE_RELEASE(m_textMaterial);
	DELETESINGLE(gFntData);
}

// splits string by delimeter
#if defined(RENDERER_TABLET)
static std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while(std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

static std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    return split(s, delim, elems);
}
#endif

void SampleRenderer::Renderer::print(PxU32* x, PxU32* y, const char** text, PxU32 textCount, PxReal scale, PxReal shadowOffset, RendererColor* textColors, bool forceFixWidthNumbers)
{
	// we split string containing '\n' in a vector of strings. This prevents from need to have a really big vertex/index buffer for text in GLES2 renderer
#if defined(RENDERER_TABLET)
	if(std::string(*text).find('\n') != std::string::npos) {
		std::vector<std::string> strings = split(*text, '\n');
		PxU32 nx = *x, ny = *y;
		for(PxU32 i = 0; i < strings.size(); ++i)
		{
			const char* sptr = strings[i].c_str();
			print(&nx, &ny, &sptr, 1, scale, shadowOffset, textColors, forceFixWidthNumbers);
			ny += TEXT_HEIGHT + TEXT_VERTICAL_SPACING;
		}
		return;
	}
#endif

	if(!gFntData || !gFntData->mTexture || !m_textMaterial || !m_textMaterialInstance || 0==textCount || NULL==x || NULL==y || NULL==text || 0 == *text || (*text)[0] == '\0')
		return;

	ScopedRender renderSection(*this);
	if (renderSection)
	{
		m_textMaterial->bind(RendererMaterial::PASS_UNLIT, m_textMaterialInstance, false);

		if(!m_useShadersForTextRendering)
		{
			m_textMaterial->unbind();
			gFntData->mTexture->select(0);
		}

		const PxU32 alignFlags  = 0;
		const float maxLength   = 0.0f;
		float* nx               = NULL;
		float* ny               = NULL;
		const ClipBox* clipBox  = NULL;
		const float textYOffset = 0.0f;
		const bool useMaxDy     = false;
		const RendererColor defaultColor(255, 255, 255, 255);

#if defined(RENDERER_TABLET)
		shadowOffset = 0.0f;
#endif	

		for (PxU32 i = 0; i < textCount; ++i)
		{
			const PxU32 color       = convertColor(textColors ? textColors[i] : defaultColor);
			const PxU32 shadowColor = convertColor(textColors ? RendererColor(0,0,0,textColors[i].a) : RendererColor(0,0,0,defaultColor.a));

			RenderTextQuads(this,
				gFntData,
				text[i],
				PxReal(x[i]), PxReal(y[i]),
				color, shadowColor,
				scale, scale,
				alignFlags,
				maxLength,
				shadowOffset * scale,
				nx, ny,
				clipBox,
				textYOffset,
				useMaxDy,
				forceFixWidthNumbers,
				m_textMaterial
				);
		}

		if(m_useShadersForTextRendering)
			m_textMaterial->unbind();
	}
}

bool SampleRenderer::Renderer::captureScreen(const char* filename)
{
	if(!filename)
		return false;

	physx::PxU32 width, height, sizeInBytes;
	const void* data = NULL;
	if (captureScreen(width, height, sizeInBytes, data) && sizeInBytes > 0)
	{
		PxDefaultFileOutputStream fileBuffer(filename);
		return fileBuffer.write(data, sizeInBytes) > 0;
	}
	else
	{
		return false;
	}
}


void Renderer::print(PxU32 x, PxU32 y, const char* text, PxReal scale, PxReal shadowOffset, RendererColor textColor, bool forceFixWidthNumbers)
{
	if (NULL == text || strlen(text) <= 0)
		return;
	print(&x, &y, &text, 1, scale, shadowOffset, &textColor, forceFixWidthNumbers);
}

///////////////////////////////////////////////////////////////////////////////

bool Renderer::initScreenquad()
{
	RendererMaterialDesc matDesc;
	matDesc.alphaTestFunc		= RendererMaterial::ALPHA_TEST_ALWAYS;
	matDesc.alphaTestRef		= 0.0f;
	matDesc.type				= RendererMaterial::TYPE_UNLIT;
	matDesc.blending			= false;
	matDesc.vertexShaderPath	= "vertex/screenquad.cg";
	matDesc.fragmentShaderPath	= "fragment/screenquad.cg";	
	matDesc.geometryShaderPath	= NULL;
#if defined(RENDERER_TABLET)
	matDesc.srcBlendFunc		= RendererMaterial::BLEND_SRC_ALPHA;
	matDesc.dstBlendFunc		= RendererMaterial::BLEND_ONE_MINUS_SRC_ALPHA;
#else
	matDesc.srcBlendFunc		= RendererMaterial::BLEND_ONE;
	matDesc.dstBlendFunc		= RendererMaterial::BLEND_ONE;
#endif
	PX_ASSERT(matDesc.isValid());

	m_screenquadOpaqueMaterial = createMaterial(matDesc);
	if(!m_screenquadOpaqueMaterial)
		{
		closeScreenquad();
		return false;
	}

	m_screenquadOpaqueMaterialInstance = new RendererMaterialInstance(*m_screenquadOpaqueMaterial);
	if(!m_screenquadOpaqueMaterialInstance)
	{
		closeScreenquad();
		return false;
	}

	matDesc.blending		= true;
	matDesc.srcBlendFunc	= RendererMaterial::BLEND_SRC_ALPHA;
	matDesc.dstBlendFunc	= RendererMaterial::BLEND_ONE_MINUS_SRC_ALPHA;

	m_screenquadAlphaMaterial = createMaterial(matDesc);
	if(!m_screenquadAlphaMaterial)
		{
		closeScreenquad();
		return false;
	}

	m_screenquadAlphaMaterialInstance = new RendererMaterialInstance(*m_screenquadAlphaMaterial);
	if(!m_screenquadAlphaMaterialInstance )
	{
		closeScreenquad();
		return false;
	}

	return true;
}

#if defined(RENDERER_TABLET)
void Renderer::setControlPosition(int ctrl_idx, const PxVec2& pos)
{
	if(ctrl_idx < CONTROL_COUNT)
	{
		m_controlPos[ctrl_idx] = pos;
	}
}

#include "foundation/PxBounds3.h"

PxBounds3 Renderer::getCenteredControlBounds(int ctrl_idx)
{
	/* Return PxBounds3 with positive volume */
	return PxBounds3(
		PxVec3(m_controlCenteredPos[ctrl_idx].x - m_controlHalfSize.x, m_controlCenteredPos[ctrl_idx].y - m_controlHalfSize.y, -1.0f), 
		PxVec3(m_controlCenteredPos[ctrl_idx].x + m_controlHalfSize.x, m_controlCenteredPos[ctrl_idx].y + m_controlHalfSize.y, 1.0f));
}

PxBounds3 Renderer::getControlBounds(int ctrl_idx)
{
	/* Return PxBounds3 with positive volume */
	return PxBounds3(
		PxVec3(m_controlPos[ctrl_idx].x - m_controlHalfSize.x, m_controlPos[ctrl_idx].y - m_controlHalfSize.y, -1.0f), 
		PxVec3(m_controlPos[ctrl_idx].x + m_controlHalfSize.x, m_controlPos[ctrl_idx].y + m_controlHalfSize.y, 1.0f));
}

PxVec2	Renderer::getControlPosition(int ctrl_idx)
{
	return m_controlPos[ctrl_idx];
}

PxVec2	Renderer::getCenteredControlPosition(int ctrl_idx)
{
	return m_controlCenteredPos[ctrl_idx];
}

bool Renderer::initControls(RendererMaterial* controlMaterial, RendererMaterialInstance* controlMaterialInstance)
{
	// create quad control
	using physx::PxReal;
	using physx::PxU32;
	using physx::PxU8;

	m_controlMaterial = controlMaterial;
	m_controlMaterialInstance = controlMaterialInstance;

	// compute control dimensions and placement according to window aspect
	// screen coordinate range: [-1,1]
	PxVec2 controlSize; 
	PxVec2 controlCenterOffset;
	{
		PxU32 width, height;
		getWindowSize(width, height);
		
		PX_ASSERT(width > 0 && height > 0);
		PX_ASSERT(gControlSizeRelative > 0.0f && gControlSizeRelative < 0.5f);
		PX_ASSERT(gControlMarginRelative > 0.0f && gControlMarginRelative < 0.5f);
		
		//size and margin should be relative to smaller screen dimension 
		if (width > height)
		{
			PxReal aspect = (PxReal)height/(PxReal)width;
			controlSize.y = 2.0f*gControlSizeRelative;
			controlSize.x = controlSize.y * aspect;
			controlCenterOffset.y = 1.0f - controlSize.y*0.5f - 2.0f*gControlMarginRelative;
			controlCenterOffset.x = 1.0f - controlSize.x*0.5f - 2.0f*gControlMarginRelative*aspect;
		}
		else 
		{
			PxReal aspect = (PxReal)width/(PxReal)height;
			controlSize.x = 2.0f*gControlSizeRelative;
			controlSize.y = controlSize.x * aspect;
			controlCenterOffset.x = 1.0f - controlSize.x*0.5f - 2.0f*gControlMarginRelative;
			controlCenterOffset.y = 1.0f - controlSize.y*0.5f - 2.0f*gControlMarginRelative*aspect;
		}
	}
	
	m_controlHalfSize = controlSize*0.5f;
	
	/* Initialize sticks positions - default and current */
	m_controlCenteredPos[0].x = -controlCenterOffset.x;
	m_controlCenteredPos[0].y = -controlCenterOffset.y;
	m_controlCenteredPos[1].x =  controlCenterOffset.x;
	m_controlCenteredPos[1].y = -controlCenterOffset.y;
	m_controlPos[0] = m_controlCenteredPos[0];
	m_controlPos[1] = m_controlCenteredPos[1];

	PxReal controlVertices[] = {  
		-m_controlHalfSize.x, -m_controlHalfSize.y, 0.0f,
		-m_controlHalfSize.x,  m_controlHalfSize.y, 0.0f,
		 m_controlHalfSize.x, -m_controlHalfSize.y, 0.0f,
		 m_controlHalfSize.x,  m_controlHalfSize.y, 0.0f };
	
	PxReal controlTexcoords[] = { 
		0.0f, 0.0f,		// each face
		0.0f, 1.0f,
		1.0f, 0.0f,
		1.0f, 1.0f };

	const PxU32 controlVerticesCount = sizeof(controlVertices) / (sizeof(controlVertices[0]) * 3);

	m_controlMesh[0] = initControl(controlVertices, controlTexcoords, controlVerticesCount);

	for(int i = 0; i < controlVerticesCount; ++i)
		controlVertices[3 * i] = -controlVertices[3 * i];

	m_controlMesh[1] = initControl(controlVertices, controlTexcoords, controlVerticesCount);

	return true;
}

void Renderer::setControlDefaultPosition(int ctrl_idx)
{
	m_controlPos[ctrl_idx] = m_controlCenteredPos[ctrl_idx];
}

RendererMesh* Renderer::initControl(PxReal* controlVertices, PxReal* controlTexcoords, PxU32 count) 
{
	const PxU32 controlVerticesCount = count;

	RendererVertexBufferDesc vbdesc;
	vbdesc.hint = RendererVertexBuffer::HINT_STATIC;
	vbdesc.maxVertices = controlVerticesCount;
	vbdesc.semanticFormats[RendererVertexBuffer::SEMANTIC_POSITION] = RendererVertexBuffer::FORMAT_FLOAT3;
	vbdesc.semanticFormats[RendererVertexBuffer::SEMANTIC_TEXCOORD0] = RendererVertexBuffer::FORMAT_FLOAT2;
	RendererVertexBuffer* mVertexBuffer = createVertexBuffer(vbdesc);
	RendererMesh* controlMesh = NULL;
	if(mVertexBuffer)
	{
		RendererMeshDesc meshdesc;
		meshdesc.primitives			= RendererMesh::PRIMITIVE_TRIANGLE_STRIP;
		meshdesc.vertexBuffers		= &mVertexBuffer;
		meshdesc.numVertexBuffers	= 1;
		meshdesc.firstVertex		= 0;
		meshdesc.numVertices		= mVertexBuffer->getMaxVertices();
		meshdesc.indexBuffer		= NULL;
		meshdesc.firstIndex			= 0;
		meshdesc.numIndices			= 0;
		meshdesc.instanceBuffer		= NULL;
		meshdesc.firstInstance		= 0;
		meshdesc.numInstances		= 0;
		controlMesh = createMesh(meshdesc);
		RENDERER_ASSERT(controlMesh, "Failed to create Mesh.");
	}
	PxU32 positionStride = 0, texcoordStride = 0;
	PxU8* locked_positions = static_cast<PxU8*>(mVertexBuffer->lockSemantic(RendererVertexBuffer::SEMANTIC_POSITION, positionStride));
	PxU8* locked_texcoords = static_cast<PxU8*>(mVertexBuffer->lockSemantic(RendererVertexBuffer::SEMANTIC_TEXCOORD0, texcoordStride));	
	for(PxU32 i = 0; i < controlVerticesCount; ++i, 
		locked_positions += positionStride, 
		locked_texcoords += texcoordStride) 
	{
		memcpy(locked_positions, controlVertices + 3 * i, sizeof(PxReal) * 3);
		memcpy(locked_texcoords, controlTexcoords + 2 * i , sizeof(PxReal) * 2);
	}
	mVertexBuffer->unlockSemantic(RendererVertexBuffer::SEMANTIC_TEXCOORD0);
	mVertexBuffer->unlockSemantic(RendererVertexBuffer::SEMANTIC_POSITION);
	return controlMesh;
}

Renderer::TabletButton::TabletButton() :
	pressedCount(0),
	emulatedKeyCode(0), 
	defaultColor(PxVec4(1.0f, 1.0f, 1.0f, 0.4f)), 
	pressedColor(PxVec4(1.0f, 0.0f, 0.0f, 0.4f)),
	callback(NULL)
{
}


void Renderer::TabletButton::setPressedCount(physx::PxU8 p)
{
	pressedCount = p;
}

void Renderer::TabletButton::incPressed()
{
	pressedCount++; 
}

void Renderer::TabletButton::decPressed()
{
	//sschirm: this caused issues on switching samples, because the OS keeps the touch states.  
	//PX_ASSERT(pressedCount);
	if(pressedCount > 0)
		pressedCount--;
}
			
void Renderer::addButton(const PxVec2& leftBottom, const PxVec2& rightTop, void (*func_ptr)(), RendererMaterial* controlMaterial, 
							RendererMaterialInstance* controlMaterialInstance)
{
	/* Create button object and save into the vector */
	TabletButton button;
	button.leftBottom = leftBottom;
	button.rightTop = rightTop;
	button.emulatedKeyCode = 0;
	button.text = "Empty";
	button.callback = func_ptr;
	/* Create graphic representation of the button. That is, quad. */
	PxReal buttonVertices[] = {  leftBottom.x,  leftBottom.y, 0.0f,
											 leftBottom.x,  rightTop.y, 0.0f,
											 rightTop.x, leftBottom.y, 0.0f,
											 rightTop.x, rightTop.y, 0.0f };
	
	/* Use full texture */
	PxReal buttonTexcoords[] = { 0.0f, 0.0f,
												0.0f, 1.0f,
												1.0f, 0.0f,
												1.0f, 1.0f};	
	const PxU32 buttonVerticesCount = sizeof(buttonVertices) / (sizeof(buttonVertices[0]) * 3);												
	button.mesh = initControl(buttonVertices, buttonTexcoords, buttonVerticesCount);
	button.material = controlMaterial;
	button.materialInstance = controlMaterialInstance;
	button.setPressedCount(0); 

	m_buttons.push_back(button);
}

void Renderer::releaseAllButtons()
{
	m_buttons.clear();
}

void Renderer::bindButtonToUserInput(size_t buttonIndex, physx::PxU16 userInputId, const char* buttonName)
{
	PX_ASSERT(m_buttons.size() > buttonIndex);

	TabletButton& button = m_buttons[buttonIndex];
	button.emulatedKeyCode = userInputId;
	button.text = buttonName;
}
#endif // RENDERER_TABLET


void Renderer::closeScreenquad()
{
	DELETESINGLE(m_screenquadAlphaMaterialInstance);
	SAFE_RELEASE(m_screenquadAlphaMaterial);
	DELETESINGLE(m_screenquadOpaqueMaterialInstance);
	SAFE_RELEASE(m_screenquadOpaqueMaterial);
}

ScreenQuad::ScreenQuad() :
	mLeftUpColor		(0xffffffff),
	mLeftDownColor		(0xffffffff),
	mRightUpColor		(0xffffffff),
	mRightDownColor		(0xffffffff),
	mAlpha				(1.0f),
	mX0					(0.0f),
	mY0					(0.0f),
	mX1					(1.0f),
	mY1					(1.0f)
{
}

bool Renderer::drawScreenQuad(const ScreenQuad& screenQuad)
{
	RendererMaterial* screenquadMaterial;
	RendererMaterialInstance* screenquadMaterialInstance;
	if(screenQuad.mAlpha==1.0f)
	{
		screenquadMaterial			= m_screenquadOpaqueMaterial;
		screenquadMaterialInstance	= m_screenquadOpaqueMaterialInstance;
	}
	else
	{
		screenquadMaterial			= m_screenquadAlphaMaterial;
		screenquadMaterialInstance	= m_screenquadAlphaMaterialInstance;
	}

	if(!screenquadMaterialInstance || !screenquadMaterial)
		return false;

	screenquadMaterial->bind(RendererMaterial::PASS_UNLIT, screenquadMaterialInstance, false);
	if(!m_useShadersForTextRendering)
		screenquadMaterial->unbind();

	ScopedRender renderSection(*this);
	if(renderSection)
	{
		setupScreenquadRenderStates();

		TextVertex Verts[4];
		const PxU16 Indices[6] = { 0,1,2,2,1,3 };

		PxU32 renderWidth, renderHeight;
		getWindowSize(renderWidth, renderHeight);

		// Initalize the vertices
		const PxReal xCoeff	= PxReal(renderWidth);
		const PxReal yCoeff	= PxReal(renderHeight);
		const PxReal x0		= screenQuad.mX0 * xCoeff;
		const PxReal y0		= screenQuad.mY0 * yCoeff;
		const PxReal sx		= screenQuad.mX1 * xCoeff;
		const PxReal sy		= screenQuad.mY1 * yCoeff;
		const PxReal rhw	= 1.0f;
		const PxReal z		= 0.0f;

		RendererColor leftUpColor		= screenQuad.mLeftUpColor;
		RendererColor leftDownColor		= screenQuad.mLeftDownColor;
		RendererColor rightUpColor		= screenQuad.mRightUpColor;
		RendererColor rightDownColor	= screenQuad.mRightDownColor;
		const PxU8 alpha = PxU8(screenQuad.mAlpha*255.0f);
		leftUpColor.a = alpha;
		leftDownColor.a = alpha;
		rightUpColor.a = alpha;
		rightDownColor.a = alpha;

		Verts[0].p	= PxVec3(x0, sy, z);		Verts[0].rhw	= rhw;		Verts[0].color	= convertColor(leftDownColor);
		Verts[1].p	= PxVec3(x0, y0, z);		Verts[1].rhw	= rhw;		Verts[1].color	= convertColor(leftUpColor);
		Verts[2].p	= PxVec3(sx, sy, z);		Verts[2].rhw	= rhw;		Verts[2].color	= convertColor(rightDownColor);
		Verts[3].p	= PxVec3(sx, y0, z);		Verts[3].rhw	= rhw;		Verts[3].color	= convertColor(rightUpColor);
#if !(defined(RENDERER_WINMODERN) && defined(RENDERER_TABLET))
		renderTextBuffer(Verts, 4, Indices, 6, screenquadMaterial);	
#endif
		resetScreenquadRenderStates();
	}

	if(m_useShadersForTextRendering)
		screenquadMaterial->unbind();

	return true;
}

bool Renderer::drawTouchControls()
{
#if defined(RENDERER_TABLET)
	renderControls();
	renderButtons();	
#endif	
	return true;
}

#ifdef RENDERER_TABLET
void Renderer::renderControls()
{	
#ifdef RENDERER_WINMODERN	
	for(int i = 0; i < PX_ARRAY_SIZE(m_controlMesh); ++i) 
	{		
		m_controlMaterial->bind(RendererMaterial::PASS_UNLIT, m_controlMaterialInstance, false);
		RendererMeshContext ctx;
		PxReal zero = 0;
		physx::PxMat44 ctrl_transform(PxVec4(m_controlPos[i].x, zero, zero, zero), 
			PxVec4(m_controlPos[i].y, zero, zero, zero),
			PxVec4(zero, zero, zero, zero),
			PxVec4(zero, zero, zero, zero));
		ctx.cullMode = RendererMeshContext::NONE;
		ctx.transform = &ctrl_transform;		
		bindMeshContext(ctx);
		m_controlMaterial->bindMeshState(false);
		m_controlMesh[i]->bind();
		m_controlMesh[i]->render(m_controlMaterial);
		m_controlMesh[i]->unbind();		
		m_controlMaterial->unbind();
	}	

#else
	m_controlMaterial->bind(RendererMaterial::PASS_UNLIT, m_controlMaterialInstance, false);
	for(int i = 0; i < PX_ARRAY_SIZE(m_controlMesh); ++i) 
	{
		RendererMeshContext ctx;
		PxReal zero = 0;
		physx::PxMat44 ctrl_transform(PxVec4(m_controlPos[i].x, zero, zero, zero), 
									PxVec4(m_controlPos[i].y, zero, zero, zero),
									PxVec4(m_controlPos[i].x, zero, zero, zero),
									PxVec4(zero, zero, zero, zero));
		ctx.cullMode = RendererMeshContext::NONE;
		ctx.transform = &ctrl_transform;
		bindMeshContext(ctx);
		m_controlMesh[i]->bind();
		m_controlMesh[i]->render(m_controlMaterial);
		m_controlMesh[i]->unbind();
	}
	m_controlMaterial->unbind();
#endif
}

std::vector<Renderer::TabletButton>& Renderer::screenButtons()
{
	return m_buttons;
}

void Renderer::renderButtons()
{
	if(m_buttons.size())
	{
		for(PxU32 i = 0; i < m_buttons.size(); ++i) 
		{
			// if the button is not used do not draw it
			if(m_buttons[i].emulatedKeyCode == 0)
				continue;

			const PxVec4& color = m_buttons[i].pressedCount ? m_buttons[i].pressedColor : m_buttons[i].defaultColor;
			const RendererMaterial::Variable* var = m_buttons[i].materialInstance->findVariable("diffuseColor", RendererMaterial::VARIABLE_FLOAT4);
			if(var)
			{
				const PxReal data[] = { color.x, color.y, color.z, color.w };
				m_buttons[i].materialInstance->writeData(*var, data);
			}

			m_buttons[i].material->bind(RendererMaterial::PASS_UNLIT, m_buttons[i].materialInstance, false);
			RendererMeshContext ctx;
			physx::PxMat44 ctrl_transform(PxVec4(0.0f));
			ctx.cullMode = RendererMeshContext::NONE;
			ctx.transform = &ctrl_transform;
			
			bindMeshContext(ctx);

			m_buttons[i].mesh->bind();
			m_buttons[i].mesh->render(m_buttons[i].material);
			m_buttons[i].mesh->unbind();
			m_buttons[i].material->unbind();
		}
		for(PxU32 i = 0; i < m_buttons.size(); ++i) 
		{
			if(m_buttons[i].emulatedKeyCode == 0)
				continue;

			/* TODO: It seems that characters has variable width, so this code is not entirely valid, 
				even though it makes it look better than just printing text starting from 
				the center of the button */
			const RendererColor textColor(255, 255, 255, 255);
			const PxReal TEXT_CHARACTER_WIDTH = 12.0f;
			const PxReal TEXT_CHARACTER_HEIGHT = 12.0f;
			/* Convert relative buttons coordinates to the absolute screen coordinates */
			PxU32 width, height;
			getWindowSize(width, height);
			PxVec2 absoluteLeftBottom = PxVec2((m_buttons[i].leftBottom.x + 1.0f) * ((PxReal)width / 2.0f), -(m_buttons[i].leftBottom.y - 1.0f) * ((PxReal)height / 2.0f));
			PxVec2 absoluteRightTop = PxVec2((m_buttons[i].rightTop.x + 1.0f) * ((PxReal)width / 2.0f), -(m_buttons[i].rightTop.y - 1.0f) * ((PxReal)height / 2.0f));
			
			PxVec2 absoluteCenter = (absoluteLeftBottom + absoluteRightTop) / 2.0f;
			PxReal absoluteWidth = absoluteRightTop.x - absoluteLeftBottom.x;
			
			/* Leave empty space for half character near edges of the button */
			PxU8 characterToFit = (PxU8) (absoluteWidth / TEXT_CHARACTER_WIDTH - 1);
			
			std::string text;
			PxVec2 textPos;
			/* Shrink text if not enough space and decide where to start printing */
			if(characterToFit < m_buttons[i].text.size()) 
			{
				textPos.x = absoluteLeftBottom.x + TEXT_CHARACTER_WIDTH / 2.0f;
				text = m_buttons[i].text.substr(0, characterToFit);
			}
			else 
			{
				textPos.x = absoluteCenter.x - ((PxReal)m_buttons[i].text.size() / 2.0f) * TEXT_CHARACTER_WIDTH;
				text = m_buttons[i].text;
			}
			textPos.y = absoluteCenter.y - TEXT_CHARACTER_HEIGHT / 2.0f;
			
			
			print((PxU32) textPos.x,(PxU32) textPos.y, text.c_str(), 0.5f, 6.0f, textColor, true);
		}
	}
}
#endif

bool Renderer::drawLines2D(PxU32 nbVerts, const PxReal* vertices, const RendererColor& color)
{
	RendererMaterial* screenquadMaterial;
	RendererMaterialInstance* screenquadMaterialInstance;
	screenquadMaterial			= m_screenquadOpaqueMaterial;
	screenquadMaterialInstance	= m_screenquadOpaqueMaterialInstance;
	if(!screenquadMaterialInstance || !screenquadMaterial)
		return false;

	screenquadMaterial->bind(RendererMaterial::PASS_UNLIT, screenquadMaterialInstance, false);
	if(!m_useShadersForTextRendering)
		screenquadMaterial->unbind();

	setupScreenquadRenderStates();

	TextVertex* verts = new TextVertex[nbVerts];

	PxU32 renderWidth, renderHeight;
	getWindowSize(renderWidth, renderHeight);
	const PxReal xCoeff	= PxReal(renderWidth);
	const PxReal yCoeff	= PxReal(renderHeight);

	const PxU32 convertedColor = convertColor(color);

	for(PxU32 i=0;i<nbVerts;i++)
	{
		verts[i].p.x	= vertices[i*2+0] * xCoeff;
		verts[i].p.y	= vertices[i*2+1] * yCoeff;
		verts[i].p.z	= 0.0f;
		verts[i].rhw	= 1.0f;
		verts[i].color	= convertedColor;
		verts[i].u		= 0.0f;
		verts[i].v		= 0.0f;
	}

#ifdef RENDERER_PSP2
	renderLines2D(verts, nbVerts, screenquadMaterial);
#else
	renderLines2D(verts, nbVerts);
#endif

	DELETEARRAY(verts);

	resetScreenquadRenderStates();
	if(m_useShadersForTextRendering)
		screenquadMaterial->unbind();

	return true;
}

bool Renderer::drawLines2D(PxU32 nbVerts, const PxReal* vertices, const RendererColor* colors)
{
	RendererMaterial* screenquadMaterial;
	RendererMaterialInstance* screenquadMaterialInstance;
	screenquadMaterial			= m_screenquadOpaqueMaterial;
	screenquadMaterialInstance	= m_screenquadOpaqueMaterialInstance;
	if(!screenquadMaterialInstance || !screenquadMaterial)
		return false;

	screenquadMaterial->bind(RendererMaterial::PASS_UNLIT, screenquadMaterialInstance, false);
	if(!m_useShadersForTextRendering)
		screenquadMaterial->unbind();

	setupScreenquadRenderStates();

	TextVertex* verts = new TextVertex[nbVerts];

	PxU32 renderWidth, renderHeight;
	getWindowSize(renderWidth, renderHeight);
	const PxReal xCoeff	= PxReal(renderWidth);
	const PxReal yCoeff	= PxReal(renderHeight);

	for(PxU32 i=0;i<nbVerts;i++)
	{
		verts[i].p.x	= vertices[i*2+0] * xCoeff;
		verts[i].p.y	= vertices[i*2+1] * yCoeff;
		verts[i].p.z	= 0.0f;
		verts[i].rhw	= 1.0f;
		verts[i].color	= convertColor(colors[i]);
		verts[i].u		= 0.0f;
		verts[i].v		= 0.0f;
	}

#ifdef RENDERER_PSP2
	renderLines2D(verts, nbVerts, screenquadMaterial);
#else
	renderLines2D(verts, nbVerts);
#endif

	DELETEARRAY(verts);

	resetScreenquadRenderStates();
	if(m_useShadersForTextRendering)
		screenquadMaterial->unbind();

	return true;
}


physx::PxProfileEventSender* Renderer::getEventBuffer() 
{ 
#ifdef PHYSX_PROFILE_SDK
	if ( mProfileZone ) return mProfileZone; 
#endif
	return NULL; 
}
