#include "MainFrame.h"
#include "Codec.h"
#include "ResponsiveImageFrame.h"
#include <wx/wx.h>
#include <wx/wfstream.h>
#include <wx/filename.h>
#include <wx/imaggif.h>

MainFrame::MainFrame(const wxString& title): wxFrame(nullptr, wxID_ANY, title) {
    CreateControls();
	SetupSizers();
    CreateStatusBar();
    wxInitAllImageHandlers();
	wxLogStatus(strWelcome);
}

void MainFrame::CreateControls() {
    panel = new wxPanel(this);
    SetIcon(wxICON(IDI_ICON1));
    wxFont headlineFont(wxFontInfo(wxSize(0, 36)).Bold()), subheadlineFont(wxFontInfo(wxSize(0, 13)));;
    headerText = new wxStaticText(panel, wxID_ANY, "Genzo Image Converter", wxPoint(0, 22), wxSize(500, -1), wxALIGN_CENTRE_HORIZONTAL);
    headerText->SetFont(headlineFont);
    subheaderText = new wxStaticText(panel, wxID_ANY, "Created by Dylan Aviles", wxPoint(0, 22), wxSize(400, -1), wxALIGN_CENTRE_HORIZONTAL);
    subheaderText->SetFont(subheadlineFont);

    textCtrlFileInputPath = new wxTextCtrl(panel, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize,
        wxTE_PROCESS_ENTER);
	textCtrlFileInputPath->SetHint("Enter path to file here");
    textCtrlFileInputPath->Bind(wxEVT_TEXT, &MainFrame::OnTextChange, this);
    textCtrlFileInputPath->Bind(wxEVT_TEXT_ENTER, &MainFrame::OnPathEnter, this);

    btnBrowse = new wxButton(panel, wxID_ANY, "Browse...");
    btnBrowse->Bind(wxEVT_BUTTON, &MainFrame::OnButtonBrowseClick, this);
	btnBrowse->SetFocus();

    btnPreview = new wxButton(panel, wxID_ANY, "Preview Image");
    btnPreview->Bind(wxEVT_BUTTON, &MainFrame::OnButtonPreviewClick, this);

    btnConvert = new wxButton(panel, wxID_ANY, "Convert Image");
    btnConvert->Bind(wxEVT_BUTTON, &MainFrame::OnButtonConvertClick, this);
	btnConvert->Disable();
}

void MainFrame::SetupSizers() {
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    mainSizer->Add(headerText, wxSizerFlags().CenterHorizontal());
    mainSizer->AddSpacer(4);
    mainSizer->Add(subheaderText, wxSizerFlags().CenterHorizontal());
    mainSizer->AddSpacer(25);

    wxBoxSizer* inputSizer = new wxBoxSizer(wxHORIZONTAL);
    inputSizer->AddSpacer(5);
    inputSizer->Add(textCtrlFileInputPath, wxSizerFlags().Proportion(1));
    inputSizer->AddSpacer(17);

    wxBoxSizer* buttonSizer = new wxBoxSizer(wxVERTICAL);
    buttonSizer->Add(btnBrowse, wxSizerFlags().CenterHorizontal());
    buttonSizer->AddSpacer(7);
    buttonSizer->Add(btnPreview);
    inputSizer->Add(buttonSizer);

    mainSizer->Add(inputSizer, wxSizerFlags().Expand());
	mainSizer->AddStretchSpacer(1);
    mainSizer->AddSpacer(40);
	mainSizer->Add(btnConvert, wxSizerFlags().Right());
    mainSizer->AddSpacer(20);

	wxGridSizer* outerSizer = new wxGridSizer(1);
	outerSizer->Add(mainSizer, wxSizerFlags().Expand().Border(wxALL, 25).Expand());

    panel->SetSizer(outerSizer);
    outerSizer->SetSizeHints(this);
}

void MainFrame::LoadImage(wxString filePath) {
    if (!wxFileExists(filePath)) {
        wxMessageBox("Cannot open file", "Error", wxOK | wxICON_ERROR);
        return;
    }

    wxFileName fileName(filePath);
    fileName.Normalize(wxPATH_NORM_ALL | wxPATH_NORM_ENV_VARS);

    // TODO: Rework this if statement so I don't have 2 almost identical else statements!
    // TODO: Rework the check for heif files to rely less on file extensions and more on file format!  Other image types load w/o an extension!
    if (fileName.GetExt() == "heic" || fileName.GetExt() == "heif" || fileName.GetExt() == "avif") {
        img = Codec::LoadHEIFImage(filePath);
        if (img.IsOk()) {
            btnConvert->Enable();
        }
        else {
            img.Destroy();
            wxMessageBox("Failed to load image using libheif!", "Error", wxOK | wxICON_ERROR);
            btnConvert->Disable();
            return;
        }
    }
    else if (img.LoadFile(filePath, wxBITMAP_TYPE_ANY)) {
        btnConvert->Enable();
    }
    else {
        img.Destroy();
        wxMessageBox("Failed to load image!", "Error", wxOK | wxICON_ERROR);
        btnConvert->Disable();
        return;
    }
    wxLogStatus(wxString::Format("Selected file: %s", filePath), _("Info"));
    wxString size = wxString::Format("%d x %d", img.GetWidth(), img.GetHeight());
    wxMessageBox(wxString::Format("Selected file: %s\nImage Size in Pixels: %s", filePath, size), _("Info"));

    // TODO: Use this to show an image preview in the main window eventually
    ShowStandaloneImage(img);
}

