#pragma once
#include <wx/wx.h>

class ResponsiveImageFrame : public wxFrame {
public:
    ResponsiveImageFrame(const wxString& title, const wxImage& image);

private:
    wxBitmap m_bitmap;
    wxPanel* m_panel;

    void OnSize(wxSizeEvent& event);
    void OnPaint(wxPaintEvent& event);
};

void ShowStandaloneImage(const wxImage& image);