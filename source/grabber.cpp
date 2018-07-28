#include "grabber.h"
#include <initguid.h>
//#include <mutex>

#include "dbg.h"

//######################################
// Defines
//######################################
#define WM_FRAMECHANGED WM_USER
//#define WM_SIZECHANGED WM_USER+1
#define GL_BGR 0x80E0
#define GL_BGRA 0x80E1

//######################################
// Globals
//######################################
HANDLE g_thread = NULL;
HWND g_hwnd = NULL;
SIZE g_videoSize = {320, 240};
int g_videoDepth = 3;
int g_dataSize = 320 * 240 * 3;
GLuint g_texID;
SpoutSender * g_spoutSender = NULL;

//std::mutex mymutex;

//######################################
// GUIDs
//######################################

// {2060C516-38D1-4E4D-AD67-8CF6BE5FA859}
DEFINE_GUID(CLSID_SpoutGrabber,
	0x2060c516, 0x38d1, 0x4e4d, 0xad, 0x67, 0x8c, 0xf6, 0xbe, 0x5f, 0xa8, 0x59);

// {692E882D-97AF-4B5F-9AD9-260D111310F5}
DEFINE_GUID(IID_ISpoutGrabber,
	0x692e882d, 0x97af, 0x4b5f, 0x9a, 0xd9, 0x26, 0xd, 0x11, 0x13, 0x10, 0xf5);

//######################################
// Setup data
//######################################

const AMOVIESETUP_PIN psudSpoutGrabberPins[] = {
  { L"Input"            // strName
  , FALSE               // bRendered
  , FALSE               // bOutput
  , FALSE               // bZero
  , FALSE               // bMany
  , &CLSID_NULL         // clsConnectsToFilter
  , L""                 // strConnectsToPin
  , 0                   // nTypes
  , NULL                // lpTypes
  }
, { L"Output"           // strName
  , FALSE               // bRendered
  , TRUE                // bOutput
  , FALSE               // bZero
  , FALSE               // bMany
  , &CLSID_NULL         // clsConnectsToFilter
  , L""                 // strConnectsToPin
  , 0                   // nTypes
  , NULL                // lpTypes
  }
};

const AMOVIESETUP_FILTER sudSpoutGrabber = {
  &CLSID_SpoutGrabber   // clsID
, L"SpoutGrabber"       // strName
, MERIT_DO_NOT_USE      // dwMerit
, 2                     // nPins
, psudSpoutGrabberPins  // lpPin
};

// Needed for the CreateInstance mechanism
CFactoryTemplate g_Templates[] = {
	{ L"SpoutGrabber"
		, &CLSID_SpoutGrabber
		, CSpoutGrabber::CreateInstance
		, NULL
		, &sudSpoutGrabber }

};

int g_cTemplates = sizeof(g_Templates)/sizeof(g_Templates[0]);

//######################################
// Notify about errors
//######################################
void ErrorMessage (const char * msg) {
	// print to debug log
	OutputDebugStringA(msg);

	// optional: show blocking message box?
	MessageBoxA(NULL, msg, "Error", MB_OK);
}

//######################################
// The message handler for the hidden dummy window
//######################################
LRESULT CALLBACK DLLWindowProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {

	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	//case WM_SIZECHANGED:
	//	bool spoutInitialized;
	//	spoutInitialized = g_spoutSender->UpdateSender("SpoutGrabber", g_videoSize.cx, g_videoSize.cy);
	//	if (!spoutInitialized) {
	//		ErrorMessage("Updating Spout Sender failed");
	//	}
	//	return 0;
	//	break;

	case WM_FRAMECHANGED:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_videoSize.cx, g_videoSize.cy, 0, g_videoDepth==4?GL_BGRA:GL_BGR, GL_UNSIGNED_BYTE, (PBYTE)wParam);
		if (glGetError()) {
			ErrorMessage("Defining texture image failed");
		}
		else {
			//send the texture via spout
			g_spoutSender->SendTexture(
				g_texID,
				GL_TEXTURE_2D,
				g_videoSize.cx,
				g_videoSize.cy,
				true
			);
		}
		break;

	default:
		return DefWindowProcW(hwnd, message, wParam, lParam);

	}

	return 0;
}

