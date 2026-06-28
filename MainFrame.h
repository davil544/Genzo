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
		wxButton* btnPreview;
		//wxArrayString supportedFormats;
		//wxChoice* choiceOutputFormat;
		wxImage img;
		const wxString& strWelcome = "Welcome to the Genzo Image Converter!  Please select a file to get started.",
			avif = "AVIF (*.avif)|*.avif", bmp = "BMP (*.bmp)|*.bmp", heic = "HEIF (*.heif; *.heic)|*.heif; *.heic", jpg = "JPEG (*.jpg;*.jpeg;*.jpe;*.jfif)|*.jpg;*.jpeg;*.jpe;*.jfif",
			/*if = "GIF (*.gif)|*.gif",*/ png = "PNG (*.png)|*.png", tiff = "TIFF (*.tiff; *.tif)|*.tiff;*.tif", webp = "WEBP (*.webp) | *.webp", all = "All files (*.*)|*.*",
			supportedFormats = jpg + "|" + bmp + "|" + png + "|" + avif  + "|" + heic + "|" + tiff + "|" + webp + "|" + all,
			
			// These formats are for loading images only, not converting them! (*.avif;*.bmp;*.dib;*.heif;*.heic;*.jpg;*.png;*.tiff;*.webp)
			allSupportedFormats = "Image files (*.*) |*.avif;*.bmp;*.dib;*.heif;*.heic;*.jpg;*.jpeg;*.jpe;*.jfif;*.png;*.tiff;*.tif;*.webp" + std::string("|") + supportedFormats;
		//TODO: Add GIF support

		void OnButtonBrowseClick(wxCommandEvent& event);
		void OnButtonConvertClick(wxCommandEvent& event);
		void OnButtonPreviewClick(wxCommandEvent& event);
		void LoadImage(wxString filePath);
		void OnPathEnter(wxCommandEvent& event);
		//void OnTxtCtrlFilePathEnterPress(wxCommandEvent& event) {
		//void OnSliderChange(wxCommandEvent& event);
		void OnTextChange(wxCommandEvent& event);
		//void ShowImagePopup(const wxImage& image);

};