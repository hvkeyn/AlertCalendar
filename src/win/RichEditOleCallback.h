#pragma once

#include <windows.h>
#include <richedit.h> // for CHARRANGE etc.
// richole.h expects COM/OLE declarations/macros to be available
#include <ole2.h>
#include <richole.h>

// Minimal IRichEditOleCallback implementation so RichEdit can load embedded objects/images
// when streaming RTF containing \objdata / OLE storage.
class RichEditOleCallback : public IRichEditOleCallback {
public:
  RichEditOleCallback() = default;

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == IID_IRichEditOleCallback) {
      *ppvObject = static_cast<IRichEditOleCallback*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG r = --m_ref;
    if (r == 0) delete this;
    return r;
  }

  // IRichEditOleCallback
  HRESULT STDMETHODCALLTYPE GetNewStorage(LPSTORAGE* lplpstg) override {
    if (!lplpstg) return E_POINTER;
    *lplpstg = nullptr;

    ILockBytes* lockBytes = nullptr;
    HRESULT hr = CreateILockBytesOnHGlobal(nullptr, TRUE, &lockBytes);
    if (FAILED(hr) || !lockBytes) return FAILED(hr) ? hr : E_FAIL;

    // Create a docfile on the lockbytes
    IStorage* storage = nullptr;
    hr = StgCreateDocfileOnILockBytes(
      lockBytes,
      STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_READWRITE,
      0,
      &storage
    );
    lockBytes->Release();
    if (FAILED(hr) || !storage) return FAILED(hr) ? hr : E_FAIL;

    *lplpstg = storage; // caller owns
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetInPlaceContext(LPOLEINPLACEFRAME* lplpFrame,
                                              LPOLEINPLACEUIWINDOW* lplpDoc,
                                              LPOLEINPLACEFRAMEINFO lpFrameInfo) override {
    (void)lplpFrame;
    (void)lplpDoc;
    (void)lpFrameInfo;
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE ShowContainerUI(BOOL fShow) override {
    (void)fShow;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE QueryInsertObject(LPCLSID lpclsid, LPSTORAGE lpstg, LONG cp) override {
    (void)lpclsid;
    (void)lpstg;
    (void)cp;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DeleteObject(LPOLEOBJECT lpoleobj) override {
    (void)lpoleobj;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE QueryAcceptData(LPDATAOBJECT lpdataobj, CLIPFORMAT* lpcfFormat,
                                            DWORD reco, BOOL fReally, HGLOBAL hMetaPict) override {
    (void)lpdataobj;
    (void)lpcfFormat;
    (void)reco;
    (void)fReally;
    (void)hMetaPict;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL fEnterMode) override {
    (void)fEnterMode;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetClipboardData(CHARRANGE* lpchrg, DWORD reco, LPDATAOBJECT* lplpdataobj) override {
    (void)lpchrg;
    (void)reco;
    (void)lplpdataobj;
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE GetDragDropEffect(BOOL fDrag, DWORD grfKeyState, LPDWORD pdwEffect) override {
    (void)fDrag;
    (void)grfKeyState;
    if (pdwEffect) *pdwEffect = DROPEFFECT_NONE;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetContextMenu(WORD seltype, LPOLEOBJECT lpoleobj, CHARRANGE* lpchrg, HMENU* lphmenu) override {
    (void)seltype;
    (void)lpoleobj;
    (void)lpchrg;
    if (lphmenu) *lphmenu = nullptr;
    return S_OK;
  }

private:
  ULONG m_ref = 1;
};

