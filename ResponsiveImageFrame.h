#pragma once
#include <wx/wx.h>

class ResponsiveImageFrame : public wxFrame {
public:
    ResponsiveImageFrame(wxFrame* parent, const wxString& title, const wxImage& image);
    void UpdateImage(const wxImage& image);

private:
    wxBitmap m_bitmap;
    wxPanel* m_panel;

    void OnSize(wxSizeEvent& event);
    void OnPaint(wxPaintEvent& event);
};

void ShowStandaloneImage(wxFrame* parent, const wxImage& image, ResponsiveImageFrame*& previewFrame);