void MainFrame::OnButtonBrowseClick(wxCommandEvent& event) {
	wxLogStatus("Select a file");
    
    wxFileDialog openFileDialog(this, _("Open Image file"), "", "", 
        allSupportedFormats, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (openFileDialog.ShowModal() == wxID_CANCEL) { wxLogStatus("Operation Cancelled"); return; }
    
    wxString filePath = openFileDialog.GetPath();
    wxLogStatus("Loading image, please wait...");
    LoadImage(filePath);
    textCtrlFileInputPath->ChangeValue(filePath);
}

void MainFrame::OnPathEnter(wxCommandEvent& event) {
    LoadImage(textCtrlFileInputPath->GetValue());
}

void MainFrame::OnButtonConvertClick(wxCommandEvent& event) {
    wxLogStatus("File Conversion Method Called!");  //TODO: Figure out what is needed to convert images to .dib bitmaps
    wxString filePath = "";
    
    wxFileDialog saveFileDialog(this, _("Save Image file"), "", "",
        supportedFormats, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (saveFileDialog.ShowModal() == wxID_OK) {
        filePath = saveFileDialog.GetPath();
        wxFileName fileName(filePath);
        fileName.Normalize(wxPATH_NORM_ALL | wxPATH_NORM_ENV_VARS);

        wxLogStatus(wxString::Format("Selected file: %s", filePath), _("Info"));
        
		// TODO: considering adding a progress bar, maybe separating the conversion process into another thread to so the UI doesn't freeze during conversion?
        if (img.IsOk()) {
            wxLogStatus("Converting image, please wait...");
            bool successful = false;
            if (fileName.GetExt() == "heic" || fileName.GetExt() == "heif" || fileName.GetExt() == "avif") {
                successful = Codec::SaveHEIFImage(img, filePath);
            }
            else if (img.SaveFile(filePath)) {
                //wxLogMessage("Image successfully saved to %s", filePath);
                successful = true;
            }
            else {
                wxLogError("Failed to save image to '%s'. Check format support.", filePath);
            }

            if (successful) {
                wxLogMessage("Image successfully saved to %s", filePath);
                wxLogStatus("Image converted successfully!  Saved to %s", filePath);
            }
        }
        else {
            wxLogError("Attempted to save an invalid image.");
        }
        
        // TODO: Figure out how to append file extension on Linux, does not happen by default for some reason
        // TODO: Figure out how to preserve metadata during conversion
        //wxMessageBox(wxString::Format("File converted and saved as: %s", filePath), _("Info"));
    }
    else {
        wxLogStatus("Conversion Cancelled");
        wxMessageBox("File conversion cancelled!", _("Info"));
    }
}

void MainFrame::OnButtonPreviewClick(wxCommandEvent& event) {
    wxString path = textCtrlFileInputPath->GetValue();
    if (!img.IsOk() && path != "") {
        LoadImage(path);
    }
    ShowStandaloneImage(img);
}

void MainFrame::OnTextChange(wxCommandEvent& event) {
    if (!event.GetString().IsEmpty()) {
        //TODO: Tweak this, file isn't necessarily loaded until it is!
        //wxString msg = wxString::Format("Selected file: %s", event.GetString());
        //wxLogStatus(msg, _("Info"));
        
        btnConvert->Enable();
    }
    else {
        wxLogStatus(strWelcome);
        btnConvert->Disable();
	}
}

// For testing purposes to ensure the images are loading properly, will integrate this with the UI in the future
/*
void MainFrame::ShowImagePopup(const wxImage& image) {
    if (!image.IsOk()) {
        wxMessageBox("The provided image is invalid or empty.", "Error", wxICON_ERROR | wxOK);
        return;
    }

    wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "Image Viewer",
        wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_FRAME_STYLE);

    wxBitmap bitmap(image);
    wxStaticBitmap* imageCtrl = new wxStaticBitmap(frame, wxID_ANY, bitmap);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(imageCtrl, 1, wxEXPAND | wxALL, 10);
    frame->SetSizerAndFit(sizer);

    frame->Show(true);
}
*/