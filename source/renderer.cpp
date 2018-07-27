#include "renderer.h"
#include <initguid.h>
#include <mutex>

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
std::mutex mymutex;

//######################################
// GUIDs
//######################################

DEFINE_GUID(CLSID_SpoutRenderer,
	0x19b0e3a6, 0xe681, 0x4af6, 0xa1, 0xe, 0xbe, 0x2, 0xc5, 0xd2, 0x5e, 0x3f);

//######################################
// Setup data
//######################################

const AMOVIESETUP_MEDIATYPE sudPinTypes = {
	&MEDIATYPE_Video,            // Major type
	&MEDIASUBTYPE_NULL          // Minor type
};

const AMOVIESETUP_PIN sudPins = {
	L"Input",                   // Name of the pin
	FALSE,                      // Is pin rendered
	FALSE,                      // Is an output pin
	FALSE,                      // Ok for no pins
	FALSE,                      // Allowed many
	&CLSID_NULL,                // Connects to filter
	L"Output",                  // Connects to pin
	1,                          // Number of pin types
	&sudPinTypes                // Details for pins
};

const AMOVIESETUP_FILTER sudSampVid = {
	&CLSID_SpoutRenderer,      // Filter CLSID
	L"SpoutRenderer",          // Filter name
	MERIT_DO_NOT_USE,          // Filter merit
	1,                         // Number pins
	&sudPins                   // Pin details
};

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
// List of class IDs and creator functions for the class factory. This
// provides the link between the OLE entry point in the DLL and an object
// being created. The class factory will call the static CreateInstance
//######################################
CFactoryTemplate g_Templates[] = {
	{ L"SpoutRenderer"
	, &CLSID_SpoutRenderer
	, CVideoRenderer::CreateInstance
	, NULL
	, &sudSampVid }
};
int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

//######################################
// CreateInstance
// This goes in the factory template table to create new filter instances
//######################################
CUnknown * WINAPI CVideoRenderer::CreateInstance (LPUNKNOWN pUnk, HRESULT *phr){
	return new CVideoRenderer(NAME("SpoutRenderer"), pUnk, phr);
}

//######################################
// The message handler for the hidden dummy window
//######################################
LRESULT CALLBACK DLLWindowProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
		break;

	//case WM_SIZECHANGED:
	//	bool spoutInitialized;
	//	spoutInitialized = g_spoutSender->UpdateSender("SpoutRenderer", g_videoSize.cx, g_videoSize.cy);
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
		return 0;
		break;

	default:
		return DefWindowProcW(hwnd, message, wParam, lParam);
		break;

	}
}

