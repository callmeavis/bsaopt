//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//

#include <windows.h>      // For common windows data types and function headers
#define STRICT_TYPED_ITEMIDS
#include <shlobj.h>
#include <objbase.h>      // For COM headers
#include <shobjidl.h>     // for IFileDialogEvents and IFileDialogControlEvents
#include <shlwapi.h>
#include <knownfolders.h> // for KnownFolder APIs/datatypes/function headers
#include <propvarutil.h>  // for PROPVAR-related functions
#include <propkey.h>      // for the Property key APIs/datatypes
#include <propidl.h>      // for the Property System APIs
#include <strsafe.h>      // for StringCchPrintfW
#include <shtypes.h>      // for COMDLG_FILTERSPEC

#include <new>
#include <exception>

template <class T> void SafeRelease(T **ppT) {
  if (*ppT) {
    (*ppT)->Release();
    *ppT = NULL;
  }
}

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "comctl32.lib")

#pragma comment(linker, "\"/manifestdependency:type='Win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

const COMDLG_FILTERSPEC c_rgLoadTypes[] =
{
    {L"Bethesda Softworks Archive (*.bsa)",	L"*.bsa"},
//  {L"All Documents (*)",         		L"*"},
};

COMDLG_FILTERSPEC c_rgSaveTypes[] =
{
    {L"Bethesda Softworks Archive (*.bsa)",	L"*.bsa"},
//  {L"All Documents (*)",         		L"*"},
};

int c_rgSaveNums = 0;
const wchar_t bsaext[] = L"bsa";
LPCWSTR defext = bsaext;
UINT defidx = 0;

const DWORD c_idFolder  = 601;
const DWORD c_idDone    = 602;

// Indices of file types
#define INDEX_BSA	1

// Controls
#define CONTROL_GROUP           2000
#define CONTROL_RADIOBUTTONLIST 2
#define CONTROL_RADIOBUTTON1    1
#define CONTROL_RADIOBUTTON2    2       // It is OK for this to have the same ID as CONTROL_RADIOBUTTONLIST,
                                        // because it is a child control under CONTROL_RADIOBUTTONLIST

// IDs for the Task Dialog Buttons
#define IDC_DEPLOYMENT		100
#define IDC_REPAIR		101
#define IDC_COPY_PACK		102

/* Utility Classes and Functions *************************************************************************************************/

LPSTR selected_string = NULL;

LPSTR UnicodeToAnsi(LPCWSTR s)
{
  if (s==NULL) return NULL;
  int cw=lstrlenW(s);
  if (cw==0) {CHAR *psz=new CHAR[1];*psz='\0';return psz;}
  int cc=WideCharToMultiByte(CP_ACP,0,s,cw,NULL,0,NULL,NULL);
  if (cc==0) return NULL;
  CHAR *psz=new CHAR[cc+1];
  cc=WideCharToMultiByte(CP_ACP,0,s,cw,psz,cc,NULL,NULL);
  if (cc==0) {delete[] psz;return NULL;}
  psz[cc]='\0';
  return psz;
}

LPCWSTR AnsiToUnicode(LPSTR s)
{
  if (s==NULL) return NULL;
  int cw=lstrlen(s);
  if (cw==0) {WCHAR *psz=new WCHAR[1];*psz='\0';return psz;}
  int cc=MultiByteToWideChar(CP_ACP,0,s,cw,NULL,0);
  if (cc==0) return NULL;
  WCHAR *psz=new WCHAR[cc+1];
  cc=MultiByteToWideChar(CP_ACP,0,s,cw,psz,cc);
  if (cc==0) {delete[] psz;return NULL;}
  psz[cc]='\0';
  return psz;
}

void ReportSelectedItems(IUnknown *punkSite, IShellItemArray *psia)
{
  DWORD cItems;
  HRESULT hr = psia->GetCount(&cItems);
  for (DWORD i = 0; SUCCEEDED(hr) && (i < cItems); i++) {
    IShellItem *psi;
    hr = psia->GetItemAt(i, &psi);
    if (SUCCEEDED(hr)) {
      PWSTR pszName;
      hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pszName);
      if (SUCCEEDED(hr)) {
	selected_string = UnicodeToAnsi(pszName);

	CoTaskMemFree(pszName);
      }

      psi->Release();
    }
  }
}

