﻿module;
#include "stdafx.h"
#include <WeaselUI.h>
#include <logging.h>
#include <WeaselVersion.h>
#include <VersionHelpers.hpp>
#include <rime_api.h>
#include <format>
#include <filesystem>

module RimeWithWeasel;
import WeaselUtility;
import StringAlgorithm;

namespace fs = std::filesystem;
using weasel::UIStyle;

#define TRANSPARENT_COLOR	0x00000000
#define ARGB2ABGR(value)	((value & 0xff000000) | ((value & 0x000000ff) << 16) | (value & 0x0000ff00) | ((value & 0x00ff0000) >> 16)) 
#define RGBA2ABGR(value)    (((value & 0xff) << 24) | ((value & 0xff000000) >> 24) | ((value & 0x00ff0000) >> 8) | ((value & 0x0000ff00) << 8))
typedef enum
{
	COLOR_ABGR = 0,
	COLOR_ARGB,
	COLOR_RGBA
} ColorFormat;

#ifdef USE_SHARP_COLOR_CODE
#define HEX_REGEX		std::regex("^(0x|#)[0-9a-f]+$", std::regex::icase)
#define TRIMHEAD_REGEX	std::regex("0x|#", std::regex::icase)
#else
#define HEX_REGEX		std::regex("^0x[0-9a-f]+$", std::regex::icase)
#define TRIMHEAD_REGEX	std::regex("0x", std::regex::icase)
#endif

int expand_ibus_modifier(int m)
{
	return (m & 0xff) | ((m & 0xff00) << 16);
}

RimeWithWeaselHandler::RimeWithWeaselHandler(weasel::UI* ui)
	: m_ui(ui)
	, m_active_session(0)
	, m_disabled(true)
	, m_current_dark_mode{ false }
	, m_show_notifications_time{ 1200 }
	, _UpdateUICallback(nullptr)
{
	_Setup();
}

RimeWithWeaselHandler::~RimeWithWeaselHandler()
{
	m_show_notifications.clear();
	m_session_status_map.clear();
	m_app_options.clear();
}

void _UpdateUIStyle(RimeConfig* config, weasel::UI* ui, bool initialize);
bool _UpdateUIStyleColor(RimeConfig* config, weasel::UIStyle& style, std::string color = "");
void _LoadAppOptions(RimeConfig* config, AppOptionsByAppName& app_options);

void _RefreshTrayIcon(const RimeSessionId session_id, const std::function<void()> _UpdateUICallback)
{
	// Dangerous, don't touch
	static char app_name[50];
	RimeGetProperty(session_id, "client_app", app_name, sizeof(app_name) - 1);
	if (to_wstring(app_name, CP_UTF8) == L"explorer.exe")
	{
		std::thread th{ [=]()
			{
				::Sleep(100);
				if (_UpdateUICallback)
				{
					_UpdateUICallback();
				}
			} };
		th.detach();
	}
	else if (_UpdateUICallback)
	{
		_UpdateUICallback();
	}
}

void RimeWithWeaselHandler::_Setup()
{
	RIME_STRUCT(RimeTraits, weasel_traits);
	auto shared_dir{ weasel_shared_data_dir() };
	auto user_dir{ weasel_user_data_dir() };
	auto log_dir{ weasel_log_dir() };
	weasel_traits.shared_data_dir = shared_dir.data();
	weasel_traits.user_data_dir = user_dir.data();
	weasel_traits.prebuilt_data_dir = weasel_traits.shared_data_dir;
	auto name{ to_string(WEASEL_IME_NAME, CP_UTF8) };
	weasel_traits.distribution_name = name.data();
	weasel_traits.distribution_code_name = WEASEL_CODE_NAME;
	weasel_traits.distribution_version = WEASEL_VERSION;
	weasel_traits.app_name = "rime.weasel";
	weasel_traits.log_dir = log_dir.data();
	RimeSetup(&weasel_traits);
	RimeSetNotificationHandler(&RimeWithWeaselHandler::OnNotify, this);
}

void RimeWithWeaselHandler::Initialize()
{
	m_disabled = _IsDeployerRunning();
	if (m_disabled)
	{
		return;
	}

	LOG(INFO) << "Initializing la rime.";
	RimeInitialize(NULL);
	if (RimeStartMaintenance(/*full_check = */False))
	{
		m_disabled = true;
	}

	RimeConfig config = { NULL };
	if (RimeConfigOpen("weasel", &config))
	{
		if (m_ui)
		{
			_UpdateUIStyle(&config, m_ui, true);
			_UpdateShowNotifications(&config, true);
			m_current_dark_mode = IsUserDarkMode();
			if (m_current_dark_mode)
			{
				std::string color_name(MAX_PATH, 0);
				if (RimeConfigGetString(&config, "style/color_scheme_dark", color_name.data(), color_name.capacity()))
				{
					color_name = color_name.data();
					_UpdateUIStyleColor(&config, m_ui->style(), color_name);
				}
			}
			m_base_style = m_ui->style();
		}
		Bool global_ascii = false;
		if (RimeConfigGetBool(&config, "global_ascii", &global_ascii))
			m_global_ascii_mode = !!global_ascii;
		if (!RimeConfigGetInt(&config, "show_notifications_time", &m_show_notifications_time))
			m_show_notifications_time = 1200;
		_LoadAppOptions(&config, m_app_options);
		RimeConfigClose(&config);
	}
	m_last_schema_id.clear();
}

void RimeWithWeaselHandler::Finalize()
{
	m_active_session = 0;
	m_disabled = true;
	m_session_status_map.clear();
	LOG(INFO) << "Finalizing la rime.";
	RimeFinalize();
}

RimeSessionId RimeWithWeaselHandler::FindSession(RimeSessionId session_id)
{
	if (m_disabled) return 0;
	Bool found = RimeFindSession(session_id);
	DLOG(INFO) << "Find session: session_id = " << session_id << ", found = " << found;
	return found ? session_id : 0;
}

RimeSessionId RimeWithWeaselHandler::AddSession(LPWSTR buffer, EatLine eat)
{
	if (m_disabled)
	{
		DLOG(INFO) << "Trying to resume service.";
		EndMaintenance();
		if (m_disabled) return 0;
	}
	RimeSessionId session_id = RimeCreateSession();
	DLOG(INFO) << "Add session: created session_id = " << session_id;
	_ReadClientInfo(session_id, buffer);

	m_session_status_map[session_id] = SessionStatus();
	m_session_status_map[session_id].style = m_base_style;

	RIME_STRUCT(RimeStatus, status);
	if (RimeGetStatus(session_id, &status))
	{
		std::string schema_id = status.schema_id;
		m_last_schema_id = schema_id;
		_LoadSchemaSpecificSettings(session_id, schema_id);
		_LoadAppInlinePreeditSet(session_id, true);
		_UpdateInlinePreeditStatus(session_id);
		_RefreshTrayIcon(session_id, _UpdateUICallback);
		m_session_status_map[session_id].status = status;
		m_session_status_map[session_id].__synced = false;
		RimeFreeStatus(&status);
	}
	m_ui->style() = m_session_status_map[session_id].style;
	// show session's welcome message :-) if any
	if (eat) {
		_Respond(session_id, eat);
	}
	m_add_session = true;
	_UpdateUI(session_id);
	m_add_session = false;
	m_active_session = session_id;
	return session_id;
}

