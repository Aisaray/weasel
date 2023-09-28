#include "stdafx.h"
#include "InstallOptionsDialog.h"
#include <dwmapi.h>

#pragma comment(lib, "Dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

int uninstall(bool silent, bool ime = false);

InstallOptionsDialog::InstallOptionsDialog()
	: installed{}, hant{}, ime{}, user_dir{}
{
}

InstallOptionsDialog::~InstallOptionsDialog()
{
}

LRESULT InstallOptionsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	BOOL value{ TRUE };
	::DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));

	DWM_SYSTEMBACKDROP_TYPE systemBackDrop{ DWMSBT_MAINWINDOW };
	::DwmSetWindowAttribute(m_hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &systemBackDrop, sizeof(DWM_SYSTEMBACKDROP_TYPE));

	cn_.Attach(GetDlgItem(IDC_RADIO_CN));
	tw_.Attach(GetDlgItem(IDC_RADIO_TW));
	remove_.Attach(GetDlgItem(IDC_REMOVE));
	default_dir_.Attach(GetDlgItem(IDC_RADIO_DEFAULT_DIR));
	custom_dir_.Attach(GetDlgItem(IDC_RADIO_CUSTOM_DIR));
	dir_.Attach(GetDlgItem(IDC_EDIT_DIR));

	CheckRadioButton(IDC_RADIO_CN, IDC_RADIO_TW,
		(hant ? IDC_RADIO_TW : IDC_RADIO_CN));
	CheckRadioButton(IDC_RADIO_DEFAULT_DIR, IDC_RADIO_CUSTOM_DIR,
		(user_dir.empty() ? IDC_RADIO_DEFAULT_DIR : IDC_RADIO_CUSTOM_DIR));
	dir_.SetWindowTextW(user_dir.c_str());

	cn_.EnableWindow(!installed);
	tw_.EnableWindow(!installed);
	remove_.EnableWindow(installed);
	dir_.EnableWindow(user_dir.empty() ? FALSE : TRUE);

	GetDlgItem(IDOK).SetWindowTextW(installed ? L"����" : L"��װ");

	CenterWindow();
	return 0;
}

LRESULT InstallOptionsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
	EndDialog(IDCANCEL);
	return 0;
}

LRESULT InstallOptionsDialog::OnOK(WORD, WORD code, HWND, BOOL&)
{
	hant = (IsDlgButtonChecked(IDC_RADIO_TW) == BST_CHECKED);
	if (IsDlgButtonChecked(IDC_RADIO_CUSTOM_DIR) == BST_CHECKED) 
	{
		CStringW text;
		dir_.GetWindowTextW(text);
		user_dir = text;
	}
	else 
	{
		user_dir.clear();
	}
	EndDialog(IDOK);
	return 0;
}

LRESULT InstallOptionsDialog::OnRemove(WORD, WORD code, HWND, BOOL&) {
	const bool non_silent = false;
	uninstall(non_silent, ime);
	installed = false;
	cn_.EnableWindow(!installed);
	tw_.EnableWindow(!installed);
	remove_.EnableWindow(installed);
	GetDlgItem(IDOK).SetWindowTextW(L"��װ");
	return 0;
}

LRESULT InstallOptionsDialog::OnUseDefaultDir(WORD, WORD code, HWND, BOOL&) {
	dir_.EnableWindow(FALSE);
	return 0;
}

LRESULT InstallOptionsDialog::OnUseCustomDir(WORD, WORD code, HWND, BOOL&) {
	dir_.EnableWindow(TRUE);
	dir_.SetFocus();
	return 0;
}