HRESULT GetSelectionFromSite(IUnknown *punkSite, BOOL fNoneImpliesFolder, IShellItemArray **ppsia)
{
    *ppsia = NULL;
    IFolderView2 *pfv;
    HRESULT hr = IUnknown_QueryService(punkSite, SID_SFolderView, IID_PPV_ARGS(&pfv));
    if (SUCCEEDED(hr)) {
	hr = pfv->GetSelection(fNoneImpliesFolder, ppsia);
	pfv->Release();
    }

    return hr;
}

void ReportSelectedItemsFromSite(IUnknown *punkSite)
{
    if (selected_string) {
	delete[] selected_string;
	selected_string = NULL;
    }

    IShellItemArray *psia;
    HRESULT hr = GetSelectionFromSite(punkSite, TRUE, &psia);
    if (SUCCEEDED(hr)) {
	ReportSelectedItems(punkSite, psia);
        psia->Release();
    }
}

/* File Dialog Event Handler *****************************************************************************************************/

class CDialogEventHandler : public IFileDialogEvents,
                            public IFileDialogControlEvents
{
public:
    // IUnknown methods
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        static const QITAB qit[] = {
            QITABENT(CDialogEventHandler, IFileDialogEvents),
            QITABENT(CDialogEventHandler, IFileDialogControlEvents),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }

    IFACEMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&_cRef);
    }

    IFACEMETHODIMP_(ULONG) Release()
    {
        long cRef = InterlockedDecrement(&_cRef);
        if (!cRef)
            delete this;
        return cRef;
    }

    // IFileDialogEvents methods
    IFACEMETHODIMP OnFileOk(IFileDialog *);// { return S_OK; };
    IFACEMETHODIMP OnFolderChange(IFileDialog *) { return S_OK; };
    IFACEMETHODIMP OnFolderChanging(IFileDialog *, IShellItem *) { return S_OK; };
    IFACEMETHODIMP OnHelp(IFileDialog *) { return S_OK; };
    IFACEMETHODIMP OnSelectionChange(IFileDialog *);// { return S_OK; };
    IFACEMETHODIMP OnShareViolation(IFileDialog *, IShellItem *, FDE_SHAREVIOLATION_RESPONSE *) { return S_OK; };
    IFACEMETHODIMP OnTypeChange(IFileDialog *pfd);
    IFACEMETHODIMP OnOverwrite(IFileDialog *, IShellItem *, FDE_OVERWRITE_RESPONSE *) { return S_OK; };

    // IFileDialogControlEvents methods
    IFACEMETHODIMP OnItemSelected(IFileDialogCustomize *pfdc, DWORD dwIDCtl, DWORD dwIDItem) { return S_OK; };
    IFACEMETHODIMP OnButtonClicked(IFileDialogCustomize *, DWORD);// { return S_OK; };
    IFACEMETHODIMP OnCheckButtonToggled(IFileDialogCustomize *, DWORD, BOOL) { return S_OK; };
    IFACEMETHODIMP OnControlActivating(IFileDialogCustomize *, DWORD) { return S_OK; };

    CDialogEventHandler() : _cRef(1) { };
private:
    ~CDialogEventHandler() { };
    long _cRef;
};

// IFileDialogEvents
IFACEMETHODIMP CDialogEventHandler::OnFileOk(IFileDialog *pfd)
{
    ReportSelectedItemsFromSite(pfd);

    return S_OK; // S_FALSE keeps the dialog up, return S_OK to allows it to dismiss
}

IFACEMETHODIMP CDialogEventHandler::OnSelectionChange(IFileDialog *pfd)
{
    /* Update the text of the Open/Add button here based on the selection
    IShellItem *psi;
    HRESULT hr = pfd->GetCurrentSelection(&psi);
    if (SUCCEEDED(hr))
    {
        SFGAOF attr;
        hr = psi->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attr);
        if (SUCCEEDED(hr) && (SFGAO_FOLDER == attr))
        {
            pfd->SetOkButtonLabel(L"Open");
        }
        else
        {
            pfd->SetOkButtonLabel(L"Add");
        }
        psi->Release();
    } */

    return S_OK;
}