RimeSessionId RimeWithWeaselHandler::RemoveSession(RimeSessionId session_id)
{
	if (m_ui) m_ui->Hide();
	if (m_disabled) return 0;
	DLOG(INFO) << "Remove session: session_id = " << session_id;
	// TODO: force committing? otherwise current composition would be lost
	RimeDestroySession(session_id);
	m_session_status_map.erase(session_id);
	m_active_session = 0;
	return 0;
}

namespace ibus
{
	enum Keycode
	{
		Escape = 0xFF1B,
		XK_bracketleft = 0x005B,	// U+005B LEFT SQUARE BRACKET
		XK_c = 0x0063,				// U+0063 LATIN SMALL LETTER C
		XK_C = 0x0043				// U+0043 LATIN CAPITAL LETTER
	};
}

BOOL RimeWithWeaselHandler::ProcessKeyEvent(weasel::KeyEvent keyEvent, RimeSessionId session_id, EatLine eat)
{
	DLOG(INFO) << "Process key event: keycode = " << keyEvent.keycode << ", mask = " << keyEvent.mask
		<< ", session_id = " << session_id;
	if (m_disabled) return FALSE;
	Bool handled = RimeProcessKey(session_id, keyEvent.keycode, expand_ibus_modifier(keyEvent.mask));
	if (!handled)
	{
		bool isVimBackInCommandMode = (keyEvent.keycode == ibus::Keycode::Escape) ||
			((keyEvent.mask & (1 << 2)) && (keyEvent.keycode == ibus::Keycode::XK_c ||
				keyEvent.keycode == ibus::Keycode::XK_c ||
				keyEvent.keycode == ibus::Keycode::XK_bracketleft));
		if (isVimBackInCommandMode &&
			RimeGetOption(session_id, "vim_mode") &&
			!RimeGetOption(session_id, "ascii_mode"))
		{
			RimeSetOption(session_id, "ascii_mode", True);
		}
	}
	_Respond(session_id, eat);
	_UpdateUI(session_id);
	m_active_session = session_id;
	return (BOOL)handled;
}

void RimeWithWeaselHandler::CommitComposition(RimeSessionId session_id)
{
	DLOG(INFO) << "Commit composition: session_id = " << session_id;
	if (m_disabled) return;
	RimeCommitComposition(session_id);
	_UpdateUI(session_id);
	m_active_session = session_id;
}

void RimeWithWeaselHandler::ClearComposition(RimeSessionId session_id)
{
	DLOG(INFO) << "Clear composition: session_id = " << session_id;
	if (m_disabled) return;
	RimeClearComposition(session_id);
	_UpdateUI(session_id);
	m_active_session = session_id;
}

void RimeWithWeaselHandler::SelectCandidateOnCurrentPage(const size_t index, RimeSessionId session_id)
{
	DLOG(INFO) << std::format("select candidate on current page, session_id = {}, index = {}", session_id, index);
	if (m_disabled)
		return;

	RimeApi* api = rime_get_api();
	api->select_candidate_on_current_page(session_id, index);
}

void RimeWithWeaselHandler::FocusIn(PARAM client_caps, RimeSessionId session_id)
{
	DLOG(INFO) << "Focus in: session_id = " << session_id << ", client_caps = " << client_caps;
	if (m_disabled) return;
	_UpdateUI(session_id);
	m_active_session = session_id;
}

void RimeWithWeaselHandler::FocusOut(PARAM param, RimeSessionId session_id)
{
	DLOG(INFO) << "Focus out: session_id = " << session_id;
	if (m_ui) m_ui->Hide();
	m_active_session = 0;
}

void RimeWithWeaselHandler::UpdateInputPosition(RECT const& rc, RimeSessionId session_id)
{
	DLOG(INFO) << "Update input position: (" << rc.left << ", " << rc.top
		<< "), session_id = " << session_id << ", m_active_session = " << m_active_session;
	if (m_ui) m_ui->UpdateInputPosition(rc);
	if (m_disabled) return;
	if (m_active_session != session_id)
	{
		_UpdateUI(session_id);
		m_active_session = session_id;
	}
}

std::string RimeWithWeaselHandler::m_message_type;
std::string RimeWithWeaselHandler::m_message_value;
std::string RimeWithWeaselHandler::m_message_label;
std::string RimeWithWeaselHandler::m_option_name;

void RimeWithWeaselHandler::OnNotify(void* context_object,
	RimeSessionId session_id,
	const char* message_type,
	const char* message_value)
{
	// may be running in a thread when deploying rime
	RimeWithWeaselHandler* self = reinterpret_cast<RimeWithWeaselHandler*>(context_object);
	if (!self || !message_type || !message_value) return;
	m_message_type = message_type;
	m_message_value = message_value;
	RimeApi* rime = rime_get_api();
	if (RIME_API_AVAILABLE(rime, get_state_label) &&
		!strcmp(message_type, "option"))
	{
		Bool state = message_value[0] != '!';
		const char* option_name = message_value + !state;
		m_option_name = option_name;
		const char* state_label = rime->get_state_label(session_id, option_name, state);
		if (state_label)
		{
			m_message_label = std::string(state_label);
		}
	}
}

std::string _SearchInMapCaseInsensitive(const AppOptionsByAppName& app_options, const std::string& app_name)
{
	for (const auto& it : app_options)
	{
		if (to_lower(it.first) == app_name)
		{
			return it.first;
		}
	}
	return std::string{ "" };
}

void RimeWithWeaselHandler::_ReadClientInfo(RimeSessionId session_id, LPWSTR buffer)
{
	std::string app_name;
	std::string client_type;
	// parse request text
	wbufferstream bs(buffer, WEASEL_IPC_BUFFER_LENGTH);
	std::wstring line;
	while (bs.good())
	{
		std::getline(bs, line);
		if (!bs.good())
			break;
		// file ends
		if (line == L".")
			break;
		const std::wstring kClientAppKey = L"session.client_app=";
		if (starts_with(line, kClientAppKey))
		{
			std::wstring lwr = line;
			to_lower(lwr);
			app_name = to_string(lwr.substr(kClientAppKey.length()), CP_UTF8);
		}
		const std::wstring kClientTypeKey = L"session.client_type=";
		if (starts_with(line, kClientTypeKey))
		{
			client_type = to_string(line.substr(kClientTypeKey.length()), CP_UTF8);
		}
	}
	// set app specific options
	if (!app_name.empty())
	{
		RimeSetProperty(session_id, "client_app", app_name.c_str());

		if (m_app_options.find(app_name) != m_app_options.end())
		{
			AppOptions& options(m_app_options[app_name]);
			std::for_each(options.begin(), options.end(), [session_id](std::pair<const std::string, bool>& pair)
				{
					DLOG(INFO) << "set app option: " << pair.first << " = " << pair.second;
					RimeSetOption(session_id, pair.first.c_str(), Bool(pair.second));
				});
		}
	}
	// ime | tsf
	RimeSetProperty(session_id, "client_type", client_type.c_str());
	// inline preedit
	bool inline_preedit = m_session_status_map[session_id].style.inline_preedit && (client_type == "tsf");
	RimeSetOption(session_id, "inline_preedit", Bool(inline_preedit));
	// show soft cursor on weasel panel but not inline
	RimeSetOption(session_id, "soft_cursor", Bool(!inline_preedit));
}

