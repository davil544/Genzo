#include "MainFrame.h"
#include "Codec.h"
#include "ResponsiveImageFrame.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/filedlgcustomize.h> // wxFileDialogCustomizeHook (wxWidgets 3.1.7+)
#include <wx/aboutdlg.h>
#include <wx/textfile.h>
#include <wx/stdpaths.h>
#include <wx/timer.h>
#include <functional>

#ifdef __WXGTK__
#include "genzo.xpm"
#include <gtk/gtk.h>
#endif

#ifndef APP_VERSION
#define APP_VERSION "Unknown"
#endif

namespace {
    bool g_preserveMetadata = true; // remembered from one Save dialog to the next

    // "*.heif; *.heic" for filter #N of a "desc|pattern|desc|pattern..." list -> "heif"
    wxString DefaultExtensionForFilter(const wxString& filters, int index) {
        const wxArrayString parts = wxSplit(filters, '|');
        const size_t patternIdx = static_cast<size_t>(index) * 2 + 1;
        if (index < 0 || patternIdx >= parts.size()) return wxString();

        wxString first = parts[patternIdx].BeforeFirst(';');
        first.Trim().Trim(false);
        if (!first.StartsWith("*.")) return wxString();

        wxString ext = first.Mid(2);
        return ext == "*" ? wxString() : ext; // "All files" has no default extension
    }

    class PollTimer : public wxTimer {
    public:
        explicit PollTimer(std::function<void()> callback) : m_callback(std::move(callback)) {}
        void Notify() override { m_callback(); }
    private:
        std::function<void()> m_callback;
    };

    class MetadataCustomizeHook : public wxFileDialogCustomizeHook {
    public:
        MetadataCustomizeHook(wxFileDialog& dialog, const wxString& filters, unsigned sourceKinds, bool initialValue)
            : m_dialog(dialog), m_filters(filters), m_sourceKinds(sourceKinds), m_value(initialValue),
            m_timer([this]() { UpdateCustomControls(); }) {}

        ~MetadataCustomizeHook() { StopPolling(); }

        void AddCustomControls(wxFileDialogCustomize& customizer) override {
            wxString label = "Preserve image metadata";
            if (m_sourceKinds != 0) {
                label << " (" << MetadataBlobs::KindsToString(m_sourceKinds) << ")";
            }
            else {
                label << " (None present)";
            }

            m_checkBox = customizer.AddCheckBox(label);
            m_checkBox->SetValue(m_sourceKinds != 0 && m_value);
            m_checkBox->Enable(m_sourceKinds != 0);

#ifdef __WXOSX__
            m_lastVisible = 1;
            m_timer.Start(300);
#else
            UpdateVisibility(wxEmptyString, m_dialog.GetFilterIndex());
            m_timer.Start(150);
#endif
        }

        void UpdateCustomControls() override {
            if (!m_checkBox) return;
            UpdateVisibility(m_dialog.GetCurrentlySelectedFilename(), CurrentFilterIndex());
        }

        void TransferDataFromCustomControls() override {
            if (m_checkBox) m_value = m_checkBox->GetValue();
        }

        void StopPolling() {
            if (m_timer.IsRunning()) m_timer.Stop();
        }

        bool IsChecked() const { return m_value; }

    private:
        int CurrentFilterIndex() const {
            int index = m_dialog.GetCurrentlySelectedFilterIndex();
#ifdef __WXGTK__
            if (index == wxNOT_FOUND) index = GtkFilterIndex();
#endif
#ifndef __WXOSX__
            if (index == wxNOT_FOUND) index = m_dialog.GetFilterIndex();
#endif
            return index;
        }

#ifdef __WXGTK__
        // Asks GTK which filter is selected and matches its name against our filter descriptions.
        int GtkFilterIndex() const {
            GtkWidget* widget = static_cast<GtkWidget*>(m_dialog.GetHandle());
            if (!widget || !GTK_IS_FILE_CHOOSER(widget)) return wxNOT_FOUND;

            GtkFileFilter* filter = gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(widget));
            const gchar* name = filter ? gtk_file_filter_get_name(filter) : nullptr;
            if (!name) return wxNOT_FOUND;