IFACEMETHODIMP CDialogEventHandler::OnButtonClicked(IFileDialogCustomize *pfdc, DWORD dwIDCtl)
{
    switch (dwIDCtl)
    {
	case c_idFolder:
	    // Instead of using IFileDialog::GetCurrentSelection(), we need to get the
	    // selection from the view to handle the "no selection implies folder" case
	    ReportSelectedItemsFromSite(pfdc);

	    IFileDialog *pfd;
	    if (SUCCEEDED(pfdc->QueryInterface(&pfd))) {
		pfd->Close(S_OK);
		pfd->Release();
	    }
	    break;
	default:
	    break;
    }

    return S_OK;
}

// IFileDialogEvents methods
// This method gets called when the file-type is changed (combo-box selection changes).
// For sample sake, let's react to this event by changing the properties show.
HRESULT CDialogEventHandler::OnTypeChange(IFileDialog *pfd)
{
    IFileSaveDialog *pfsd;
    HRESULT hr = pfd->QueryInterface(&pfsd);
    if (SUCCEEDED(hr))
    {
        UINT uIndex;
        hr = pfsd->GetFileTypeIndex(&uIndex);   // index of current file-type
        if (SUCCEEDED(hr))
        {
            IPropertyDescriptionList *pdl = NULL;

            switch (uIndex)
            {
            case INDEX_BSA:
                // When .bsa is selected, let's ask for some arbitrary property, say Title.
                hr = PSGetPropertyDescriptionListFromString(L"prop:System.Title", IID_PPV_ARGS(&pdl));
                if (SUCCEEDED(hr))
                {
                    // FALSE as second param == do not show default properties.
                    hr = pfsd->SetCollectedProperties(pdl, FALSE);
                    pdl->Release();
                }
                break;
            }
        }
        pfsd->Release();
    }
    return hr;
}

// Instance creation helper
HRESULT CDialogEventHandler_CreateInstance(REFIID riid, void **ppv)
{
    *ppv = NULL;
    CDialogEventHandler *pDialogEventHandler = new (std::nothrow) CDialogEventHandler();
    HRESULT hr = pDialogEventHandler ? S_OK : E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
    {
        hr = pDialogEventHandler->QueryInterface(riid, ppv);
        pDialogEventHandler->Release();
    }
    return hr;
}

/* BSAopt Snippets ***************************************************************************************************/

// This code snippet demonstrates how to work with the BSAopt interface
HRESULT AskInput()
{
  if (selected_string) {
    delete[] selected_string;
    selected_string = NULL;
  }

  // CoCreate the File Open Dialog object.
  IFileDialog *pfd = NULL;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
  if (SUCCEEDED(hr))
  {
    // Create an event handling object, and hook it up to the dialog.
    IFileDialogEvents *pfde = NULL;
    hr = CDialogEventHandler_CreateInstance(IID_PPV_ARGS(&pfde));
    if (SUCCEEDED(hr))
    {
      // Hook up the event handler.
      DWORD dwCookie;
      hr = pfd->Advise(pfde, &dwCookie);
      if (SUCCEEDED(hr))
      {
	// Set the options on the dialog.
	DWORD dwFlags;

	// Before setting, always get the options first in order not to override existing options.
	hr = pfd->GetOptions(&dwFlags);
	if (SUCCEEDED(hr))
	{
	  // In this case, get shell items only for file system items.
	  hr = pfd->SetOptions(dwFlags | FOS_FORCEFILESYSTEM);
	  if (SUCCEEDED(hr))
	  {
	    IFileDialogCustomize *pfdc;
	    if (SUCCEEDED(pfd->QueryInterface(&pfdc))) {
	      // The spacing pads the button a bit.
	      pfdc->AddPushButton(c_idFolder, L" Use Folder ");
	      pfdc->Release();
	    }

	    // Set the file types to display only. Notice that, this is a 1-based array.
	    hr = pfd->SetFileTypes(ARRAYSIZE(c_rgLoadTypes), c_rgLoadTypes);
	    if (SUCCEEDED(hr))
	    {
	      // Set the selected file type index to Word Docs for this example.
	      hr = pfd->SetFileTypeIndex(INDEX_BSA);
	      if (SUCCEEDED(hr))
	      {
		// Set the default extension to be ".bsa" file.
		hr = pfd->SetDefaultExtension(L"bsa");
		if (SUCCEEDED(hr))
		{
		  // Show the dialog
		  hr = pfd->Show(NULL);
		  if (SUCCEEDED(hr))
		  {
#if 0
		    // Obtain the result, once the user clicks the 'Open' button.
		    // The result is an IShellItem object.
		    IShellItem *psiResult;
		    HRESULT hr2 = pfd->GetResult(&psiResult);
		    if (SUCCEEDED(hr2))
		    {
		      // We are just going to print out the name of the file for sample sake.
		      PWSTR pszFilePath = NULL;
		      hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
		      if (SUCCEEDED(hr)) {
			TaskDialog(NULL,
			  NULL,
			  L"CommonFileDialogApp",
			  pszFilePath,
			  NULL,
			  TDCBF_OK_BUTTON,
			  TD_INFORMATION_ICON,
			  NULL);

			CoTaskMemFree(pszFilePath);
		      }

		      psiResult->Release();
		    }
#endif
		  }
		}
	      }
	    }
	  }
	}

	// Unhook the event handler.
	pfd->Unadvise(dwCookie);
      }

      pfde->Release();
    }

    pfd->Release();
  }

  return hr;
}