void RimeWithWeaselHandler::_GetCandidateInfo(weasel::CandidateInfo& cinfo, RimeContext& ctx)
{
	cinfo.candies.resize(ctx.menu.num_candidates);
	cinfo.comments.resize(ctx.menu.num_candidates);
	cinfo.labels.resize(ctx.menu.num_candidates);
	for (int i = 0; i < ctx.menu.num_candidates; ++i)
	{
		cinfo.candies[i].str = to_wstring(ctx.menu.candidates[i].text, CP_UTF8);
		if (ctx.menu.candidates[i].comment)
		{
			cinfo.comments[i].str = to_wstring(ctx.menu.candidates[i].comment, CP_UTF8);
		}
		if (RIME_STRUCT_HAS_MEMBER(ctx, ctx.select_labels) && ctx.select_labels)
		{
			cinfo.labels[i].str = to_wstring(ctx.select_labels[i], CP_UTF8);
		}
		else if (ctx.menu.select_keys)
		{
			cinfo.labels[i].str = std::wstring(1, ctx.menu.select_keys[i]);
		}
		else
		{
			cinfo.labels[i].str = std::to_wstring((i + 1) % 10);
		}
	}
	cinfo.highlighted = ctx.menu.highlighted_candidate_index;
	cinfo.currentPage = ctx.menu.page_no;
	cinfo.is_last_page = ctx.menu.is_last_page;
}

void RimeWithWeaselHandler::StartMaintenance()
{
	m_session_status_map.clear();
	Finalize();
	_UpdateUI(0);
}

void RimeWithWeaselHandler::EndMaintenance()
{
	if (m_disabled)
	{
		Initialize();
		_UpdateUI(0);
	}
	m_session_status_map.clear();
}

void RimeWithWeaselHandler::SetOption(RimeSessionId session_id, const std::string& opt, bool val)
{
	RimeSetOption(session_id, opt.c_str(), val);
}

void RimeWithWeaselHandler::UpdateColorTheme(bool darkMode)
{
	RimeConfig config{ nullptr };
	if (RimeConfigOpen("weasel", &config))
	{
		if (m_ui)
		{
			_UpdateUIStyle(&config, m_ui, true);
			m_current_dark_mode = darkMode;
			if (darkMode)
			{
				std::string color_name(MAX_PATH, 0);
				if (RimeConfigGetString(&config, "style/color_scheme_dark", color_name.data(), color_name.capacity()))
				{
					color_name = color_name.data();
					_UpdateUIStyleColor(&config, m_ui->style(), color_name);
				}
			}
			m_base_style = m_ui->style();
		}
		RimeConfigClose(&config);
	}

	for (auto& pair : m_session_status_map)
	{
		RIME_STRUCT(RimeStatus, status);
		if (RimeGetStatus(pair.first, &status))
		{
			_LoadSchemaSpecificSettings(pair.first, std::string(status.schema_id));
			_LoadAppInlinePreeditSet(pair.first, true);
			_UpdateInlinePreeditStatus(pair.first);
			pair.second.status = status;
			pair.second.__synced = false;
			RimeFreeStatus(&status);
		}
	}
	m_ui->style() = m_session_status_map[m_active_session].style;
}

void RimeWithWeaselHandler::OnUpdateUI(std::function<void()> const& cb)
{
	_UpdateUICallback = cb;
}

bool RimeWithWeaselHandler::_IsDeployerRunning()
{
	HANDLE hMutex = CreateMutex(NULL, TRUE, L"WeaselDeployerMutex");
	bool deployer_detected = hMutex && GetLastError() == ERROR_ALREADY_EXISTS;
	if (hMutex)
	{
		CloseHandle(hMutex);
	}
	return deployer_detected;
}

void RimeWithWeaselHandler::_UpdateUI(RimeSessionId session_id)
{
	weasel::Status weasel_status;
	weasel::Context weasel_context;

	bool is_tsf = _IsSessionTSF(session_id);

	if (session_id == 0)
		weasel_status.disabled = m_disabled;

	_GetStatus(weasel_status, session_id, weasel_context);

	if (!is_tsf) {
		_GetContext(weasel_context, session_id);
	}

	if (!m_ui) return;

	if (RimeGetOption(session_id, "inline_preedit"))
		m_session_status_map[session_id].style.client_caps |= weasel::INLINE_PREEDIT_CAPABLE;
	else
		m_session_status_map[session_id].style.client_caps &= ~weasel::INLINE_PREEDIT_CAPABLE;

	if (weasel_status.composing)
	{
		 m_ui->Update(weasel_context, weasel_status);
		 //if (!is_tsf)
		 //{
			// m_ui->Show();
			// // LOG(INFO) << std::format("From RimeWithWeaselHandler::_UpdateUI. is_tsf = {:s}", is_tsf);
		 //}
	}
	else if (!_ShowMessage(weasel_context, weasel_status))
	{
		m_ui->Hide();
		m_ui->Update(weasel_context, weasel_status);
	}

	_RefreshTrayIcon(session_id, _UpdateUICallback);

	m_message_type.clear();
	m_message_value.clear();
	m_message_label.clear();
	m_option_name.clear();
}

void RimeWithWeaselHandler::_LoadIconSettingFromSchema(RimeConfig& config, char* buffer, const int BUF_SIZE,
	const char* key1, const char* key2, std::wstring_view user_dir, std::wstring_view shared_dir, std::wstring& value)
{
	memset(buffer, '\0', (BUF_SIZE + 1));

	if (RimeConfigGetString(&config, key1, buffer, BUF_SIZE) || (key2 != nullptr && RimeConfigGetString(&config, key2, buffer, BUF_SIZE)))
	{
		auto tmp = to_wstring(buffer, CP_UTF8);
		fs::path user_file{ std::format(LR"({}\{})", user_dir.data(), tmp) };
		if (fs::exists(user_file) && !fs::is_directory(user_file))
		{
			value = user_file.wstring();
		}
		else
		{
			fs::path shared_file{ std::format(LR"({}\{})", shared_dir.data(), tmp) };
			if (fs::exists(shared_file) && !fs::is_directory(shared_file))
			{
				value = shared_file.wstring();
			}
			else
			{
				value = L"";
			}
		}
	}
	else
	{
		value = L"";
	}
}

