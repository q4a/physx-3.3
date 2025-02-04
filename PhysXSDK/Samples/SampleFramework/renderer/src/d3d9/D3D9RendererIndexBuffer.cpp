/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <RendererConfig.h>

#if defined(RENDERER_ENABLE_DIRECT3D9)

#include "D3D9RendererIndexBuffer.h"
#include <RendererIndexBufferDesc.h>

#if defined(PX_WINDOWS)
#include <pxtask/PxCudaContextManager.h>
#endif

using namespace SampleRenderer;

static D3DFORMAT getD3D9Format(RendererIndexBuffer::Format format)
{
	D3DFORMAT d3dFormat = D3DFMT_UNKNOWN;
	switch(format)
	{
	case RendererIndexBuffer::FORMAT_UINT16: d3dFormat = D3DFMT_INDEX16; break;
	case RendererIndexBuffer::FORMAT_UINT32: d3dFormat = D3DFMT_INDEX32; break;
	}
	RENDERER_ASSERT(d3dFormat != D3DFMT_UNKNOWN, "Unable to convert to D3DFORMAT.");
	return d3dFormat;
}

D3D9RendererIndexBuffer::D3D9RendererIndexBuffer(IDirect3DDevice9 &d3dDevice, D3D9Renderer& renderer, const RendererIndexBufferDesc &desc) :
	RendererIndexBuffer(desc),
	m_d3dDevice(d3dDevice)
{
	m_d3dIndexBuffer = 0;

	m_usage      = D3DUSAGE_WRITEONLY;
	m_pool       = renderer.canUseManagedResources() ? D3DPOOL_MANAGED : D3DPOOL_DEFAULT;
	PxU32	indexSize  = getFormatByteSize(desc.format);
	m_format     = getD3D9Format(desc.format);
	m_bufferSize = indexSize * desc.maxIndices;

#if RENDERER_ENABLE_DYNAMIC_VB_POOLS
	if(desc.hint == RendererIndexBuffer::HINT_DYNAMIC )
	{
		m_usage = desc.registerInCUDA ? 0 : D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY;
		m_pool  = D3DPOOL_DEFAULT;
	}
#endif

	onDeviceReset();

	if(m_d3dIndexBuffer)
	{
		m_maxIndices = desc.maxIndices;
	}
}

D3D9RendererIndexBuffer::~D3D9RendererIndexBuffer(void)
{
	if(m_d3dIndexBuffer)
	{
#if defined(PX_WINDOWS) && PX_SUPPORT_GPU_PHYSX
		if(m_interopContext && m_registeredInCUDA)
		{
			m_registeredInCUDA = !m_interopContext->unregisterResourceInCuda(m_InteropHandle);
		}
#endif
		m_d3dIndexBuffer->Release();
	}
}

void D3D9RendererIndexBuffer::onDeviceLost(void)
{
	m_registeredInCUDA = false;

	if(m_pool != D3DPOOL_MANAGED && m_d3dIndexBuffer)
	{
#if defined(PX_WINDOWS) && PX_SUPPORT_GPU_PHYSX
		if(m_interopContext && m_registeredInCUDA)
		{
			m_registeredInCUDA = !m_interopContext->unregisterResourceInCuda(m_InteropHandle);
		}
#endif
		m_d3dIndexBuffer->Release();
		m_d3dIndexBuffer = 0;
	}
}

void D3D9RendererIndexBuffer::onDeviceReset(void)
{
	if(!m_d3dIndexBuffer)
	{
		m_d3dDevice.CreateIndexBuffer(m_bufferSize, m_usage, m_format, m_pool, &m_d3dIndexBuffer, 0);
		RENDERER_ASSERT(m_d3dIndexBuffer, "Failed to create Direct3D9 Index Buffer.");
#if defined(PX_WINDOWS) && PX_SUPPORT_GPU_PHYSX
		if(m_interopContext && m_d3dIndexBuffer && m_mustBeRegisteredInCUDA)
		{
			m_registeredInCUDA = m_interopContext->registerResourceInCudaD3D(m_InteropHandle, m_d3dIndexBuffer);
		}
#endif
	}
}

void *D3D9RendererIndexBuffer::lock(void)
{
	void *buffer = 0;
	if(m_d3dIndexBuffer)
	{
		const Format format     = getFormat();
		const PxU32  maxIndices = getMaxIndices();
		const PxU32  bufferSize = maxIndices * getFormatByteSize(format);
		if(bufferSize > 0)
		{
			m_d3dIndexBuffer->Lock(0, (UINT)bufferSize, &buffer, 0);
		}
	}
	return buffer;
}

void D3D9RendererIndexBuffer::unlock(void)
{
	if(m_d3dIndexBuffer)
	{
		m_d3dIndexBuffer->Unlock();
	}
}

void D3D9RendererIndexBuffer::bind(void) const
{
	m_d3dDevice.SetIndices(m_d3dIndexBuffer);
}

void D3D9RendererIndexBuffer::unbind(void) const
{
	m_d3dDevice.SetIndices(0);
}

#endif // #if defined(RENDERER_ENABLE_DIRECT3D9)