// This code snippet demonstrates how to work with the BSAopt interface
HRESULT AskOutput()
{
  if (selected_string) {
    delete[] selected_string;
    selected_string = NULL;
  }

  // CoCreate the File Open Dialog object.
  IFileDialog *pfd = NULL;
  HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
  if (SUCCEEDED(hr))
  {
    // Create an event handling object, and hook it up to the dialog.
    IFileDialogEvents *pfde = NULL;
    hr = CDialogEventHandler_CreateInstance(IID_PPV_ARGS(&pfde));
    if (SUCCEEDED(hr))
    {
      // Hook up the event handler.
      DWORD dwCookie;
      hr = pfd->Advise(pfde, &dwCookie);
      if (SUCCEEDED(hr))
      {
	// Set the options on the dialog.
	DWORD dwFlags;

	// Before setting, always get the options first in order not to override existing options.
	hr = pfd->GetOptions(&dwFlags);
	if (SUCCEEDED(hr))
	{
	  // In this case, get shell items only for file system items.
	  hr = pfd->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_STRICTFILETYPES);
	  if (SUCCEEDED(hr))
	  {
	    IFileDialogCustomize *pfdc;
	    if (SUCCEEDED(pfd->QueryInterface(&pfdc))) {
	      // The spacing pads the button a bit.
	      pfdc->AddPushButton(c_idFolder, L" Use Folder ");
	      pfdc->Release();
	    }

	    // Set the file types to display only. Notice that, this is a 1-based array.
	    hr = pfd->SetFileTypes(ARRAYSIZE(c_rgSaveTypes), c_rgSaveTypes);
	    if (SUCCEEDED(hr))
	    {
	      // Set the selected file type index to Word Docs for this example.
	      hr = pfd->SetFileTypeIndex(INDEX_BSA);
	      if (SUCCEEDED(hr))
	      {
		// Set the default extension to be ".bsa" file.
		hr = pfd->SetDefaultExtension(defext);
		if (SUCCEEDED(hr))
		{
		  // Show the dialog
		  hr = pfd->Show(NULL);
		  if (SUCCEEDED(hr))
		  {
		    // Obtain the result, once the user clicks the 'Open' button.
		    // The result is an IShellItem object.
		    IShellItem *psiResult;
		    HRESULT hr2 = pfd->GetResult(&psiResult);
		    if (SUCCEEDED(hr2))
		    {
		      // We are just going to print out the name of the file for sample sake.
		      PWSTR pszFilePath = NULL;
		      hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
		      if (SUCCEEDED(hr)) {
			if (selected_string) {
			  delete[] selected_string;
			  selected_string = NULL;
			}

			selected_string = UnicodeToAnsi(pszFilePath);

#if 0
			TaskDialog(NULL,
			  NULL,
			  L"CommonFileDialogApp",
			  pszFilePath,
			  NULL,
			  TDCBF_OK_BUTTON,
			  TD_INFORMATION_ICON,
			  NULL);
#endif

			CoTaskMemFree(pszFilePath);
		      }

		      psiResult->Release();
		    }
		  }
		}
	      }
	    }
	  }
	}

	// Unhook the event handler.
	pfd->Unadvise(dwCookie);
      }

      pfde->Release();
    }

    pfd->Release();
  }

  return hr;
}

