#pragma once
#include <wx/wx.h>

class ResponsiveImageFrame : public wxFrame {
public:
    ResponsiveImageFrame(wxWindow* parent, const wxString& title, const wxImage& image);

private:
    wxBitmap m_bitmap;
    wxPanel* m_panel;

    void OnSize(wxSizeEvent& event);
    void OnPaint(wxPaintEvent& event);
};

void ShowStandaloneImage(wxWindow* parent, const wxImage& image);