#pragma once
#include <wx/wx.h>

class MainFrame : public wxFrame
{
	public:
		MainFrame(const wxString& title);

	private:
		void CreateControls();
		void SetupSizers();
		wxTextCtrl* textCtrlFileInputPath = nullptr;
		wxPanel* panel;
		wxStaticText* headerText;
		wxStaticText* subheaderText;
		wxStaticText* lblOutputFormat;
		wxButton* btnBrowse;
		wxButton* btnConvert; //TODO: Figure out how to combine these 2 button declarations into 1 statement
		wxArrayString supportedFormats;
		wxChoice* choiceOutputFormat;
		const wxString strWelcome = "Welcome to the Genzo Image Converter!  Please select a file to get started.";

		void OnButtonBrowseClick(wxCommandEvent& event);
		void OnButtonConvertClick(wxCommandEvent& event);
		//void OnSliderChange(wxCommandEvent& event);
		void OnTextChange(wxCommandEvent& event);
};