//######################################
// The new window/OpenGL thread
//######################################
DWORD WINAPI ThreadProc (void * data) {

	int exitCode = 0;

	HDC hdc = NULL;
	HGLRC hRc = NULL;
	HGLRC glContext;

	//######################################
	// Create dummy window for OpenGL context
	//######################################

	HINSTANCE inj_hModule = GetModuleHandle(NULL);

	// register the windows class
	WNDCLASSEXA wc = { 0 };
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.hInstance = inj_hModule;
	wc.lpszClassName = "SpoutRenderer";
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
		ErrorMessage("RegisterClassExW");
		++exitCode;
		goto cleanup;
	}

	g_hwnd = CreateWindowExA(
		0, // dwExStyle
		"SpoutRenderer",
		"SpoutRenderer",
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
	bool spoutInitialized = g_spoutSender->CreateSender("SpoutRenderer", g_videoSize.cx, g_videoSize.cy);
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
// Constructor
//######################################
CVideoRenderer::CVideoRenderer (TCHAR *pName, LPUNKNOWN pUnk, HRESULT *phr) :
	CBaseVideoRenderer(CLSID_SpoutRenderer, pName, pUnk, phr),
	m_InputPin(NAME("Video Pin"), this, &m_InterfaceLock,phr, L"Input")
{
	// Store the video input pin
	m_pInputPin = &m_InputPin;
	g_thread = CreateThread(0, 0, ThreadProc, NULL, 0, 0);
}

//######################################
// Destructor
//######################################
CVideoRenderer::~CVideoRenderer (){
	SendMessage(g_hwnd, WM_DESTROY, 0, 0);
	WaitForSingleObject(g_thread, INFINITE);
	g_thread = NULL;
	m_pInputPin = NULL;
}

 //######################################
// CheckMediaType
// Check the proposed video media type
//######################################
HRESULT CVideoRenderer::CheckMediaType(const CMediaType *pmtIn){

	// Does this have a VIDEOINFOHEADER format block
	const GUID *pFormatType = pmtIn->FormatType();
	if (*pFormatType != FORMAT_VideoInfo) {
		NOTE("Format GUID not a VIDEOINFOHEADER");
		return E_INVALIDARG;
	}
	ASSERT(pmtIn->Format());

	// Check the format looks reasonably ok
	ULONG Length = pmtIn->FormatLength();
	if (Length < SIZE_VIDEOHEADER) {
		NOTE("Format smaller than a VIDEOHEADER");
		return E_FAIL;
	}

	VIDEOINFO *pInput = (VIDEOINFO *)pmtIn->Format();

	// Check the major type is MEDIATYPE_Video
	const GUID *pMajorType = pmtIn->Type();
	if (*pMajorType != MEDIATYPE_Video) {
		NOTE("Major type not MEDIATYPE_Video");
		return E_INVALIDARG;
	}

	// Check we can identify the media subtype
	const GUID *pSubType = pmtIn->Subtype();
	if (GetBitCount(pSubType) == USHRT_MAX) {
		NOTE("Invalid video media subtype");
		return E_INVALIDARG;
	}

	// We only accept 24 and 32 bit
	if (pInput->bmiHeader.biBitCount!=32 && pInput->bmiHeader.biBitCount!=24) {
		NOTE("Invalid video biBitCount");
		return E_INVALIDARG;
	}

	return NOERROR;
}

//######################################
// GetPin
// We only support one input pin and it is numbered zero
//######################################
CBasePin *CVideoRenderer::GetPin (int n){
	ASSERT(n == 0);
	if (n != 0) return NULL;

	// Assign the input pin if not already done so
	if (m_pInputPin == NULL) {
		m_pInputPin = &m_InputPin;
	}

	return m_pInputPin;
}

//######################################
// DoRenderSample
// render the current image
//######################################
HRESULT CVideoRenderer::DoRenderSample (IMediaSample *pMediaSample) {

	CheckPointer(pMediaSample, E_POINTER);
	CAutoLock cInterfaceLock(&m_InterfaceLock);

	PBYTE pbData;
	HRESULT hr = pMediaSample->GetPointer(&pbData);
	if (FAILED(hr)) return hr;

	if (g_hwnd) {
		mymutex.lock();
		SendMessage(g_hwnd, WM_FRAMECHANGED, (WPARAM)pbData, 0);
		mymutex.unlock();
	}

	return S_OK;
}

//######################################
// SetMediaType
// We store a copy of the media type used for the connection in the renderer
// because it is required by many different parts of the running renderer
// This can be called when we come to draw a media sample that has a format
// change with it. We normally delay type changes until they are really due
// for rendering otherwise we will change types too early if the source has
// allocated a queue of samples. In our case this isn't a problem because we
// only ever receive one sample at a time so it's safe to change immediately
//######################################
HRESULT CVideoRenderer::SetMediaType (const CMediaType *pmt){
	CheckPointer(pmt, E_POINTER);
	HRESULT hr = NOERROR;
	CAutoLock cInterfaceLock(&m_InterfaceLock);
	CMediaType StoreFormat(m_mtIn);
	m_mtIn = *pmt;
	return NOERROR;
}

//######################################
// BreakConnect
// This is called when a connection or an attempted connection is terminated
// and lets us to reset the connection flag held by the base class renderer
// The filter object may be hanging onto an image to use for refreshing the
// video window so that must be freed (the allocator decommit may be waiting
// for that image to return before completing) then we must also uninstall
// any palette we were using, reset anything set with the control interfaces
// then set our overall state back to disconnected ready for the next time
//######################################
HRESULT CVideoRenderer::BreakConnect (){
	CAutoLock cInterfaceLock(&m_InterfaceLock);

	// Check we are in a valid state
	HRESULT hr = CBaseVideoRenderer::BreakConnect();
	if (FAILED(hr)) return hr;

	// The window is not used when disconnected
	IPin *pPin = m_InputPin.GetConnected();
	if (pPin) SendNotifyWindow(pPin, NULL);

	return NOERROR;
}

//######################################
// CompleteConnect
// When we complete connection we need to see if the video has changed sizes
// If it has then we activate the window and reset the source and destination
// rectangles. If the video is the same size then we bomb out early. By doing
// this we make sure that temporary disconnections such as when we go into a
// fullscreen mode do not cause unnecessary property changes. The basic ethos
// is that all properties should be persistent across connections if possible
//######################################
HRESULT CVideoRenderer::CompleteConnect (IPin *pReceivePin){

	CAutoLock cInterfaceLock(&m_InterfaceLock);

	CBaseVideoRenderer::CompleteConnect(pReceivePin);

	// Has the video size changed between connections
	VIDEOINFOHEADER *pVideoInfo = (VIDEOINFOHEADER *) m_mtIn.Format();

	if (pVideoInfo->bmiHeader.biWidth == g_videoSize.cx){
		if (pVideoInfo->bmiHeader.biHeight == g_videoSize.cy){
			return NOERROR;
		}
	}

	mymutex.lock();

	g_videoSize.cx = pVideoInfo->bmiHeader.biWidth;
	g_videoSize.cy = pVideoInfo->bmiHeader.biHeight;
	g_videoDepth = pVideoInfo->bmiHeader.biBitCount / 8;
	g_dataSize = g_videoSize.cx * g_videoSize.cy * g_videoDepth;

	mymutex.unlock();

	return NOERROR;
}

//######################################
// Constructor
//######################################
CVideoInputPin::CVideoInputPin (TCHAR *pObjectName,
		CVideoRenderer *pRenderer,
		CCritSec *pInterfaceLock,
		HRESULT *phr,
		LPCWSTR pPinName) :
	CRendererInputPin(pRenderer, phr, pPinName),
	m_pRenderer(pRenderer),
	m_pInterfaceLock(pInterfaceLock)
{
	ASSERT(m_pRenderer);
	ASSERT(pInterfaceLock);
}

////////////////////////////////////////////////////////////////////////
// Exported entry points for registration and unregistration
// (in this case they only call through to default implementations).
////////////////////////////////////////////////////////////////////////

//######################################
// DllRegisterSever
//######################################
STDAPI DllRegisterServer (){
	return AMovieDllRegisterServer2(TRUE);
}

//######################################
// DllUnregisterServer
//######################################
STDAPI DllUnregisterServer (){
	return AMovieDllRegisterServer2(FALSE);
}

//######################################
// DllEntryPoint
//######################################
extern "C" BOOL WINAPI DllEntryPoint (HINSTANCE, ULONG, LPVOID);
BOOL APIENTRY DllMain(HANDLE hModule, DWORD  dwReason, LPVOID lpReserved){
	return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}

