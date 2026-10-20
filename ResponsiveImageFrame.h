#pragma once
#include <wx/wx.h>
#include "Codec.h"

class ResponsiveImageFrame : public wxFrame {
public:
    ResponsiveImageFrame(wxFrame* parent, const wxString& title, const AnimatedImage& animation);
    void UpdateAnimation(const AnimatedImage& animation);

private:
    AnimatedImage m_animation;
    int m_currentFrame = 0;
    int m_currentLoop = 0;
    bool m_paused = false;
    wxTimer m_timer;
    wxPanel* m_panel;

    void OnSize(wxSizeEvent& event);
    void OnPaint(wxPaintEvent& event);
    void OnTimer(wxTimerEvent& event);
    void OnClick(wxMouseEvent& event);
    void StartTimer();
    void StopTimer();
};

void ShowStandaloneImage(wxFrame* parent, const AnimatedImage& animation, ResponsiveImageFrame*& previewFrame);