            const wxString current = wxString::FromUTF8(name);
            const wxArrayString parts = wxSplit(m_filters, '|');
            for (size_t i = 0; i + 1 < parts.size(); i += 2) {
                if (parts[i] == current) return static_cast<int>(i / 2);
            }
            return wxNOT_FOUND;
        }
#endif

        void UpdateVisibility(const wxString& selectedName, int filterIndex) {
            wxString ext;
            if (!selectedName.empty() && !wxDirExists(selectedName)) {
                ext = wxFileName(selectedName).GetExt().Lower();
            }
#ifdef __WXOSX__
            if (ext.empty() && filterIndex != wxNOT_FOUND) ext = DefaultExtensionForFilter(m_filters, filterIndex);
            if (ext.empty()) return;
#else
            if (ext.empty()) ext = DefaultExtensionForFilter(m_filters, filterIndex);
#endif

            const int visible = Metadata::WritableKinds(ext) != 0 ? 1 : 0;
            if (visible != m_lastVisible) {
                m_lastVisible = visible;
                m_checkBox->Show(visible != 0);
            }
        }

        wxFileDialog& m_dialog;
        wxString m_filters;
        unsigned m_sourceKinds;
        bool m_value;
        wxFileDialogCheckBox* m_checkBox = nullptr;
        int m_lastVisible = -1; // -1 = not set yet
        PollTimer m_timer;
    };
}

MainFrame::MainFrame(const wxString& title) : wxFrame(nullptr, wxID_ANY, title) {
    wxInitAllImageHandlers();
    Codec::RegisterGIFHandler();
    CreateMenuBar();
    CreateControls();
    SetupSizers();
    CreateStatusBar();
    wxLogStatus(strWelcome);
}

