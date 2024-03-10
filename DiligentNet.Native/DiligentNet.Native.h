#pragma once

#include "pch.h"

class DiligentNetNativeImpl;

class DiligentNetNative
{
private:
	DiligentNetNativeImpl* m_impl;

public:
	DiligentNetNative();
	void Initialize(void* hWnd);
	void Render();
	void Resize(unsigned int width, unsigned int  height);
};
