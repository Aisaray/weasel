module;
#include "stdafx.h"
#include "Globals.h"
module WeaselTSF;

void WeaselTSF::_ClearCompositionDisplayAttributes(TfEditCookie ec, _In_ ITfContext* pContext)
{
    ITfRange* pRangeComposition = nullptr;
    ITfProperty* pDisplayAttributeProperty = nullptr;

    if (FAILED(_pComposition->GetRange(&pRangeComposition)))
    {
        return;
    }

    if (SUCCEEDED(pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pDisplayAttributeProperty)))
    {
        pDisplayAttributeProperty->Clear(ec, pRangeComposition);

        pDisplayAttributeProperty->Release();
    }

    pRangeComposition->Release();
}

BOOL WeaselTSF::_SetCompositionDisplayAttributes(TfEditCookie ec, _In_ ITfContext* pContext, ITfRange *pRangeComposition)
{
    ITfProperty* pDisplayAttributeProperty = nullptr;
    HRESULT hr = S_OK;

    if (pRangeComposition == nullptr)
        hr = _pComposition->GetRange(&pRangeComposition);
    if (FAILED(hr))
    {
        return FALSE;
    }

    hr = E_FAIL;

    if (SUCCEEDED(pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pDisplayAttributeProperty)))
    {
        VARIANT var;
        var.vt = VT_I4; // we're going to set a TfGuidAtom
        var.lVal = _gaDisplayAttributeInput;

        hr = pDisplayAttributeProperty->SetValue(ec, pRangeComposition, &var);

        pDisplayAttributeProperty->Release();
    }

    // DO NOT release range composition here
    // it will be released in another function
    // pRangeComposition->Release();
    return (hr == S_OK);
}

BOOL WeaselTSF::_InitDisplayAttributeGuidAtom()
{
    com_ptr<ITfCategoryMgr> pCategoryMgr;
    HRESULT hr = _pThreadMgr->QueryInterface(&pCategoryMgr);

    if (FAILED(hr))
    {
        return FALSE;
    }

    hr = pCategoryMgr->RegisterGUID(c_guidDisplayAttributeInput, &_gaDisplayAttributeInput);
    if (FAILED(hr))
    {
        goto Exit;
    }

Exit:
    return (hr == S_OK);
}
