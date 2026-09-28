#include "App.h"
#include "MainFrame.h"
#include <libheif/heif.h>
#include <wx/wx.h>

wxIMPLEMENT_APP(App);

bool App::OnInit() {
	heif_init(nullptr);
	MainFrame* mainFrame = new MainFrame("Genzo Image Converter");
	mainFrame->SetClientSize(640, 240);
	mainFrame->Center();
	mainFrame->Show();
	return true;
}

int App::OnExit() {
	heif_deinit();
	return wxApp::OnExit();
}