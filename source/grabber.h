#include <streams.h>
#include "Spout.h"

// We define the interface the app can use to program us
MIDL_INTERFACE("6B652FFF-11FE-4FCE-92AD-0266B5D7C78F")
ISpoutGrabber : public IUnknown {
	public:
		virtual HRESULT STDMETHODCALLTYPE SetAcceptedMediaType (const CMediaType *pType) = 0;
		virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType (CMediaType *pType) = 0;
};

class CSpoutGrabberInPin;
class CSpoutGrabber;

//----------------------------------------------------------------------------
// we override the input pin class so we can provide a media type
// to speed up connection times. When you try to connect a filesourceasync
// to a transform filter, DirectShow will insert a splitter and then
// start trying codecs, both audio and video, video codecs first. If
// your sample grabber's set to connect to audio, unless we do this, it
// will try all the video codecs first. Connection times are sped up x10
// for audio with just this minor modification!
//----------------------------------------------------------------------------
class CSpoutGrabberInPin : public CTransInPlaceInputPin {
	friend class CSpoutGrabber;

	BOOL m_bMediaTypeChanged;

public:

	CSpoutGrabberInPin (CTransInPlaceFilter * pFilter, HRESULT * pHr)
		: CTransInPlaceInputPin (TEXT("SpoutGrabberInputPin\0"), pFilter, pHr, L"Input\0")
		, m_bMediaTypeChanged (FALSE)
	{}

	// override to provide major media type for fast connects
	HRESULT GetMediaType (int iPosition, CMediaType *pMediaType);
	// override this or GetMediaType is never called
	STDMETHODIMP EnumMediaTypes (IEnumMediaTypes **ppEnum);
	HRESULT SetMediaType (const CMediaType *pMediaType);
};

//----------------------------------------------------------------------------
//
//----------------------------------------------------------------------------
class CSpoutGrabber : public CTransInPlaceFilter, public ISpoutGrabber {
	friend class CSpoutGrabberInPin;

protected:

	CMediaType m_mtAccept;
	CCritSec m_Lock; // serialize access to our data

	BOOL IsReadOnly () { return !m_bModifiesData; }

	// PURE, override this to ensure we get connected with the right media type
	HRESULT CheckInputType (const CMediaType * pMediaType);

	// PURE, override this to callback the user when a sample is received
	HRESULT Transform (IMediaSample * pMediaSample)  { return NOERROR; }

	// The base class CTransInPlaceFilter Transform() method is called by it's Receive() method
	HRESULT Receive (IMediaSample * pMediaSample);

public:

	static CUnknown *WINAPI CreateInstance (LPUNKNOWN punk, HRESULT *phr);

	// Expose ISpoutGrabber
	STDMETHODIMP NonDelegatingQueryInterface (REFIID riid, void ** ppv);
	DECLARE_IUNKNOWN;

	CSpoutGrabber (IUnknown * pOuter, HRESULT * pHr, BOOL ModifiesData);
	~CSpoutGrabber ();

	// ISpoutGrabber
	STDMETHODIMP SetAcceptedMediaType (const CMediaType * pMediaType);
	STDMETHODIMP GetConnectedMediaType (CMediaType * pMediaType);
};
