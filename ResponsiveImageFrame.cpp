#include "ResponsiveImageFrame.h"
#include <wx/graphics.h>

ResponsiveImageFrame::ResponsiveImageFrame(wxFrame* parent, const wxString& title, const wxImage& image)
    : wxFrame(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE)
{
    if (parent && parent->GetIcon().IsOk()) {
        SetIcon(parent->GetIcon());
    }

    m_bitmap = wxBitmap(image);

    m_panel = new wxPanel(this, wxID_ANY);
    m_panel->SetBackgroundColour(wxColour(30, 30, 30));

    m_panel->SetDoubleBuffered(true);

    SetMinSize(wxSize(200, 200));

    m_panel->Bind(wxEVT_SIZE, &ResponsiveImageFrame::OnSize, this);
    m_panel->Bind(wxEVT_PAINT, &ResponsiveImageFrame::OnPaint, this);
}

void ResponsiveImageFrame::OnSize(wxSizeEvent& event) {
    m_panel->Refresh();
    event.Skip();
}

void ResponsiveImageFrame::OnPaint(wxPaintEvent& event) {
    wxPaintDC dc(m_panel);

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);

    if (gc && m_bitmap.IsOk()) {
        wxSize panelSize = m_panel->GetClientSize();
        double targetW = panelSize.GetWidth();
        double targetH = panelSize.GetHeight();

        double origW = m_bitmap.GetWidth();
        double origH = m_bitmap.GetHeight();

        double scaleW = targetW / origW;
        double scaleH = targetH / origH;
        double scale = std::min(scaleW, scaleH);

        double finalW = origW * scale;
        double finalH = origH * scale;

        double x = (targetW - finalW) / 2.0;
        double y = (targetH - finalH) / 2.0;

        gc->DrawBitmap(m_bitmap, x, y, finalW, finalH);

        delete gc;
    }
}

void ShowStandaloneImage(wxFrame* parent, const wxImage& image, ResponsiveImageFrame*& previewFrame) {
    if (!image.IsOk()) {
        wxMessageBox("The provided image is invalid.", "Error", wxICON_ERROR | wxOK);
        return;
    }

    if (previewFrame) {
        previewFrame->UpdateImage(image);
        previewFrame->Raise();
    }
    else {
        previewFrame = new ResponsiveImageFrame(parent, "Image Viewer", image);
        previewFrame->Bind(wxEVT_CLOSE_WINDOW, [&previewFrame](wxCloseEvent& event) {
            previewFrame = nullptr;
            event.Skip();
            });
        previewFrame->Show(true);
    }
}

void ResponsiveImageFrame::UpdateImage(const wxImage& image) {
    if (!image.IsOk()) return;

    m_bitmap = wxBitmap(image);
    m_panel->Refresh();
}