void MainFrame::CreateControls() {
    panel = new wxPanel(this);

#if defined(__WXMSW__)
    SetIcon(wxICON(IDI_ICON1));
#elif defined(__WXGTK__)
    SetIcon(wxIcon(genzo_xpm));
#elif defined(__WXMAC__)
    wxIcon macIcon;
    if (macIcon.LoadFile(wxStandardPaths::Get().GetResourcesDir() + "genzo.icns", wxBITMAP_TYPE_ICON)) {
        SetIcon(macIcon);
    }
#endif

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

void MainFrame::CreateMenuBar() {
    wxMenu* menuFile = new wxMenu;
    wxMenu* menuHelp = new wxMenu;

    menuFile->Append(wxID_OPEN, "&Open...\tCtrl-O", "Open an image file");
    menuFile->Append(wxID_SAVEAS, "&Save/Convert...\tCtrl-S", "Convert and save the image");
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT, "E&xit\tAlt-X", "Quit the application");

    wxMenuBar* menuBar = new wxMenuBar;
    menuBar->Append(menuFile, "&File");

    menuHelp->Append(wxID_ABOUT, "&About...\tF1", "Show information about this application");
    menuBar->Append(menuHelp, "&Help");

    SetMenuBar(menuBar);

    Bind(wxEVT_MENU, &MainFrame::OnButtonBrowseClick, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnButtonConvertClick, this, wxID_SAVEAS);
    Bind(wxEVT_MENU, &MainFrame::OnExit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
}

void MainFrame::OnExit(wxCommandEvent& event) {
    Close(true);
}

void MainFrame::LoadImage(wxString filePath) {
    if (!wxFileExists(filePath)) {
        wxMessageBox("Cannot open file", "Error", wxOK | wxICON_ERROR);
        return;
    }

    sourceMetadata.Clear(); // never let a previous image's metadata leak onto this one
    animation.Clear();

    wxFileName fileName(filePath);
    fileName.Normalize(wxPATH_NORM_ALL | wxPATH_NORM_ENV_VARS);

    // TODO: Rework this if statement so I don't have 2 almost identical else statements!
    // TODO: Rework the check for heif files to rely less on file extensions and more on file format!  Other image types load w/o an extension!
    if (fileName.GetExt() == "heic" || fileName.GetExt() == "heif" || fileName.GetExt() == "avif") {
        img = Codec::LoadHEIFAnimation(filePath, animation) ? animation.frames[0].image : wxImage();
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
    else if (fileName.GetExt().Lower() == "gif") {
        img = Codec::LoadGIFAnimation(filePath, animation) ? animation.frames[0].image : wxImage();
        if (img.IsOk()) {
            btnConvert->Enable();
        }
        else {
            img.Destroy();
            wxMessageBox("Failed to load GIF using giflib!", "Error", wxOK | wxICON_ERROR);
            btnConvert->Disable();
            return;
        }
    }
    else if (fileName.GetExt().Lower() == "webp") {
        img = Codec::LoadWebPAnimation(filePath, animation) ? animation.frames[0].image : wxImage();
        if (img.IsOk()) {
            btnConvert->Enable();
        }
        else {
            img.Destroy();
            wxMessageBox("Failed to load WebP using libwebp!", "Error", wxOK | wxICON_ERROR);
            btnConvert->Disable();
            return;
        }
    }
    else if (fileName.GetExt() == "icns") {
        img = Codec::LoadICNS(filePath);

        if (img.IsOk()) {
            btnConvert->Enable();
        }
        else {
            img.Destroy();
            btnConvert->Disable();
            return;
        }
    }

    else if (fileName.GetExt() == "jp2" || fileName.GetExt() == "jpf" || fileName.GetExt() == "jpx" || fileName.GetExt() == "j2c" || fileName.GetExt() == "j2k") {
        img = Codec::LoadJP2(filePath);

        if (img.IsOk()) {
            btnConvert->Enable();
        }
        else {
            img.Destroy();
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
    // Read Exif / IPTC / XMP straight from the file's container. This is independent of which
    // decoder produced the pixels, and is identified by the file's contents rather than its extension.
    Metadata::ReadFromFile(filePath, sourceMetadata);

    wxLogStatus(wxString::Format("Selected file: %s", filePath), _("Info"));
    wxString size = wxString::Format("%d x %d", img.GetWidth(), img.GetHeight());
    wxString metadataFound = sourceMetadata.IsEmpty() ? wxString("none") : MetadataBlobs::KindsToString(sourceMetadata.Kinds());
    wxString frames = animation.IsAnimated() ? wxString::Format("\nAnimation Frames: %d", static_cast<int>(animation.frames.size())) : wxString();
    wxMessageBox(wxString::Format("Selected file: %s\nImage Size in Pixels: %s\nMetadata: %s%s", filePath, size, metadataFound, frames), _("Info"));

    // TODO: Use this to show an image preview in the main window eventually
    ShowStandaloneImage(this, img, previewFrame);
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
    wxLogStatus("File Conversion Method Called!");
    wxString filePath = "";

    wxFileDialog saveFileDialog(this, _("Save Image file"), "", "",
        supportedFormats, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    // Start on the first filter (JPEG). macOS otherwise comes up on "All files", which has no extension.
    saveFileDialog.SetFilterIndex(0);

    MetadataCustomizeHook metadataHook(saveFileDialog, supportedFormats, sourceMetadata.Kinds(), g_preserveMetadata);
    const bool hasMetadataCheckbox = saveFileDialog.SetCustomizeHook(metadataHook);

    const int dialogResult = saveFileDialog.ShowModal();
    metadataHook.StopPolling();

    if (dialogResult == wxID_OK) {
        filePath = saveFileDialog.GetPath();
        wxFileName fileName(filePath);
        fileName.Normalize(wxPATH_NORM_ALL | wxPATH_NORM_ENV_VARS);

        // No extension typed (macOS and Linux don't add one themselves, and "All files" has none to add):
        // use the selected filter's extension, or JPEG if that doesn't give one.
        if (fileName.GetExt().empty()) {
            wxString defaultExt = DefaultExtensionForFilter(supportedFormats, saveFileDialog.GetFilterIndex());
            if (defaultExt.empty()) defaultExt = "jpg";
            fileName.SetExt(defaultExt);

            if (fileName.FileExists() &&
                wxMessageBox(wxString::Format("\"%s\" already exists. Do you want to replace it?", fileName.GetFullName()),
                    _("Save Image file"), wxYES_NO | wxICON_QUESTION, this) != wxYES) {
                wxLogStatus("Conversion Cancelled");
                return;
            }
            filePath = fileName.GetFullPath();
        }

        const bool formatCanCarry = (Metadata::WritableKinds(fileName.GetExt()) & sourceMetadata.Kinds()) != 0;
        bool preserveMetadata = g_preserveMetadata;
        if (hasMetadataCheckbox) {
            preserveMetadata = metadataHook.IsChecked();
        }
        else if (formatCanCarry) {
            preserveMetadata = wxMessageBox(
                wxString::Format("Preserve the original metadata (%s) in the converted file?",
                    MetadataBlobs::KindsToString(sourceMetadata.Kinds())),
                _("Metadata"), wxYES_NO | wxICON_QUESTION, this) == wxYES;
        }
        if (formatCanCarry) {
            g_preserveMetadata = preserveMetadata;
        }
        const bool wantMetadata = preserveMetadata && !sourceMetadata.IsEmpty();

        wxLogStatus(wxString::Format("Selected file: %s", filePath), _("Info"));

        if (img.IsOk()) {
            wxLogStatus("Converting image, please wait...");
            wxString ext = fileName.GetExt().Lower();
            bool successful = false;
            const bool isHeif = (ext == "heic" || ext == "heif" || ext == "avif");
            // Animated sources keep their animation when the target can hold one (GIF, WebP, AVIF); everything else gets the first frame.
            const bool animatedSource = animation.IsAnimated();
            const AnimatedImage stillSource = AnimatedImage::FromImage(img);
            const AnimatedImage& source = animatedSource ? animation : stillSource;
            if (isHeif) {
                successful = Codec::SaveHEIFImage(source, filePath, 80, wantMetadata ? &sourceMetadata : nullptr);
            }
            else if (ext == "gif") {
                successful = Codec::SaveGIF(source, filePath);
            }
            else if (ext == "webp" && animatedSource) {
                successful = Codec::SaveWebPAnimation(source, filePath);
            }
            else if (ext == "icns") {
                successful = Codec::SaveICNS(img, filePath);
            }
            else if (ext == "jp2" || ext == "jpf" || ext == "jpx" || ext == "j2k" || ext == "j2c") {
                successful = Codec::SaveJP2(img, filePath, 100);
            }
            else if (ext == "tga" || ext == "targa" || ext == "icb" || ext == "vda" || ext == "vst" || ext == "tpic") {
                successful = img.SaveFile(filePath, wxBITMAP_TYPE_TGA);
            }
            else if (img.SaveFile(filePath)) {
                successful = true;
            }
            else {
                wxLogError("Failed to save image to '%s'. Check format support.", filePath);
            }

            wxString metadataNote;
            if (successful && wantMetadata && formatCanCarry) {
                const unsigned have = sourceMetadata.Kinds();
                const unsigned canWrite = Metadata::WritableKinds(ext);
                const unsigned carried = have & canWrite;
                const unsigned dropped = have & ~canWrite;

                bool embedded = true;
                if (carried != 0 && !isHeif) {
                    embedded = Metadata::EmbedInFile(filePath, sourceMetadata);
                }

                if (!embedded) {
                    metadataNote = "\nThe image was saved, but its metadata could not be copied.";
                }
                else if (carried != 0) {
                    metadataNote = "\nMetadata preserved: " + MetadataBlobs::KindsToString(carried);
                    if (dropped != 0) {
                        metadataNote += " (this format can't store: " + MetadataBlobs::KindsToString(dropped) + ")";
                    }
                }
            }

            if (successful && animatedSource && ext != "gif" && ext != "webp" && ext != "avif") {
                metadataNote += "\nThis format can't store animations, so only the first frame was saved.";
            }

            if (successful) {
                wxLogMessage("Image successfully saved to %s%s", filePath, metadataNote);
                wxLogStatus("Image converted successfully!  Saved to %s", filePath);
            }
        }
        else {
            wxLogError("Attempted to save an invalid image.");
        }
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
    ShowStandaloneImage(this, img, previewFrame);
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

void MainFrame::OnButtonAboutClick(wxCommandEvent& event) {
    wxLogStatus("Copyright Info Might Go Here");
    wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "Image Viewer",
        wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_FRAME_STYLE);
}

void MainFrame::OnAbout(wxCommandEvent& event) {
    wxDialog aboutDialog(this, wxID_ANY, "About Genzo Image Converter",
        wxDefaultPosition, wxSize(500, 450));

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    wxFont headlineFont(wxFontInfo(wxSize(0, 18)).Bold());

    wxStaticText* titleText = new wxStaticText(&aboutDialog, wxID_ANY,
        "Genzo Image Converter",
        wxDefaultPosition, wxDefaultSize,
        wxALIGN_CENTRE_HORIZONTAL);
    titleText->SetFont(headlineFont);

    wxStaticText* subText = new wxStaticText(&aboutDialog, wxID_ANY,
        "Version " APP_VERSION "\nCreated by Dylan Aviles",
        wxDefaultPosition, wxDefaultSize,
        wxALIGN_CENTRE_HORIZONTAL);

    wxArrayString libraryOptions;
    libraryOptions.Add("Genzo");
    libraryOptions.Add("wxWidgets");
    libraryOptions.Add("libheif");
    libraryOptions.Add("aom");
    libraryOptions.Add("dav1d");
    libraryOptions.Add("giflib");
    libraryOptions.Add("kvazaar");
    libraryOptions.Add("libjpeg-turbo");
    libraryOptions.Add("libwebp");
    libraryOptions.Add("openjpeg");

    wxChoice* libChoice = new wxChoice(&aboutDialog, wxID_ANY, wxDefaultPosition, wxDefaultSize, libraryOptions);
    libChoice->SetSelection(0);

    wxTextCtrl* licenseTextBox = new wxTextCtrl(&aboutDialog, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxSize(-1, 200),
        wxTE_MULTILINE | wxTE_READONLY);

    auto updateLicenseText = [licenseTextBox, this](int selection) {
        wxString text, lib;
        switch (selection) {
        case 0:
            text = "Genzo Image Converter is distributed under the Clear BSD License.\n\n"
                "Copyright(c) 2026 Dylan Aviles\n"
                "All rights reserved.\n\n" + clear_bsd;
            break;

        case 1:
            text = wxWidgets;
            break;

        case 2:
            lib = "libheif";
            break;

        case 3:
            lib = "aom";
            break;

        case 4:
            lib = "dav1d";
            break;

        case 5:
            lib = "giflib";
            break;

        case 6:
            lib = "kvazaar";
            break;

        case 7:
            lib = "libjpeg-turbo";
            break;

        case 8:
            lib = "libwebp";
            break;

        case 9:
            lib = "openjpeg";
            break;

        default:
            break;
        }

        if (selection != 0 && selection != 1) {
            text = LoadTextFromFile(wxFileName::GetPathSeparator() + wxString("third-party-licenses") + wxFileName::GetPathSeparator() + lib + wxString(".txt"));
        }
        if (text.IsEmpty()) {
            text = "License information not available for the selected library. Please ensure license files are present in the \"third-party-licenses\" folder and try again.";
        }
        licenseTextBox->SetValue(text);
        };

    updateLicenseText(0);

    libChoice->Bind(wxEVT_CHOICE, [updateLicenseText](wxCommandEvent& e) {
        updateLicenseText(e.GetSelection());
        });

    wxButton* okButton = new wxButton(&aboutDialog, wxID_OK, "OK");

    sizer->AddSpacer(15);
    sizer->Add(titleText, wxSizerFlags().CenterHorizontal());
    sizer->Add(subText, wxSizerFlags().CenterHorizontal().Border(wxTOP | wxBOTTOM, 10));

    sizer->Add(new wxStaticText(&aboutDialog, wxID_ANY, "View License For:"), wxSizerFlags().Border(wxLEFT | wxTOP, 10));

    sizer->Add(libChoice, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 10));
    sizer->Add(licenseTextBox, wxSizerFlags().Proportion(1).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 10));

    sizer->Add(okButton, wxSizerFlags().CenterHorizontal().Border(wxBOTTOM, 15));

    aboutDialog.SetSizer(sizer);
    aboutDialog.Layout();

    aboutDialog.CenterOnScreen();
    aboutDialog.ShowModal();
}

wxString MainFrame::LoadTextFromFile(const wxString& filePath) {
    wxString fileContents = "";
    wxTextFile file;

    if (file.Open(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath() + filePath)) {
        for (size_t i = 0; i < file.GetLineCount(); ++i) {
            fileContents << file.GetLine(i) << "\n";
        }
        file.Close();
    }
    else {
        wxLogError("Could not open file: %s", wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath() + filePath);
    }

    return fileContents;
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