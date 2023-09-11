module;
#include "stdafx.h"
#include <WeaselUI.h>
#include <WeaselCommon.h>
export module LanguageBar;
import WeaselTSF;

export
{
	class CLangBarItemButton : public ITfLangBarItemButton, public ITfSource
	{
	public:
		// com_ptr����ָ����Ϊ�������ͻᴥ��COM���ü���������ʱ��־�����Ϊ���COM���ü�������ݼ���0
		// �ʽ������͸�Ϊ������
		CLangBarItemButton(WeaselTSF& textService, REFGUID guid, weasel::UIStyle& style);
		~CLangBarItemButton();

		/* IUnknown */
		STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject);
		STDMETHODIMP_(ULONG) AddRef();
		STDMETHODIMP_(ULONG) Release();

		/* ITfLangBarItem */
		STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo);
		STDMETHODIMP GetStatus(DWORD* pdwStatus);
		STDMETHODIMP Show(BOOL fShow);
		STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip);

		/* ITfLangBarItemButton */
		STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea);
		STDMETHODIMP InitMenu(ITfMenu* pMenu);
		STDMETHODIMP OnMenuSelect(UINT wID);
		STDMETHODIMP GetIcon(HICON* phIcon);
		STDMETHODIMP GetText(BSTR* pbstrText);

		/* ITfSource */
		STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie);
		STDMETHODIMP UnadviseSink(DWORD dwCookie);

		void UpdateWeaselStatus(weasel::Status stat);
		void SetLangbarStatus(DWORD dwStatus, BOOL fSet);
		void RightClick(POINT& pt);

	private:
		WeaselTSF& _textService;
		GUID _guid;
		weasel::UIStyle& _style;
		com_ptr<ITfLangBarItemSink> _pLangBarItemSink;
		LONG _cRef; /* COM Reference count */
		DWORD _status;
		bool ascii_mode;
		std::wstring _current_schema_zhung_icon;
		std::wstring _current_schema_ascii_icon;
	};
}