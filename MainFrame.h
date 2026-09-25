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
		wxStaticText *headerText, *subheaderText, *lblOutputFormat;
		wxButton *btnBrowse, *btnConvert, *btnPreview;
		wxImage img;
		const wxString strWelcome = "Welcome to the Genzo Image Converter!  Please select a file to get started.",
			avif = "AVIF (*.avif)|*.avif", bmp = "BMP (*.bmp)|*.bmp", gif = "GIF (*.gif)|*.gif", heic = "HEIF (*.heif; *.heic)|*.heif; *.heic", ico = "ICO (*.ico)|*.ico", icns = "ICNS (*.icns)|*.icns", jpg = "JPEG (*.jpg;*.jpeg;*.jpe;*.jfif)|*.jpg;*.jpeg;*.jpe;*.jfif",
			jpeg2k = "JPEG-2000 (*.jp2; *.jpf; *.jpx; *.j2k; *.j2c)|*.jp2;*.jpf;*.jpx;*.j2k;*.j2c", png = "PNG (*.png)|*.png", tiff = "TIFF (*.tiff; *.tif)|*.tiff;*.tif", webp = "WEBP (*.webp) | *.webp", all = "All files (*.*)|*.*",
			// Currently only static gifs are supported, add support for animated gifs before changing point release

			// TODO: Add XPM, GIF, ICNS & SVG support, finish adding JPEG-2000 support
			// supportedFormats is used for loading images only, not converting them!
			supportedFormats = jpg + "|" + bmp + "|" + png + "|" + gif + "|" + avif  + "|" + heic + "|" + ico + "|" + icns + "|" + jpeg2k + "|" + tiff + "|" + webp + "|" + all,
			allSupportedFormats = "Image files (*.*) |*.avif;*.bmp;*.dib;*.heif;*.heic;*.gif;*.ico;*.icns;*.jpg;*.jpeg;*.jpe;*.jfif;*.jp2;*.jpf;*.jpx;*.j2k;*.j2c;*.png;*.tiff;*.tif;*.webp" + std::string("|") + supportedFormats,
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
		void OnTextChange(wxCommandEvent& event);
		wxString LoadTextFromFile(const wxString& filePath);
};