#pragma once

using namespace System;

class DiligentNetNative;

namespace DiligentNetCLR 
{
	public ref class DiligentNetEngine
	{
	private:
		DiligentNetNative* m_nativeEngine;

	public:
		DiligentNetEngine();
		void Initialize(IntPtr windowHandle);
		void Render();
		void Resize(unsigned int width, unsigned int height);
	};
}
