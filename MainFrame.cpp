#include "MainFrame.h"
#include <wx/wx.h>

MainFrame::MainFrame(const wxString& title): wxFrame(nullptr, wxID_ANY, title) {
    CreateControls();
	SetupSizers();
    CreateStatusBar();
	wxLogStatus(strWelcome);
}

void MainFrame::CreateControls() {
    panel = new wxPanel(this);
    wxFont headlineFont(wxFontInfo(wxSize(0, 36)).Bold()), subheadlineFont(wxFontInfo(wxSize(0, 13)));;
    headerText = new wxStaticText(panel, wxID_ANY, "Genzo Image Converter", wxPoint(0, 22), wxSize(500, -1), wxALIGN_CENTRE_HORIZONTAL);
    headerText->SetFont(headlineFont);
    subheaderText = new wxStaticText(panel, wxID_ANY, "Created by Dylan Aviles", wxPoint(0, 22), wxSize(400, -1), wxALIGN_CENTRE_HORIZONTAL);
    subheaderText->SetFont(subheadlineFont);

    textCtrlFileInputPath = new wxTextCtrl(panel, wxID_ANY);
	textCtrlFileInputPath->SetHint("Enter path to file here");
    textCtrlFileInputPath->Bind(wxEVT_TEXT, &MainFrame::OnTextChange, this);

    btnBrowse = new wxButton(panel, wxID_ANY, "Browse...");
    btnBrowse->Bind(wxEVT_BUTTON, &MainFrame::OnButtonBrowseClick, this);
	btnBrowse->SetFocus();
    btnConvert = new wxButton(panel, wxID_ANY, "Convert Image");
    btnConvert->Bind(wxEVT_BUTTON, &MainFrame::OnButtonConvertClick, this);
	btnConvert->Disable();
}

void MainFrame::SetupSizers() {
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    mainSizer->Add(headerText, wxSizerFlags().CenterHorizontal());
    mainSizer->AddSpacer(5);
    mainSizer->Add(subheaderText, wxSizerFlags().CenterHorizontal());
    mainSizer->AddSpacer(25);

    wxBoxSizer* inputSizer = new wxBoxSizer(wxHORIZONTAL);
    inputSizer->Add(textCtrlFileInputPath, wxSizerFlags().Proportion(1));
    inputSizer->AddSpacer(5);
    inputSizer->Add(btnBrowse);

    mainSizer->Add(inputSizer, wxSizerFlags().Expand());
	mainSizer->AddStretchSpacer(1);
    mainSizer->AddSpacer(40);
	mainSizer->Add(btnConvert, wxSizerFlags().Right());

	wxGridSizer* outerSizer = new wxGridSizer(1);
	outerSizer->Add(mainSizer, wxSizerFlags().Expand().Border(wxALL, 25).Expand());

    panel->SetSizer(outerSizer);
    outerSizer->SetSizeHints(this);
}

void MainFrame::OnButtonBrowseClick(wxCommandEvent& event) {
	wxLogStatus("Select a file");
    
    wxFileDialog openFileDialog(this, _("Open Image file"), "", "",
        "Image files (*.heif;*.avif;*.png;*.jpg;*.tiff;*.y4m)|*.heif;*.avif;*.png;*.jpg;*.tiff;*.y4m", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (openFileDialog.ShowModal() == wxID_OK) {
        wxString filePath = openFileDialog.GetPath();

        wxMessageBox(wxString::Format("Selected file: %s", filePath), _("Info"));
        wxLogStatus(wxString::Format("Selected file: %s", filePath), _("Info"));
        textCtrlFileInputPath->ChangeValue(filePath);
        btnConvert->Enable();
    }
    else {
        wxLogStatus(strWelcome);
        btnConvert->Disable();
    }
}

void MainFrame::OnButtonConvertClick(wxCommandEvent& event) {
    wxLogStatus("File Conversion Method Called!");
    wxString outputFormat = "JPEG files (*.jpg;*.jpeg)|*.jpg;*.jpeg|PNG files (*.png)|*.png|HEIF files (*.heif)|*.heif";

    wxString filePath = "";
    
    wxFileDialog saveFileDialog(this, _("Save Image file"), "", "",
        outputFormat, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (saveFileDialog.ShowModal() == wxID_OK) {
        filePath = saveFileDialog.GetPath();

        wxLogStatus(wxString::Format("Selected file: %s", filePath), _("Info"));
        
		// Insert logic for file conversion here, try catch statement advised for error handling. Also considering adding a progress bar
        // TODO: Figure out how to append file extension on Linux, does not happen by default for some reason
        wxMessageBox(wxString::Format("File converted and saved as: %s", filePath), _("Info"));
    }
    else {
        wxLogStatus("Conversion Cancelled");
        wxMessageBox("File conversion cancelled!", _("Info"));
    }
}

/*
void MainFrame::OnSliderChange(wxCommandEvent& event) {
	wxString msg = wxString::Format("Slider Value: %d", event.GetInt());
    wxLogStatus(msg);
}
*/

void MainFrame::OnTextChange(wxCommandEvent& event) {
    if (!event.GetString().IsEmpty()) {
        wxString msg = wxString::Format("Selected file: %s", event.GetString());
        wxLogStatus(msg, _("Info"));
        
        btnConvert->Enable();
    }
    else {
        wxLogStatus(strWelcome);
        btnConvert->Disable();
	}
}