void RimeWithWeaselHandler::_LoadSchemaSpecificSettings(RimeSessionId session_id, const std::string& schema_id)
{
	if (!m_ui) return;
	const int BUF_SIZE = 255;
	char buffer[BUF_SIZE + 1];
	RimeConfig config;
	if (!RimeSchemaOpen(schema_id.c_str(), &config))
		return;
	m_ui->style() = m_base_style;
	_UpdateUIStyle(&config, m_ui, false);
	SessionStatus& session_status = m_session_status_map[session_id];
	session_status.style = m_ui->style();

	// load schema color style config
	memset(buffer, '\0', sizeof(buffer));
	if (!m_current_dark_mode && RimeConfigGetString(&config, "style/color_scheme", buffer, BUF_SIZE))
	{
		std::string color_name{ buffer };
		RimeConfigIterator preset{ 0 };
		if (RimeConfigBeginMap(&preset, &config, std::format("preset_color_schemes/{}", color_name).data()))
		{
			_UpdateUIStyleColor(&config, m_ui->style(), color_name);
		}
		else
		{
			RimeConfig weaselconfig;
			if (RimeConfigOpen("weasel", &weaselconfig))
			{
				_UpdateUIStyleColor(&weaselconfig, m_ui->style(), std::string(buffer));
				RimeConfigClose(&weaselconfig);
			}
		}
	}
	else if (m_current_dark_mode && RimeConfigGetString(&config, "style/color_scheme_dark", buffer, BUF_SIZE))
	{
		std::string color_name{ buffer };
		RimeConfigIterator preset{};
		if (RimeConfigBeginMap(&preset, &config, std::format("preset_color_schemes/{}", color_name).data()))
		{
			_UpdateUIStyleColor(&config, m_ui->style(), color_name);
		}
		else
		{
			RimeConfig weaselconfig;
			if (RimeConfigOpen("weasel", &weaselconfig))
			{
				_UpdateUIStyleColor(&weaselconfig, m_ui->style(), std::string(buffer));
				RimeConfigClose(&weaselconfig);
			}
		}
	}
	// load schema icon start
	{
		std::wstring user_dir = to_wstring(weasel_user_data_dir());
		std::wstring shared_dir = to_wstring(weasel_shared_data_dir());

		_LoadIconSettingFromSchema(config, buffer, BUF_SIZE, "schema/icon", "schema/zhung_icon", user_dir, shared_dir, session_status.style.current_zhung_icon);
		_LoadIconSettingFromSchema(config, buffer, BUF_SIZE, "schema/ascii_icon", NULL, user_dir, shared_dir, session_status.style.current_ascii_icon);
		_LoadIconSettingFromSchema(config, buffer, BUF_SIZE, "schema/full_icon", NULL, user_dir, shared_dir, session_status.style.current_full_icon);
		_LoadIconSettingFromSchema(config, buffer, BUF_SIZE, "schema/half_icon", NULL, user_dir, shared_dir, session_status.style.current_half_icon);
	}
	// load schema icon end
	RimeConfigClose(&config);
}

void RimeWithWeaselHandler::_LoadAppInlinePreeditSet(RimeSessionId session_id, bool ignore_app_name)
{
	static char _app_name[50];
	RimeGetProperty(session_id, "client_app", _app_name, sizeof(_app_name) - 1);
	std::string app_name(_app_name);
	if (!ignore_app_name && m_last_app_name == app_name)
		return;
	m_last_app_name = app_name;
	SessionStatus& session_status = m_session_status_map[session_id];
	bool inline_preedit = session_status.style.inline_preedit;
	if (!app_name.empty())
	{
		// auto it = m_app_options.find(app_name);
		if (auto it = m_app_options.find(app_name); it != m_app_options.end())
		{
			AppOptions& options(m_app_options[it->first]);
			auto pfind = std::make_shared<bool>(false);
			std::for_each(options.begin(), options.end(), [session_id, pfind, inline_preedit, this](std::pair<const std::string, bool>& pair)
				{
					if (pair.first == "inline_preedit")
					{
						*pfind = true;
						RimeSetOption(session_id, pair.first.c_str(), Bool(pair.second));
						m_session_status_map[session_id].style.inline_preedit = Bool(pair.second);
						if (m_session_status_map[session_id].style.inline_preedit != inline_preedit)
						{
							_UpdateInlinePreeditStatus(session_id);
						}
					}
				});
			if (!(*pfind))
			{
				goto load_schema_inline;
			}
		}
		else
		{
		load_schema_inline:
			RIME_STRUCT(RimeStatus, status);
			if (RimeGetStatus(session_id, &status))
			{
				std::string schema_id = status.schema_id;
				RimeConfig config;
				if (!RimeSchemaOpen(schema_id.c_str(), &config)) return;
				Bool inline_preedit = session_status.style.inline_preedit;
				if (RimeConfigGetBool(&config, "style/inline_preedit", &inline_preedit))
					session_status.style.inline_preedit = !!inline_preedit;
				RimeConfigClose(&config);
				RimeFreeStatus(&status);
				if (m_ui->style().inline_preedit != (!!inline_preedit))
				{
					_UpdateInlinePreeditStatus(session_id);
				}
			}
		}
	}
}

bool RimeWithWeaselHandler::_ShowMessage(weasel::Context& ctx, weasel::Status& status)
{
	// show as auxiliary string
	std::wstring& tips(ctx.aux.str);
	bool show_icon = false;
	if (m_message_type == "deploy") {
		if (m_message_value == "start")
			tips = L"正在部署 RIME";
		else if (m_message_value == "success")
			tips = L"部署完成";
		else if (m_message_value == "failure")
			tips = L"有错误，请查看日志 %TEMP%\\rime.weasel.*.INFO";
	}
	else if (m_message_type == "schema") 
	{
		tips = /*L"【" + */status.schema_name/* + L"】"*/;
	}
	else if (m_message_type == "option") 
	{
		status.type = weasel::SCHEMA;
		if (m_message_value == "!ascii_mode")
		{
			show_icon = true;
		}
		else if (m_message_value == "ascii_mode")
		{
			show_icon = true;
		}
		else
		{
			tips = to_wstring(m_message_label, CP_UTF8);
		}

		if (m_message_value == "full_shape" || m_message_value == "!full_shape")
		{
			status.type = weasel::FULL_SHAPE;
		}		
	}
	if (tips.empty() && !show_icon)
		return m_ui->IsCountingDown();

	auto foption = m_show_notifications.find(m_option_name);
	auto falways = m_show_notifications.find("always");
	if ((!m_add_session && (foption != m_show_notifications.end() ||
		falways != m_show_notifications.end())) ||
		m_message_type == "deploy")
	{
		m_ui->Update(ctx, status);
		if (m_show_notifications_time)
			m_ui->ShowWithTimeout(m_show_notifications_time);
		return true;
	}
	else
	{
		return m_ui->IsCountingDown();
	}
}
inline std::string _GetLabelText(const std::vector<weasel::Text>& labels, int id, const wchar_t* format)
{
	wchar_t buffer[128];
	swprintf_s<128>(buffer, format, labels.at(id).str.c_str());
	return to_string(buffer, CP_UTF8);
}

