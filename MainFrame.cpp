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

    //textCtrlFileInputPath = new wxTextCtrl(panel, wxID_ANY);
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
    //inputSizer->Add(btnBrowse);

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
    wxFileInputStream is(filePath);
    if (!is.IsOk()) {
        wxMessageBox("Cannot open file", "Error", wxOK | wxICON_ERROR);
        return;
    }

    wxFileName fileName(filePath);
    fileName.Normalize(wxPATH_NORM_ALL & wxPATH_NORM_ENV_VARS);
    if (fileName.GetExt() == "heic" || fileName.GetExt() == "heif" || fileName.GetExt() == "avif") {
        wxLogStatus(wxString::Format("HEIF Image successfully loaded!"), _("Info"));
        img = Codec::LoadHEIFImage(filePath);
        wxMessageBox("HEIF file loaded successfully!", "Info");
        ShowStandaloneImage(img);
        btnConvert->Enable();
    }
    else if (img.LoadFile(is, wxBITMAP_TYPE_ANY)) {
        wxString size = "";
        size = wxString::Format("%d x %d", img.GetWidth(), img.GetHeight());
        wxMessageBox(wxString::Format("Selected file: %s", filePath), _("Info"));
        wxLogStatus(wxString::Format("Selected file: %s", filePath), _("Info"));
        wxMessageBox(wxString::Format("Image Size in Pixels: %s", size), _("File Info"));

        wxBitmapType type = img.GetType();
        wxMessageBox(wxString::Format("Image Bitmap Type: %d", type), _("File Info"));

        switch (type) {
        case wxBITMAP_TYPE_BMP: wxMessageBox("Image Bitmap Type: BMP", _("File Info")); break;
        case wxBITMAP_TYPE_JPEG: wxMessageBox("Image Bitmap Type: JPEG", _("File Info")); break;
        case wxBITMAP_TYPE_PNG: wxMessageBox("Image Bitmap Type: PNG", _("File Info")); break;
        case wxBITMAP_TYPE_GIF: wxMessageBox("Image Bitmap Type: GIF", _("File Info")); break;
        }

        // TODO: Use this to show an image preview in the main window eventually
        ShowStandaloneImage(img);
        btnConvert->Enable();
    }
    else {
        wxMessageBox("Failed to load image", "Error", wxOK | wxICON_ERROR);
        btnConvert->Disable();
    }
    
}

void MainFrame::OnButtonBrowseClick(wxCommandEvent& event) {
	wxLogStatus("Select a file");
    
    wxFileDialog openFileDialog(this, _("Open Image file"), "", "",
        "Image files (*.heif;*.avif;*.png;*.jpg;*.tiff;*.y4m)|*.heif;*.avif;*.png;*.jpg;*.tiff;*.y4m", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (openFileDialog.ShowModal() == wxID_CANCEL) { wxLogStatus("Operation Cancelled"); return; }
    
    wxString filePath = openFileDialog.GetPath();
    LoadImage(filePath);
    textCtrlFileInputPath->ChangeValue(filePath);
    
}

void MainFrame::OnPathEnter(wxCommandEvent& event) {
    LoadImage(textCtrlFileInputPath->GetValue());
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

void MainFrame::OnButtonPreviewClick(wxCommandEvent& event) {
    ShowStandaloneImage(img);
}

void MainFrame::OnTextChange(wxCommandEvent& event) {
    if (!event.GetString().IsEmpty()) {
        //TODO: Tweak this, file isn't necessarily loaded until it is!
        wxString msg = wxString::Format("Selected file: %s", event.GetString());
        wxLogStatus(msg, _("Info"));
        
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