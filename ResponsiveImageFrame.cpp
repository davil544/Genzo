#include "ResponsiveImageFrame.h"
#include <wx/graphics.h>

ResponsiveImageFrame::ResponsiveImageFrame(wxFrame* parent, const wxString& title, const AnimatedImage& animation)
    : wxFrame(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE) {
    if (parent && parent->GetIcon().IsOk()) {
        SetIcon(parent->GetIcon());
    }

    m_animation = animation;
    m_currentFrame = 0;
    m_currentLoop = 0;
    m_paused = false;

    m_panel = new wxPanel(this, wxID_ANY);
    m_panel->SetBackgroundColour(wxColour(30, 30, 30));

    m_panel->SetDoubleBuffered(true);

    SetMinSize(wxSize(200, 200));

    m_panel->Bind(wxEVT_SIZE, &ResponsiveImageFrame::OnSize, this);
    m_panel->Bind(wxEVT_PAINT, &ResponsiveImageFrame::OnPaint, this);
    m_panel->Bind(wxEVT_LEFT_DOWN, &ResponsiveImageFrame::OnClick, this);

    m_timer.Bind(wxEVT_TIMER, &ResponsiveImageFrame::OnTimer, this);

    if (m_animation.IsAnimated()) {
        StartTimer();
    }
}

void ResponsiveImageFrame::OnSize(wxSizeEvent& event) {
    m_panel->Refresh();
    event.Skip();
}

void ResponsiveImageFrame::OnPaint(wxPaintEvent& event) {
    wxPaintDC dc(m_panel);

    if (!m_animation.IsOk()) return;

    const wxImage& frame = m_animation.frames[m_currentFrame].image;
    if (!frame.IsOk()) return;

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);

    if (gc) {
        wxSize panelSize = m_panel->GetClientSize();
        double targetW = panelSize.GetWidth();
        double targetH = panelSize.GetHeight();

        double origW = frame.GetWidth();
        double origH = frame.GetHeight();

        double scaleW = targetW / origW;
        double scaleH = targetH / origH;
        double scale = std::min(scaleW, scaleH);

        double finalW = origW * scale;
        double finalH = origH * scale;

        double x = (targetW - finalW) / 2.0;
        double y = (targetH - finalH) / 2.0;

        gc->DrawBitmap(wxBitmap(frame), x, y, finalW, finalH);

        delete gc;
    }
}

void ResponsiveImageFrame::OnTimer(wxTimerEvent& event) {
    if (m_animation.frames.size() <= 1) return;

    m_currentFrame++;
    if (m_currentFrame >= static_cast<int>(m_animation.frames.size())) {
        m_currentFrame = 0;
        m_currentLoop++;
        if (m_animation.loopCount > 0 && m_currentLoop >= m_animation.loopCount) {
            StopTimer();
            m_paused = true;
            m_panel->Refresh();
            return;
        }
    }

    m_panel->Refresh();
    StartTimer();
}

void ResponsiveImageFrame::OnClick(wxMouseEvent& event) {
    if (m_animation.frames.size() <= 1) return;
    m_paused = !m_paused;
    if (m_paused) {
        StopTimer();
    }
    else {
        StartTimer();
    }
    event.Skip();
}

void ResponsiveImageFrame::StartTimer() {
    if (m_animation.frames.size() <= 1) return;
    int delay = m_animation.frames[m_currentFrame].delayMs;
    if (delay < 10) delay = 10;
    m_timer.Start(delay, false);
}

void ResponsiveImageFrame::StopTimer() {
    m_timer.Stop();
}

void ShowStandaloneImage(wxFrame* parent, const AnimatedImage& animation, ResponsiveImageFrame*& previewFrame) {
    if (!animation.IsOk()) {
        wxMessageBox("The provided image is invalid.", "Error", wxICON_ERROR | wxOK);
        return;
    }

    if (previewFrame) {
        previewFrame->UpdateAnimation(animation);
        previewFrame->Raise();
    }
    else {
        previewFrame = new ResponsiveImageFrame(parent, "Image Viewer", animation);
        previewFrame->Bind(wxEVT_CLOSE_WINDOW, [&previewFrame](wxCloseEvent& event) {
            previewFrame = nullptr;
            event.Skip();
            });
        previewFrame->Show(true);
    }
}

void ResponsiveImageFrame::UpdateAnimation(const AnimatedImage& animation) {
    m_animation = animation;
    m_currentFrame = 0;
    m_currentLoop = 0;
    m_paused = false;
    m_panel->Refresh();
    if (m_animation.IsAnimated()) {
        StartTimer();
    }
    else {
        StopTimer();
    }
}