bool RimeWithWeaselHandler::_Respond(RimeSessionId session_id, EatLine eat)
{
	std::set<std::string> actions;
	std::list<std::string> messages;

	SessionStatus& session_status = m_session_status_map[session_id];
	RIME_STRUCT(RimeCommit, commit);
	if (RimeGetCommit(session_id, &commit))
	{
		actions.insert("commit");
		messages.emplace_back(std::format("commit={}\n", commit.text));
		RimeFreeCommit(&commit);
	}

	bool is_composing = false;
	RIME_STRUCT(RimeStatus, status);
	if (RimeGetStatus(session_id, &status))
	{
		is_composing = !!status.is_composing;
		actions.insert("status");
		messages.emplace_back(std::format("status.ascii_mode={}\n", status.is_ascii_mode));
		messages.emplace_back(std::format("status.composing={}\n", status.is_composing));
		messages.emplace_back(std::format("status.disabled={}\n", status.is_disabled));
		messages.emplace_back(std::format("status.full_shape={}\n", status.is_full_shape));
		messages.emplace_back(std::format("status.ascii_punct={}\n", status.is_ascii_punct));		
		messages.emplace_back(std::format("status.prediction={}\n", status.is_predictable));
		messages.emplace_back(std::format("status.schema_id={}\n", status.schema_id));
		if (m_global_ascii_mode && (session_status.status.is_ascii_mode != status.is_ascii_mode))
		{
			for (auto& pair : m_session_status_map)
			{
				if (pair.first != session_id)
					RimeSetOption(pair.first, "ascii_mode", !!status.is_ascii_mode);
			}
		}
		session_status.status = status;
		RimeFreeStatus(&status);
	}

	RIME_STRUCT(RimeContext, ctx);
	if (RimeGetContext(session_id, &ctx))
	{
		if (is_composing)
		{
			actions.insert("ctx");
			switch (m_ui->style().preedit_type)
			{
			case weasel::UIStyle::PREVIEW:
				if (ctx.commit_text_preview != NULL)
				{
					std::string first = ctx.commit_text_preview;
					messages.emplace_back(std::format("ctx.preedit={}\n", first));
					messages.emplace_back(std::format("ctx.preedit.cursor={},{},{}\n",
						utf8towcslen(first.data(), 0),
						utf8towcslen(first.data(), first.size()),
						utf8towcslen(first.data(), first.size())));
					break;
				}
				// no preview, fall back to composition
			case weasel::UIStyle::COMPOSITION:
				messages.emplace_back(std::format("ctx.preedit={}\n", ctx.composition.preedit));
				if (ctx.composition.sel_start <= ctx.composition.sel_end)
				{
					messages.emplace_back(std::format("ctx.preedit.cursor={},{},{}\n",
						utf8towcslen(ctx.composition.preedit, ctx.composition.sel_start),
						utf8towcslen(ctx.composition.preedit, ctx.composition.sel_end),
						utf8towcslen(ctx.composition.preedit, ctx.composition.cursor_pos)));
				}
				break;
			case weasel::UIStyle::PREVIEW_ALL:
				weasel::CandidateInfo cinfo;
				_GetCandidateInfo(cinfo, ctx);
				std::string topush = std::format("ctx.preedit={} [", ctx.composition.preedit);
				for (auto i = 0; i < ctx.menu.num_candidates; i++)
				{
					std::string label = session_status.style.label_font_point > 0 ? _GetLabelText(cinfo.labels, i, session_status.style.label_text_format.c_str()) : "";
					std::string comment = session_status.style.comment_font_point > 0 ? to_string(cinfo.comments.at(i).str, CP_UTF8) : "";
					std::string mark_text = session_status.style.mark_text.empty() ? "*" : to_string(session_status.style.mark_text, CP_UTF8);
					std::string prefix = (i != ctx.menu.highlighted_candidate_index) ? "" : mark_text;
					topush += std::format(" {}{}{} {}", prefix, label, ctx.menu.candidates[i].text, comment);
				}
				messages.emplace_back(std::format("{} ]\n", topush));
				//messages.emplace_back(std::string("ctx.preedit=") + ctx.composition.preedit + '\n');
				if (ctx.composition.sel_start <= ctx.composition.sel_end)
				{
					messages.emplace_back(std::format("ctx.preedit.cursor={},{},{}\n",
						utf8towcslen(ctx.composition.preedit, ctx.composition.sel_start),
						utf8towcslen(ctx.composition.preedit, ctx.composition.sel_end),
						utf8towcslen(ctx.composition.preedit, ctx.composition.cursor_pos)));
				}
				break;
			}
		}
		if (ctx.menu.num_candidates)
		{
			weasel::CandidateInfo cinfo;
			std::wstringstream ss;
			boost::archive::text_woarchive oa(ss);
			_GetCandidateInfo(cinfo, ctx);

			oa << cinfo;

			messages.emplace_back(std::format("ctx.cand={}\n", to_string(ss.str(), CP_UTF8)));
		}
		RimeFreeContext(&ctx);
	}

	// configuration information
	actions.insert("config");
	messages.emplace_back(std::format("config.inline_preedit={}\n", (int)session_status.style.inline_preedit));

	// style
	if (!session_status.__synced) 
	{
		std::wstringstream ss;
		boost::archive::text_woarchive oa(ss);
		oa << m_ui->style();

		actions.insert("style");
		messages.emplace_back(std::format("style={}\n", to_string(ss.str(), CP_UTF8)));
		session_status.__synced = true;
	}

	// summarize

	if (actions.empty())
	{
		messages.insert(messages.begin(), std::string("action=noop\n"));
	}
	else
	{
		std::string actionList(join(actions, ","));
		messages.insert(messages.begin(), std::format("action={}\n", actionList));
	}

	messages.emplace_back(std::string(".\n"));

	return std::all_of(messages.begin(), messages.end(), [&eat](std::string_view msg)
		{
			return eat(to_wstring(msg.data(), CP_UTF8));
		});
}

static inline COLORREF blend_colors(COLORREF fcolor, COLORREF bcolor)
{
	return RGB(
		(GetRValue(fcolor) * 2 + GetRValue(bcolor)) / 3,
		(GetGValue(fcolor) * 2 + GetGValue(bcolor)) / 3,
		(GetBValue(fcolor) * 2 + GetBValue(bcolor)) / 3
	) | ((((fcolor >> 24) + (bcolor >> 24) / 2) << 24));
}

// convertions from color format to COLOR_ABGR
static inline int ConvertColorToAbgr(int color, ColorFormat fmt = COLOR_ABGR)
{
	if (fmt == COLOR_ABGR) return color;
	else if (fmt == COLOR_ARGB) return ARGB2ABGR(color);
	else return RGBA2ABGR(color);
}