#if 0
HRESULT Copy()
{
  HRESULT hr;

  hr = AskInput();
  if (SUCCEEDED(hr)) {
    /* look what we'v got
    char *infile, *outfile; */
    struct stat sinfo; char ext[256], *extp;
    stat(selected_string, &sinfo);
    extp = getext(ext, selected_string);
    infile = strdup(selected_string);
    defext = AnsiToUnicode(extp);

				    c_rgSaveTypes[0] = c_rgLoadTypes[0], c_rgSaveNums = 1, defidx = 1;
    /**/ if (!stricmp("bsa", extp)) c_rgSaveTypes[1] = c_rgLoadTypes[1], c_rgSaveNums++, defidx = 2;
    else if (!stricmp("kf",  extp)) c_rgSaveTypes[1] = c_rgLoadTypes[2], c_rgSaveNums++, defidx = 2;
    else if (!stricmp("dds", extp)) c_rgSaveTypes[1] = c_rgLoadTypes[3], c_rgSaveNums++, defidx = 2;
    else if (!stricmp("png", extp)) c_rgSaveTypes[1] = c_rgLoadTypes[4], c_rgSaveNums++, defidx = 2;
    else if (!stricmp("tga", extp)) c_rgSaveTypes[1] = c_rgLoadTypes[5], c_rgSaveNums++, defidx = 2;
    else if (!stricmp("bmp", extp)) c_rgSaveTypes[1] = c_rgLoadTypes[6], c_rgSaveNums++, defidx = 2;
    else if (!stricmp("ppm", extp)) c_rgSaveTypes[1] = c_rgLoadTypes[7], c_rgSaveNums++, defidx = 2;
    else if (!stricmp("pgm", extp)) c_rgSaveTypes[1] = c_rgLoadTypes[8], c_rgSaveNums++, defidx = 2;
    else if (!stricmp("pfm", extp)) c_rgSaveTypes[1] = c_rgLoadTypes[9], c_rgSaveNums++, defidx = 2;
    else if (!stricmp("hdr", extp)) c_rgSaveTypes[1] = c_rgLoadTypes[10], c_rgSaveNums++, defidx = 2;
    else if (!stricmp("wav", extp)) c_rgSaveTypes[1] = c_rgLoadTypes[11], c_rgSaveNums++, defidx = 2;
    else                            c_rgSaveTypes[1] = c_rgLoadTypes[16], c_rgSaveNums++, defidx = 2;

    hr = AskOutput();
    if (SUCCEEDED(hr)) {
      outfile = strdup(selected_string);

      parse_inifile("./bsaopt.ini", "Copy");
      prolog();

#ifdef	NDEBUG
      //try {
#endif
	process(infile, outfile);

	fflush(stderr);
	fflush(stdout);

	/* summary */
	summary(stderr);
	summary(stdout);
#ifdef	NDEBUG
      //}
      //catch(exception &e) {
	//fprintf(stderr, "fatal error: %s\n", e.what());
      //}
#endif

      epilog();

      free(outfile);
    }

    delete[] defext;
    free(infile);
  }

  return hr;
}
#endif

#if 0
bool parse_gui() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(hr)) {
        TASKDIALOGCONFIG taskDialogParams = { sizeof(taskDialogParams) };
        taskDialogParams.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;

        TASKDIALOG_BUTTON const buttons[] =
        {
            { IDC_DEPLOYMENT,	L"Deploy an optimized mod-package" },
            { IDC_REPAIR,	L"Repair files" },
            { IDC_COPY_PACK,	L"Copy, pack or extract files without modifications" },
        };

        taskDialogParams.pButtons = buttons;
        taskDialogParams.cButtons = ARRAYSIZE(buttons);
        taskDialogParams.pszMainInstruction = L"Pick the task you want to carry out";
        taskDialogParams.pszWindowTitle = L"BSAopt";

        while (SUCCEEDED(hr)) {
            int selectedId;

            hr = TaskDialogIndirect(&taskDialogParams, &selectedId, NULL, NULL);
            if (SUCCEEDED(hr)) {
                /**/ if (selectedId == IDCANCEL)
                    break;
                else if (selectedId == IDC_DEPLOYMENT)
                    Deployment();
                else if (selectedId == IDC_REPAIR)
                    Repair();
                else if (selectedId == IDC_COPY_PACK)
                    Copy();
            }
        }

	CoUninitialize();
	exit(0);
    }

    return false;
}
#endif
