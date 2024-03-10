using System;
using System.Windows;
using System.Windows.Media;
using DiligentNetCLR;

namespace DiligentNet
{
    public sealed class DiligentNetViewport : Win32HwndControl
    {
        private bool m_allowRendering;

        private readonly DiligentNetEngine m_engine = new DiligentNetEngine();

        protected override void Initialize()
        {
            m_engine.Initialize(Hwnd);

            m_allowRendering = true;
            CompositionTarget.Rendering += OnCompositionTargetRendering;
        }

        protected override void Uninitialize()
        {
            m_allowRendering = false;
            CompositionTarget.Rendering -= OnCompositionTargetRendering;
        }

        private double GetDpiScale()
        {
            var source = PresentationSource.FromVisual(this);
            return source?.CompositionTarget == null
                ? default
                : source.CompositionTarget.TransformToDevice.M11;
        }

        protected override void Resized()
        {
            var scale = GetDpiScale();
            m_engine.Resize((uint)(ActualWidth * scale), (uint)(ActualHeight * scale));
        }

        private void OnCompositionTargetRendering(object sender, EventArgs eventArgs)
        {
            if (!m_allowRendering)
            {
                return;
            }

            Render();
        }

        private void Render()
        {
            m_engine.Render();
        }
    }
}