static Bool _RimeConfigGetColor32bWithFallback(RimeConfig* config, std::string_view key, int& value, ColorFormat fmt, const int& fallback)
{
	char color[256] = { 0 };
	if (!RimeConfigGetString(config, key.data(), color, 256))
	{
		value = fallback;
		return False;
	}
	std::string color_str = std::string(color);
	// color code hex 
	if (std::regex_match(color_str, HEX_REGEX))
	{
		std::string tmp = std::regex_replace(color_str, TRIMHEAD_REGEX, "");
		// limit first 8 code
		tmp = tmp.substr(0, 8);
		if (tmp.length() == 6) // color code without alpha, xxyyzz add alpha ff
		{
			value = std::stoi(tmp, 0, 16);
			if (fmt != COLOR_RGBA) value |= 0xff000000;
			else value = (value << 8) | 0x000000ff;
		}
		else if (tmp.length() == 3) // color hex code xyz => xxyyzz and alpha ff
		{
			tmp = tmp.substr(0, 1) + tmp.substr(0, 1)
				+ tmp.substr(1, 1) + tmp.substr(1, 1)
				+ tmp.substr(2, 1) + tmp.substr(2, 1);

			value = std::stoi(tmp, 0, 16);
			if (fmt != COLOR_RGBA) value |= 0xff000000;
			else value = (value << 8) | 0x000000ff;
		}
		else if (tmp.length() == 4)	// color hex code vxyz => vvxxyyzz
		{
			tmp = tmp.substr(0, 1) + tmp.substr(0, 1)
				+ tmp.substr(1, 1) + tmp.substr(1, 1)
				+ tmp.substr(2, 1) + tmp.substr(2, 1)
				+ tmp.substr(3, 1) + tmp.substr(3, 1);

			std::string tmp1 = tmp.substr(0, 6);
			int value1 = std::stoi(tmp1, 0, 16);
			tmp1 = tmp.substr(6);
			int value2 = std::stoi(tmp1, 0, 16);
			value = (value1 << (tmp1.length() * 4)) | value2;
		}
		else if (tmp.length() > 6 && tmp.length() <= 8)	/* color code with alpha */
		{
			// stoi limitation, split to handle
			std::string tmp1 = tmp.substr(0, 6);
			int value1 = std::stoi(tmp1, 0, 16);
			tmp1 = tmp.substr(6);
			int value2 = std::stoi(tmp1, 0, 16);
			value = (value1 << (tmp1.length() * 4)) | value2;
		}
		else	// reject other code, length less then 3 or length == 5
		{
			value = fallback;
			return False;
		}
		value = ConvertColorToAbgr(value, fmt);
		value = (value & 0xffff'ffff);
		return True;
	}
	// regular number or other stuff, if user use pure dec number, they should take care themselves
	else
	{
		int tmp = 0;
		if (!RimeConfigGetInt(config, key.data(), &tmp))
		{
			value = fallback;
			return False;
		}

		if (fmt != COLOR_RGBA)
			value = (tmp | 0xff00'0000) & 0xffff'ffff;
		else
			value = ((tmp << 8) | 0x0000'00ff) & 0xffff'ffff;
		value = ConvertColorToAbgr(value, fmt);
		return True;
	}
}

// for remove useless spaces around sperators, begining and ending
static inline void _RemoveSpaceAroundSep(std::wstring& str)
{
	str = std::regex_replace(str, std::wregex(L"\\s*(,|:^|$)\\s*"), L"$1");
}

// parse bool type configuration to T type value trueValue / falseValue
template<typename T>
static void _RimeGetBool(RimeConfig* config, const char* key, bool cond, T& value, const T& trueValue, const T& falseValue)
{
	Bool tempb = false;
	if (RimeConfigGetBool(config, key, &tempb) || cond)
	{
		value = (!!tempb) ? trueValue : falseValue;
	}
}

// parse string option to T type value, with fallback
template <typename T>
void _RimeParseStringOpt(RimeConfig* config, const std::string& key, T& value, const std::map<std::string, T>& amap)
{
	char str_buff[256]{};
	if (RimeConfigGetString(config, key.data(), str_buff, sizeof(str_buff) - 1))
	{
		auto it = amap.find(str_buff);
		if (it != amap.end())
			value = it->second;
	}
}

// turn value to be non-negative
static inline void _abs(int* value)
{
	*value = abs(*value);
}

// get int type value with fallback key fb_key, and func to execute after reading
static void _RimeGetIntWithFallback(RimeConfig* config, const char* key, int* value, const char* fb_key = nullptr, std::function<void(int*)> func = nullptr)
{
	if (!RimeConfigGetInt(config, key, value) && fb_key != nullptr)
	{
		RimeConfigGetInt(config, fb_key, value);
	}
	if (func)
	{
		func(value);
	}
}

// get string value, with fallback value *fallback, and func to execute after reading
static void _RimeGetStringWithFunc(RimeConfig* config, const char* key, std::wstring& value,
	const std::wstring* fallback = nullptr, const std::function<void(std::wstring&)> func = nullptr)
{
	const int BUF_SIZE{ 2047 };
	char buffer[BUF_SIZE + 1]{};
	if (RimeConfigGetString(config, key, buffer, BUF_SIZE))
	{
		std::wstring temp{ to_wstring(buffer, CP_UTF8) };
		if (func)
		{
			func(temp);
		}
		value = temp;
	}
	else if (fallback)
	{
		value = *fallback;
	}
}

void RimeWithWeaselHandler::_UpdateShowNotifications(RimeConfig* config, bool initialize)
{
	Bool show_notifications = true;
	RimeConfigIterator iter;
	if (initialize)
		m_show_notifications_base.clear();
	m_show_notifications.clear();

	if (RimeConfigGetBool(config, "show_notifications", &show_notifications)) 
	{
		// config read as bool, for gloal all on or off
		if (show_notifications)
			m_show_notifications["always"] = true;
		if (initialize)
			m_show_notifications_base = m_show_notifications;
	}
	else if (RimeConfigBeginList(&iter, config, "show_notifications")) 
	{
		// config read as list, list item should be option name in schema
		// or key word 'schema' for schema switching tip
		while (RimeConfigNext(&iter)) 
		{
			char buffer[256] = { 0 };
			if (RimeConfigGetString(config, iter.path, buffer, 256))
				m_show_notifications[std::string(buffer)] = true;
		}
		if (initialize)
			m_show_notifications_base = m_show_notifications;
	}
	else 
	{
		// not configured, or incorrect type
		if (initialize)
			m_show_notifications_base["always"] = true;
		m_show_notifications = m_show_notifications_base;
	}
}

// update ui's style parameters, ui has been check before referenced
static void _UpdateUIStyle(RimeConfig* config, weasel::UI* ui, bool initialize)
{
	weasel::UIStyle& style(ui->style());

	const int BUF_SIZE = 255;
	char buffer[BUF_SIZE + 1]{};
	// get font faces
	_RimeGetStringWithFunc(config, "style/font_face", style.font_face, nullptr, _RemoveSpaceAroundSep);
	std::wstring* const pFallbackFontFace = initialize ? &style.font_face : nullptr;
	_RimeGetStringWithFunc(config, "style/label_font_face", style.label_font_face, pFallbackFontFace, _RemoveSpaceAroundSep);
	_RimeGetStringWithFunc(config, "style/comment_font_face", style.comment_font_face, pFallbackFontFace, _RemoveSpaceAroundSep);
	// able to set label font/comment font empty, force fallback to font face.
	if (style.label_font_face.empty())
	{
		style.label_font_face = style.font_face;
	}
	if (style.comment_font_face.empty())
	{
		style.comment_font_face = style.font_face;
	}
	// get font points
	_RimeGetIntWithFallback(config, "style/font_point", &style.font_point);
	if (style.font_point < 1)
		style.font_point = 12;
	_RimeGetIntWithFallback(config, "style/label_font_point", &style.label_font_point, "style/font_point", _abs);
	_RimeGetIntWithFallback(config, "style/comment_font_point", &style.comment_font_point, "style/font_point", _abs);
	_RimeGetIntWithFallback(config, "style/mouse_hover_ms", &style.mouse_hover_ms, nullptr, _abs);
	_RimeGetBool(config, "style/inline_preedit", initialize, style.inline_preedit, true, false);
	_RimeGetBool(config, "style/vertical_auto_reverse", initialize, style.vertical_auto_reverse, true, false);
	const std::map<std::string, UIStyle::PreeditType> _preeditMap
	{
		{ "composition",	UIStyle::COMPOSITION },
		{ "preview",		UIStyle::PREVIEW },
		{ "preview_all",	UIStyle::PREVIEW_ALL }
	};

	_RimeParseStringOpt(config, "style/preedit_type", style.preedit_type, _preeditMap);
	const std::map<std::string, UIStyle::AntiAliasMode> _aliasModeMap
	{
		{ "force_dword",	UIStyle::FORCE_DWORD },
		{ "cleartype",		UIStyle::CLEARTYPE },
		{ "grayscale",		UIStyle::GRAYSCALE },
		{ "aliased",		UIStyle::ALIASED },
		{ "default",		UIStyle::DEFAULT }
	};

	_RimeParseStringOpt(config, "style/antialias_mode", style.antialias_mode, _aliasModeMap);
	const std::map<std::string, UIStyle::LayoutAlignType> _alignType
	{
		{ "top",			UIStyle::ALIGN_TOP },
		{ "center",			UIStyle::ALIGN_CENTER },
		{ "bottom",			UIStyle::ALIGN_BOTTOM }
	};
	_RimeParseStringOpt(config, "style/layout/align_type", style.align_type, _alignType);
	_RimeGetBool(config, "style/display_tray_icon", initialize, style.display_tray_icon, true, false);
	_RimeGetBool(config, "style/ascii_tip_follow_cursor", initialize, style.ascii_tip_follow_cursor, true, false);
	_RimeGetBool(config, "style/horizontal", initialize, style.layout_type, UIStyle::LAYOUT_HORIZONTAL, UIStyle::LAYOUT_VERTICAL);
	_RimeGetBool(config, "style/paging_on_scroll", initialize, style.paging_on_scroll, true, false);
	_RimeGetBool(config, "style/click_to_capture", initialize, style.click_to_capture, true, false);
	_RimeGetBool(config, "style/fullscreen", false, style.layout_type,
		((style.layout_type == UIStyle::LAYOUT_HORIZONTAL) ? UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN : UIStyle::LAYOUT_VERTICAL_FULLSCREEN), style.layout_type);
	_RimeGetBool(config, "style/vertical_text", false, style.layout_type, UIStyle::LAYOUT_VERTICAL_TEXT, style.layout_type);
	_RimeGetBool(config, "style/vertical_text_left_to_right", false, style.vertical_text_left_to_right, true, false);
	_RimeGetBool(config, "style/vertical_text_with_wrap", false, style.vertical_text_with_wrap, true, false);

	const std::map<std::string, bool> _text_orientation
	{
		{ std::string{ "horizontal" }, false },
		{ std::string{ "vertical" }, true}
	};
	bool _text_orientation_bool{ false };
	_RimeParseStringOpt(config, "style/text_orientation", _text_orientation_bool, _text_orientation);
	if (_text_orientation_bool)
	{
		style.layout_type = UIStyle::LAYOUT_VERTICAL_TEXT;
	}

	_RimeGetStringWithFunc(config, "style/label_format", style.label_text_format);
	_RimeGetStringWithFunc(config, "style/mark_text", style.mark_text);
	_RimeGetIntWithFallback(config, "style/layout/min_width", &style.min_width, NULL, _abs);
	_RimeGetIntWithFallback(config, "style/layout/max_width", &style.max_width, NULL, _abs);
	_RimeGetIntWithFallback(config, "style/layout/min_height", &style.min_height, NULL, _abs);
	_RimeGetIntWithFallback(config, "style/layout/max_height", &style.max_height, NULL, _abs);
	// layout (alternative to style/horizontal)
	const std::map < std::string, UIStyle::LayoutType > _layoutMap
	{
		{ "vertical",					UIStyle::LAYOUT_VERTICAL},
		{ "horizontal",					UIStyle::LAYOUT_HORIZONTAL},
		{ "vertical_text",				UIStyle::LAYOUT_VERTICAL_TEXT},
		{ "vertical+fullscreen",		UIStyle::LAYOUT_VERTICAL_FULLSCREEN},
		{ "horizontal+fullscreen",		UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN}
	};
	_RimeParseStringOpt(config, "style/layout/type", style.layout_type, _layoutMap);
	// disable max_width when full screen
	if (style.layout_type == UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN || style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN)
	{
		style.max_width = 0;
		style.inline_preedit = false;
	}
	_RimeGetIntWithFallback(config, "style/layout/border", &style.border, "style/layout/border_width", _abs);
	_RimeGetIntWithFallback(config, "style/layout/margin_x", &style.margin_x);
	_RimeGetIntWithFallback(config, "style/layout/margin_y", &style.margin_y);
	_RimeGetIntWithFallback(config, "style/layout/spacing", &style.spacing, NULL, _abs);
	_RimeGetIntWithFallback(config, "style/layout/candidate_spacing", &style.candidate_spacing, NULL, _abs);
	_RimeGetIntWithFallback(config, "style/layout/hilite_spacing", &style.hilite_spacing, NULL, _abs);
	_RimeGetIntWithFallback(config, "style/layout/hilite_padding_x", &style.hilite_padding_x, "style/layout/hilite_padding", _abs);
	_RimeGetIntWithFallback(config, "style/layout/hilite_padding_y", &style.hilite_padding_y, "style/layout/hilite_padding", _abs);
	_RimeGetIntWithFallback(config, "style/layout/shadow_radius", &style.shadow_radius, NULL, _abs);
	// disable shadow for fullscreen layout
	style.shadow_radius *= (!(style.layout_type == UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN || style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN));
	_RimeGetIntWithFallback(config, "style/layout/shadow_offset_x", &style.shadow_offset_x);
	_RimeGetIntWithFallback(config, "style/layout/shadow_offset_y", &style.shadow_offset_y);
	// round_corner as alias of hilited_corner_radius
	_RimeGetIntWithFallback(config, "style/layout/hilited_corner_radius", &style.round_corner, "style/layout/round_corner", _abs);
	// corner_radius not set, fallback to round_corner
	_RimeGetIntWithFallback(config, "style/layout/corner_radius", &style.round_corner_ex, "style/layout/round_corner", _abs);
	// fix padding and spacing settings
	if (style.layout_type != UIStyle::LAYOUT_VERTICAL_TEXT)
	{
		// hilite_padding vs spacing
		style.spacing = max(style.spacing, style.hilite_padding_y * 2);	// if hilite_padding over spacing, increase spacing
		// hilite_padding vs candidate_spacing
		if (style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN || style.layout_type == UIStyle::LAYOUT_VERTICAL) {
			style.candidate_spacing = max(style.candidate_spacing, style.hilite_padding_y * 2);	// vertical, if hilite_padding_y over candidate spacing, increase candidate spacing
		}
		else {
			style.candidate_spacing = max(style.candidate_spacing, style.hilite_padding_x * 2);	// horizontal, if hilite_padding_x over candidate spacing, increase candidate spacing
		}
		// hilite_padding_x vs hilite_spacing
		style.hilite_spacing = max(style.hilite_spacing, style.hilite_padding_x);
	}
	else	// LAYOUT_VERTICAL_TEXT
	{
		// hilite_padding_x vs spacing
		style.spacing = max(style.spacing, style.hilite_padding_x * 2); // if hilite_padding over spacing, increase spacing
		// hilite_padding vs candidate_spacing
		style.candidate_spacing = max(style.candidate_spacing, style.hilite_padding_x * 2); // if hilite_padding_x over candidate spacing, increase candidate spacing
		// vertical_text_with_wrap and hilite_padding_y over candidate_spacing
		if (style.vertical_text_with_wrap)
			style.candidate_spacing = max(style.candidate_spacing, style.hilite_padding_y * 2);
		// hilite_padding_y vs hilite_spacing
		style.hilite_spacing = max(style.hilite_spacing, style.hilite_padding_y);
	}
	// fix padding and margin settings
	int scale = style.margin_x < 0 ? -1 : 1;
	style.margin_x = scale * max(style.hilite_padding_x, abs(style.margin_x));
	scale = style.margin_y < 0 ? -1 : 1;
	style.margin_y = scale * max(style.hilite_padding_y, abs(style.margin_y));
	// get enhanced_position
	_RimeGetBool(config, "style/enhanced_position", initialize, style.enhanced_position, true, false);
	// get color scheme
	if (initialize && RimeConfigGetString(config, "style/color_scheme", buffer, BUF_SIZE))
	{
		_UpdateUIStyleColor(config, style);
	}
}

// load color configs to style, by "style/color_scheme" or specific scheme name "color" which is default empty
static bool _UpdateUIStyleColor(RimeConfig* config, UIStyle& style, std::string color)
{
	const int BUF_SIZE = 255;
	char buffer[BUF_SIZE + 1];
	memset(buffer, '\0', sizeof(buffer));
	std::string color_mark = "style/color_scheme";
	// color scheme
	if (RimeConfigGetString(config, color_mark.c_str(), buffer, BUF_SIZE) || !color.empty())
	{
		std::string prefix("preset_color_schemes/");
		prefix += (color.empty()) ? buffer : color;
		// define color format, default abgr if not set
		ColorFormat fmt = COLOR_ABGR;
		const std::map<std::string, ColorFormat> _colorFmt = {
			{std::string("argb"), COLOR_ARGB},
			{std::string("rgba"), COLOR_RGBA},
			{std::string("abgr"), COLOR_ABGR}
		};
		_RimeParseStringOpt(config, (prefix + "/color_format"), fmt, _colorFmt);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/back_color"), style.back_color, fmt, 0xffffffff);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/shadow_color"), style.shadow_color, fmt, TRANSPARENT_COLOR);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/prevpage_color"), style.prevpage_color, fmt, TRANSPARENT_COLOR);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/nextpage_color"), style.nextpage_color, fmt, TRANSPARENT_COLOR);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/text_color"), style.text_color, fmt, 0xff000000);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/candidate_text_color"), style.candidate_text_color, fmt, style.text_color);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/candidate_back_color"), style.candidate_back_color, fmt, TRANSPARENT_COLOR);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/border_color"), style.border_color, fmt, style.text_color);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/hilited_text_color"), style.hilited_text_color, fmt, style.text_color);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/hilited_back_color"), style.hilited_back_color, fmt, style.back_color);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/hilited_candidate_text_color"), style.hilited_candidate_text_color, fmt, style.hilited_text_color);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/hilited_candidate_back_color"), style.hilited_candidate_back_color, fmt, style.hilited_back_color);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/hilited_candidate_shadow_color"), style.hilited_candidate_shadow_color, fmt, TRANSPARENT_COLOR);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/hilited_shadow_color"), style.hilited_shadow_color, fmt, TRANSPARENT_COLOR);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/candidate_shadow_color"), style.candidate_shadow_color, fmt, TRANSPARENT_COLOR);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/candidate_border_color"), style.candidate_border_color, fmt, TRANSPARENT_COLOR);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/hilited_candidate_border_color"), style.hilited_candidate_border_color, fmt, TRANSPARENT_COLOR);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/label_color"), style.label_text_color, fmt, blend_colors(style.candidate_text_color, style.candidate_back_color));
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/hilited_label_color"), style.hilited_label_text_color, fmt, blend_colors(style.hilited_candidate_text_color, style.hilited_candidate_back_color));
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/comment_text_color"), style.comment_text_color, fmt, style.label_text_color);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/hilited_comment_text_color"), style.hilited_comment_text_color, fmt, style.hilited_label_text_color);
		_RimeConfigGetColor32bWithFallback(config, (prefix + "/hilited_mark_color"), style.hilited_mark_color, fmt, TRANSPARENT_COLOR);
		return true;
	}
	return false;
}

