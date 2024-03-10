#include "pch.h"

#include "DiligentNet.CLR.h"
#include "../DiligentNet.Native/DiligentNet.Native.h"

namespace DiligentNetCLR 
{
	DiligentNetEngine::DiligentNetEngine()
	{
		m_nativeEngine = new DiligentNetNative();
    }

	void DiligentNetEngine::Initialize(IntPtr windowHandle)
	{
		m_nativeEngine->Initialize(windowHandle.ToPointer());
	}

	void DiligentNetEngine::Render()
	{
		m_nativeEngine->Render();
	}

	void DiligentNetEngine::Resize(unsigned int width, unsigned int height)
	{
		m_nativeEngine->Resize(width, height);
	}
}
