#pragma once
#include <wx/wx.h>

class MainFrame : public wxFrame
{
	public:
		MainFrame(const wxString& title);

	private:
		void CreateControls();
		void SetupSizers();
		void CreateMenuBar();
		void OnExit(wxCommandEvent& event);
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
		const wxString strWelcome = "Welcome to the Genzo Image Converter!  Please select a file to get started.",
			avif = "AVIF (*.avif)|*.avif", bmp = "BMP (*.bmp)|*.bmp", heic = "HEIF (*.heif; *.heic)|*.heif; *.heic", ico = "ICO (*.ico)|*.ico", jpg = "JPEG (*.jpg;*.jpeg;*.jpe;*.jfif)|*.jpg;*.jpeg;*.jpe;*.jfif",
			/*gif = "GIF (*.gif)|*.gif",*/ png = "PNG (*.png)|*.png", tiff = "TIFF (*.tiff; *.tif)|*.tiff;*.tif", webp = "WEBP (*.webp) | *.webp", all = "All files (*.*)|*.*",
			supportedFormats = jpg + "|" + bmp + "|" + png + "|" + avif  + "|" + heic + "|" + ico + "|" + tiff + "|" + webp + "|" + all,
			
			// These formats are for loading images only, not converting them! (*.avif;*.bmp;*.dib;*.heif;*.heic;*.jpg;*.png;*.tiff;*.webp)
			// TODO: Add XPM, GIF, ICNS & SVG support
			allSupportedFormats = "Image files (*.*) |*.avif;*.bmp;*.dib;*.heif;*.heic;*.ico;*.jpg;*.jpeg;*.jpe;*.jfif;*.png;*.tiff;*.tif;*.webp" + std::string("|") + supportedFormats,
			clear_bsd = "Redistribution and use in source and binary forms, with or without "
			"modification, are permitted (subject to the limitations in the disclaimer "
			"below) provided that the following conditions are met:\n\n"

			"1. Redistributions of source code must retain the above copyright notice, this "
			"list of conditions and the following disclaimer.\n\n"

			"2. Redistributions in binary form must reproduce the above copyright notice, "
			"this list of conditions and the following disclaimer in the documentation "
			"and /or other materials provided with the distribution.\n\n"

			"3. Neither the name of the copyright holder nor the names of its "
			"contributors may be used to endorse or promote products derived from "
			"this software without specific prior written permission.\n\n"

			"NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY "
			"THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "
			"\"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE "
			"IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE "
			"DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR "
			"ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES "
			"(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS "
			"OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY "
			"OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING NEGLIGENCE "
			"OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED "
			"OF THE POSSIBILITY OF SUCH DAMAGE.",
			
			wxWidgets = "wxWidgets is distributed under the wxWindows Library Licence.\n\n"
			"This is essentially the GNU Library General Public License (LGPL), "
			"with an exception stating that derived works in binary form may be "
			"distributed on the user's own terms.\n\n"
			"See https://www.wxwidgets.org/about/licence/ for full details.";
		

		void OnButtonBrowseClick(wxCommandEvent& event);
		void OnButtonConvertClick(wxCommandEvent& event);
		void OnButtonPreviewClick(wxCommandEvent& event);
		void OnButtonAboutClick(wxCommandEvent& event);
		void OnAbout(wxCommandEvent& event);
		void LoadImage(wxString filePath);
		void OnPathEnter(wxCommandEvent& event);
		//void OnTxtCtrlFilePathEnterPress(wxCommandEvent& event) {
		//void OnSliderChange(wxCommandEvent& event);
		void OnTextChange(wxCommandEvent& event);
		wxString LoadTextFromFile(const wxString& filePath);
		//void ShowImagePopup(const wxImage& image);
};