static void _LoadAppOptions(RimeConfig* config, AppOptionsByAppName& app_options)
{
	app_options.clear();
	RimeConfigIterator app_iter;
	RimeConfigIterator option_iter;
	RimeConfigBeginMap(&app_iter, config, "app_options");
	while (RimeConfigNext(&app_iter)) {
		AppOptions& options(app_options[app_iter.key]);
		RimeConfigBeginMap(&option_iter, config, app_iter.path);
		while (RimeConfigNext(&option_iter)) {
			Bool value = False;
			if (RimeConfigGetBool(config, option_iter.path, &value)) {
				options[option_iter.key] = !!value;
			}
		}
		RimeConfigEnd(&option_iter);
	}
	RimeConfigEnd(&app_iter);
}

void RimeWithWeaselHandler::_GetStatus(weasel::Status& stat, RimeSessionId session_id, weasel::Context& ctx)
{
	RIME_STRUCT(RimeStatus, status);
	if (RimeGetStatus(session_id, &status))
	{
		std::string schema_id = "";
		if (status.schema_id)
			schema_id = status.schema_id;
		stat.schema_name = to_wstring(status.schema_name, CP_UTF8);
		stat.schema_id = to_wstring(status.schema_id, CP_UTF8);
		stat.ascii_mode = !!status.is_ascii_mode;
		stat.composing = !!status.is_composing;
		stat.disabled = !!status.is_disabled;
		stat.full_shape = !!status.is_full_shape;
		stat.ascii_punct = !!status.is_ascii_punct;
		stat.prediction = !!status.is_predictable;
		if (schema_id != m_last_schema_id)
		{
			m_last_schema_id = schema_id;
			if (schema_id != ".default") {							// don't load for schema select menu
				bool inline_preedit = m_session_status_map[session_id].style.inline_preedit;
				_LoadSchemaSpecificSettings(session_id, schema_id);
				_LoadAppInlinePreeditSet(session_id, true);
				if (m_session_status_map[session_id].style.inline_preedit != inline_preedit)
					_UpdateInlinePreeditStatus(session_id);			// in case of inline_preedit set in schema
				_RefreshTrayIcon(session_id, _UpdateUICallback);	// refresh icon after schema changed
				if (m_show_notifications.find("schema") != m_show_notifications.end() && m_show_notifications_time > 0)
				{
					ctx.aux.str = stat.schema_name;
					m_ui->Update(ctx, stat);
					m_ui->ShowWithTimeout(m_show_notifications_time);
				}
			}
		}
		RimeFreeStatus(&status);
	}

}

