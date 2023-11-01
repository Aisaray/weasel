module;
#include "stdafx.h"
#include <WeaselCommon.h>
#include "test.h"
#ifdef TEST
#define WEASEL_ENABLE_LOGGING
#include "logging.h"
#endif // TEST
module WeaselTSF;
import CandidateList;
import ResponseParser;
import WeaselUtility;
import Compartment;

STDAPI WeaselTSF::DoEditSession(TfEditCookie ec)
{
	// get commit string from server
	std::wstring commit;
	weasel::Config config;
	auto context = std::make_shared<weasel::Context>();
	weasel::ResponseParser parser(&commit, context.get(), &_status, &config, &_cand->style());

	bool ok = m_client.GetResponseData(std::ref(parser));
	auto state = _UpdateLanguageBar(_status);

#ifdef TEST
	LOG(INFO) << std::format("From WeaselTSF::DoEditSession. state = {}, ok = {}", state, ok);
#endif // TEST

	if (GetBit(18))		// _bitset[18]: _Eaten
	{
		_StartComposition(_pEditSessionContext, !config.inline_preedit);		
		_EndComposition(_pEditSessionContext, false);
		ReSetBit(18);	// _bitset[18]: _Eaten
	}
	else if (ok)
	{
		if (state && !_IsComposing())
		{
			_StartComposition(_pEditSessionContext, !config.inline_preedit);
			if (GetBit(17))								// _bitset[17]: _CaretFollowing
			{
				_UpdateCompositionWindow(_pEditSessionContext);
			}
			else
			{
				_SetCompositionPosition(m_rcFallback);
			}
			_EndComposition(_pEditSessionContext, true);
		}
		else
		{
			if (!commit.empty())
			{
				// For auto-selecting, commit and preedit can both exist.
				// Commit and close the original composition first.
				if (!_IsComposing()) {
					_StartComposition(_pEditSessionContext, !config.inline_preedit);
				}
				_InsertText(_pEditSessionContext, commit);
				_EndComposition(_pEditSessionContext, false);
			}
			if (_status.composing && !_IsComposing())
			{
				_StartComposition(_pEditSessionContext, !config.inline_preedit);
			}
			else if (!_status.composing && _IsComposing())
			{
				_EndComposition(_pEditSessionContext, true);
			}
			if (_IsComposing() && config.inline_preedit)
			{
				_ShowInlinePreedit(_pEditSessionContext, context);
				SetBit(5);					// _bitset[5]: _InlinePreedit
			}
			if (GetBit(17))					// _bitset[17]: _CaretFollowing
			{
				_UpdateCompositionWindow(_pEditSessionContext);
			}
			else
			{
				_SetCompositionPosition(m_rcFallback);
			}
		}
	}
	_UpdateUI(*context, _status);

	return TRUE;
}

