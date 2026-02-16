#include "App.h"
#include "MainFrame.h"
#include <wx/wx.h>

wxIMPLEMENT_APP(App);

bool App::OnInit() {
	MainFrame* mainFrame = new MainFrame("Genzo Image Converter");
	mainFrame->SetClientSize(640, 240);
	mainFrame->Center();
	mainFrame->Show();
	return true;
}