//######################################
// The new window/OpenGL thread
//######################################
DWORD WINAPI ThreadProc (void * data) {

	int exitCode = 0;

	HDC hdc = NULL;
	HGLRC hRc = NULL;
	HGLRC glContext = NULL;

	//######################################
	// Create dummy window for OpenGL context
	//######################################

	HINSTANCE inj_hModule = GetModuleHandle(NULL);

	// register the windows class
	WNDCLASSEXA wc = { 0 };
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.hInstance = inj_hModule;
	wc.lpszClassName = "SpoutGrabber";
	wc.lpfnWndProc = DLLWindowProc;
	wc.style = 0;
	wc.hIcon = NULL;
	wc.hIconSm = NULL;
	wc.hCursor = NULL;
	wc.lpszMenuName = NULL;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hbrBackground = NULL;

	if (!RegisterClassExA(&wc)) {
		ErrorMessage("RegisterClassEx failed");
		++exitCode;
		goto cleanup;
	}

	g_hwnd = CreateWindowExA(
		0, // dwExStyle
		"SpoutGrabber",
		"SpoutGrabber",
		WS_OVERLAPPEDWINDOW,
		0,
		0,
		1,
		1,
		NULL,
		NULL,
		inj_hModule,
		NULL
	);

	if (!g_hwnd) {
		ErrorMessage("Couldn't create dummy window");
		++exitCode;
		goto cleanup;
	}

	hdc = GetDC(g_hwnd);
	if (!hdc) {
		ErrorMessage("GetDC failed");
		++exitCode;
		goto cleanup;
	}

	PIXELFORMATDESCRIPTOR pfd;
	ZeroMemory(&pfd, sizeof(pfd));
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 16;
	pfd.iLayerType = PFD_MAIN_PLANE;
	int iFormat = ChoosePixelFormat(hdc, &pfd);
	if (!iFormat) {
		ErrorMessage("ChoosePixelFormat failed");
		++exitCode;
		goto cleanup;
	}

	if (!SetPixelFormat(hdc, iFormat, &pfd)) {
		DWORD dwError = GetLastError();
		// 2000 (0x7D0) The pixel format is invalid.
		// Caused by repeated call of the SetPixelFormat function
		char temp[128];
		sprintf_s(temp, "SetPixelFormat failed: %d (%x)", dwError, dwError);
		ErrorMessage(temp);
		++exitCode;
		goto cleanup;
	}

	hRc = wglCreateContext(hdc);
	if (!hRc) {
		ErrorMessage("wglCreateContext failed");
		++exitCode;
		goto cleanup;
	}

	if (wglMakeCurrent(hdc, hRc)) {
		glContext = wglGetCurrentContext();
		if (glContext == NULL) {
			ErrorMessage("wglGetCurrentContext failed");
			++exitCode;
			goto cleanup;
		}
	}
	else {
		ErrorMessage("wglMakeCurrent failed");
		++exitCode;
		goto cleanup;
	}

	//######################################
	// Create texture
	//######################################
	glGenTextures(1, &g_texID); // should never fail
	glBindTexture(GL_TEXTURE_2D, g_texID);
	if (glGetError()) {
		ErrorMessage("Creating texture failed");
		++exitCode;
		goto cleanup;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	//######################################
	// Create spout sender
	//######################################
	g_spoutSender = new SpoutSender;
	bool spoutInitialized = g_spoutSender->CreateSender("SpoutGrabber", g_videoSize.cx, g_videoSize.cy);
	if (!spoutInitialized) {
		ErrorMessage("Creating Spout Sender failed");
		++exitCode;
		goto cleanup;
	}

	//######################################
	// Dispatch messages
	//######################################
	MSG messages;
	while (GetMessage(&messages, NULL, 0, 0)) {
		TranslateMessage(&messages);
		DispatchMessage(&messages);
	}

	//######################################
	// Clean up
	//######################################
cleanup:

	// Release and delete SpoutSender
	if (g_spoutSender) {
		g_spoutSender->ReleaseSender();
		delete g_spoutSender;
		g_spoutSender = NULL;
	}

	// Delete OpenGl context
	if (glContext) {
		wglDeleteContext(glContext);
	}

	// Destroy dummy window used for OpenGL context creation
	if (g_hwnd) {
		DestroyWindow(g_hwnd);
	}

	return exitCode;
}

//######################################
// CreateInstance
// Provide the way for COM to create a CSpoutGrabber object
//######################################
CUnknown * WINAPI CSpoutGrabber::CreateInstance (LPUNKNOWN punk, HRESULT *phr) {
	ASSERT(phr);

	// assuming we don't want to modify the data
	CSpoutGrabber *pNewObject = new CSpoutGrabber(punk, phr, FALSE);

	if(pNewObject == NULL) {
		if (phr) *phr = E_OUTOFMEMORY;
	}

	return pNewObject;
}

//######################################
// Constructor
//######################################
CSpoutGrabber::CSpoutGrabber (IUnknown * pOuter, HRESULT * phr, BOOL ModifiesData)
		: CTransInPlaceFilter( TEXT("SpoutGrabber"), (IUnknown*) pOuter, CLSID_SpoutGrabber, phr, (BOOL)ModifiesData )
{
	// this is used to override the input pin with our own
	m_pInput = (CTransInPlaceInputPin*) new CSpoutGrabberInPin(this, phr);
	if (!m_pInput) {
		if (phr) *phr = E_OUTOFMEMORY;
	}

	g_thread = CreateThread(0, 0, ThreadProc, NULL, 0, 0);
}

//######################################
// Destructor
//######################################
CSpoutGrabber::~CSpoutGrabber () {
	if (g_hwnd && g_thread) {
		SendMessage(g_hwnd, WM_DESTROY, 0, 0);
		WaitForSingleObject(g_thread, INFINITE);
		g_thread = NULL;
	}

	m_pInput = NULL;
}

//######################################
//
//######################################
STDMETHODIMP CSpoutGrabber::NonDelegatingQueryInterface (REFIID riid, void ** ppv) {
	CheckPointer(ppv, E_POINTER);

	if(riid == IID_ISpoutGrabber) {
		return GetInterface((ISpoutGrabber *) this, ppv);
	}
	else {
		return CTransInPlaceFilter::NonDelegatingQueryInterface(riid, ppv);
	}
}

//######################################
// This is where you force the sample grabber to connect with one type
// or the other. What you do here is crucial to what type of data your
// app will be dealing with in the sample grabber's callback. For instance,
// if you don't enforce right-side-up video in this call, you may not get
// right-side-up video in your callback. It all depends on what you do here.
//######################################
HRESULT CSpoutGrabber::CheckInputType( const CMediaType * pMediaType) {
	CheckPointer(pMediaType, E_POINTER);
	CAutoLock lock( &m_Lock );

	// Does this have a VIDEOINFOHEADER format block
	const GUID *pFormatType = pMediaType->FormatType();
	if (*pFormatType != FORMAT_VideoInfo) {
		NOTE("Format GUID not a VIDEOINFOHEADER");
		return E_INVALIDARG;
	}
	ASSERT(pMediaType->Format());

	// Check the format looks reasonably ok
	ULONG Length = pMediaType->FormatLength();
	if (Length < SIZE_VIDEOHEADER) {
		NOTE("Format smaller than a VIDEOHEADER");
		return E_FAIL;
	}

	// Check if the media major type is MEDIATYPE_Video
	const GUID *pMajorType = pMediaType->Type();
	if (*pMajorType != MEDIATYPE_Video) {
		NOTE("Major type not MEDIATYPE_Video");
		return E_INVALIDARG;
	}

	// Check if the media subtype is either MEDIASUBTYPE_RGB32 or MEDIASUBTYPE_RGB24
	const GUID *pSubType = pMediaType->Subtype();
	if (*pSubType != MEDIASUBTYPE_RGB32 && *pSubType != MEDIASUBTYPE_RGB24) {
		NOTE("Invalid video media subtype");
		return E_INVALIDARG;
	}

	return NOERROR;
}

//######################################
// This bit is almost straight out of the base classes.
// We override this so we can handle Transform( )'s error
// result differently.
//######################################
HRESULT CSpoutGrabber::Receive (IMediaSample * pMediaSample) {
	CheckPointer(pMediaSample, E_POINTER);

	AM_SAMPLE2_PROPERTIES * const pProps = m_pInput->SampleProps();
	if (pProps->dwStreamId != AM_STREAM_MEDIA) {
		if( m_pOutput->IsConnected() )
			return m_pOutput->Deliver(pMediaSample);
		else
			return NOERROR;
	}

	// get sample data, pass to SpoutSender
	if (g_hwnd) {
		
		CAutoLock lock(&m_Lock);
		
		PBYTE pbData;
		HRESULT hr = pMediaSample->GetPointer(&pbData);
		if (FAILED(hr)) return hr;

		//mymutex.lock();
		SendMessage(g_hwnd, WM_FRAMECHANGED, (WPARAM)pbData, 0);
		//mymutex.unlock();
	}

	return m_pOutput->Deliver(pMediaSample);
}

//######################################
// Transform
//######################################
//HRESULT CSpoutGrabber::Transform (IMediaSample * pMediaSample) {
//    return NOERROR;
//}

//######################################
// SetAcceptedMediaType
//######################################
STDMETHODIMP CSpoutGrabber::SetAcceptedMediaType (const CMediaType * pMediaType) {
	CAutoLock lock(&m_Lock);
	if (!pMediaType) {
		m_mtAccept = CMediaType();
		return NOERROR;
	}
	HRESULT hr;
	hr = CopyMediaType(&m_mtAccept, pMediaType);
	return hr;
}

//######################################
// GetAcceptedMediaType
//######################################
STDMETHODIMP CSpoutGrabber::GetConnectedMediaType (CMediaType * pMediaType) {
	if (!m_pInput || !m_pInput->IsConnected()) return VFW_E_NOT_CONNECTED;
	return m_pInput->ConnectionMediaType(pMediaType);
}

//######################################
// used to help speed input pin connection times. We return a partially
// specified media type - only the main type is specified. If we return
// anything BUT a major type, some codecs written improperly will crash
//######################################
HRESULT CSpoutGrabberInPin::GetMediaType (int iPosition, CMediaType * pMediaType) {
	CheckPointer(pMediaType, E_POINTER);

	if (iPosition < 0) return E_INVALIDARG;
	if (iPosition > 0) return VFW_S_NO_MORE_ITEMS;

	*pMediaType = CMediaType( );
	pMediaType->SetType( ((CSpoutGrabber*)m_pFilter)->m_mtAccept.Type( ) );

	return S_OK;
}

//######################################
// override the CTransInPlaceInputPin's method, and return a new enumerator
// if the input pin is disconnected. This will allow GetMediaType to be
// called. If we didn't do this, EnumMediaTypes returns a failure code
// and GetMediaType is never called.
//######################################
STDMETHODIMP CSpoutGrabberInPin::EnumMediaTypes (IEnumMediaTypes **ppEnum) {
	CheckPointer(ppEnum, E_POINTER);
	ValidateReadWritePtr(ppEnum,sizeof(IEnumMediaTypes *));

	// if the output pin isn't connected yet, offer the possibly
	// partially specified media type that has been set by the user
	if( !((CSpoutGrabber*)m_pTIPFilter)->OutputPin( )->IsConnected() ) {
		// Create a new reference counted enumerator
		*ppEnum = new CEnumMediaTypes( this, NULL );
		return (*ppEnum) ? NOERROR : E_OUTOFMEMORY;
	}

	// if the output pin is connected, offer it's fully qualified media type
	return ((CSpoutGrabber*)m_pTIPFilter)->OutputPin( )->GetConnected()->EnumMediaTypes( ppEnum );
}

//######################################
//
//######################################
HRESULT CSpoutGrabberInPin::SetMediaType (const CMediaType *pMediaType) {

	m_bMediaTypeChanged = TRUE;

	// Has the video size changed between connections
	VIDEOINFOHEADER *pVideoInfo = (VIDEOINFOHEADER *) pMediaType->Format();

	if (
		pVideoInfo->bmiHeader.biWidth == g_videoSize.cx &&
		pVideoInfo->bmiHeader.biHeight == g_videoSize.cy &&
		pVideoInfo->bmiHeader.biBitCount / 8 == g_dataSize
	) {
		return NOERROR;
	}
	
	//mymutex.lock();

	g_videoSize.cx = pVideoInfo->bmiHeader.biWidth;
	g_videoSize.cy = pVideoInfo->bmiHeader.biHeight;
	g_videoDepth = pVideoInfo->bmiHeader.biBitCount / 8;
	g_dataSize = g_videoSize.cx * g_videoSize.cy * g_videoDepth;

	//mymutex.unlock();

	return CTransInPlaceInputPin::SetMediaType(pMediaType);
}

////////////////////////////////////////////////////////////////////////
// Exported entry points for registration and unregistration
// (in this case they only call through to default implementations).
////////////////////////////////////////////////////////////////////////

//######################################
// DllRegisterSever
//######################################
STDAPI DllRegisterServer () {
	return AMovieDllRegisterServer2(TRUE);
}

//######################################
// DllUnregisterServer
//######################################
STDAPI DllUnregisterServer () {
	return AMovieDllRegisterServer2(FALSE);
}

//######################################
// DllEntryPoint
//######################################
extern "C" BOOL WINAPI DllEntryPoint (HINSTANCE, ULONG, LPVOID);
BOOL APIENTRY DllMain(HANDLE hModule, DWORD  dwReason, LPVOID lpReserved) {
	return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}