void RimeWithWeaselHandler::_GetContext(weasel::Context& weasel_context, RimeSessionId session_id)
{
	RIME_STRUCT(RimeContext, ctx);
	if (RimeGetContext(session_id, &ctx))
	{
		if (ctx.composition.length > 0)
		{
			weasel_context.preedit.str = to_wstring(ctx.composition.preedit, CP_UTF8);
			if (ctx.composition.sel_start < ctx.composition.sel_end)
			{
				weasel::TextAttribute attr;
				attr.type = weasel::HIGHLIGHTED;
				attr.range.start = utf8towcslen(ctx.composition.preedit, ctx.composition.sel_start);
				attr.range.end = utf8towcslen(ctx.composition.preedit, ctx.composition.sel_end);

				weasel_context.preedit.attributes.emplace_back(attr);
			}
		}
		if (ctx.menu.num_candidates)
		{
			weasel::CandidateInfo& cinfo(weasel_context.cinfo);
			_GetCandidateInfo(cinfo, ctx);
		}
		RimeFreeContext(&ctx);
	}
}

bool RimeWithWeaselHandler::_IsSessionTSF(RimeSessionId session_id)
{
	std::string client_type(20, 0);
	RimeGetProperty(session_id, "client_type", client_type.data(), client_type.size());
	client_type = client_type.data();
	return client_type == "tsf";
}

void RimeWithWeaselHandler::_UpdateInlinePreeditStatus(RimeSessionId session_id)
{
	if (!m_ui)	return;
	// set inline_preedit option
	bool inline_preedit = m_session_status_map[session_id].style.inline_preedit && _IsSessionTSF(session_id);
	RimeSetOption(session_id, "inline_preedit", Bool(inline_preedit));
	// show soft cursor on weasel panel but not inline
	RimeSetOption(session_id, "soft_cursor", Bool(!inline_preedit));
}

