﻿// TestWeaselIPC.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
import WeaselIPC;
import RimeWithWeasel;

#include <boost/interprocess/streams/bufferstream.hpp>
using namespace boost::interprocess;

#include <iostream>
#include <memory>

CAppModule _Module;

int console_main();
int client_main();
int server_main();

// usage: TestWeaselIPC.exe [/start | /stop | /console]

int _tmain(int argc, _TCHAR* argv[])
{
	if (argc == 1)  // no args
	{
		return client_main();
	}
	else if(argc > 1 && !wcscmp(L"/start", argv[1]))
	{
		return server_main();
	}
	else if (argc > 1 && !wcscmp(L"/stop", argv[1]))
	{
		weasel::Client client;
		if (!client.Connect())
		{
			std::cerr << "server not running." << std::endl;
			return 0;
		}
		client.ShutdownServer();
		return 0;
	}
	else if (argc > 1 && !wcscmp(L"/console", argv[1]))
	{
		return console_main();
		return 0;
	}

	return -1;
}

bool launch_server()
{
	int ret = (int)ShellExecute( NULL, L"open", L"TestWeaselIPC.exe", L"/start", NULL, SW_NORMAL);
	if (ret <= 32)
	{
		std::cerr << "failed to launch server." << std::endl;
		return false;
	}
	return true;
}

bool read_buffer(LPWSTR buffer, size_t length, LPWSTR dest)
{
	wbufferstream bs(buffer, length);
	bs.read(dest, WEASEL_IPC_BUFFER_LENGTH);
	return bs.good();
}

const char* wcstomb(const wchar_t* wcs)
{
	const int buffer_len = 8192;
	static char buffer[buffer_len];
	WideCharToMultiByte(CP_OEMCP, NULL, wcs, -1, buffer, buffer_len, NULL, FALSE);
	return buffer;
}

int console_main()
{
	weasel::Client client;
	if (!client.Connect())
	{
		std::cerr << "failed to connect to server." << std::endl;
		return -2;
	}
	client.StartSession();
	if (!client.Echo())
	{
		std::cerr << "failed to start session." << std::endl;
		return -3;
	}
	
	while (std::cin.good())
	{
		int ch = std::cin.get();
		if (!std::cin.good())
			break;
		bool eaten = client.ProcessKeyEvent(weasel::KeyEvent(ch, 0));
		std::cout << "server replies: " << eaten << std::endl;
		if (eaten)
		{
			WCHAR response[WEASEL_IPC_BUFFER_LENGTH];
			bool ret = client.GetResponseData(std::bind<bool>(read_buffer, std::placeholders::_1, std::placeholders::_2, std::ref(response)));
			std::cout << "get response data: " << ret << std::endl;
			std::cout << "buffer reads: " << std::endl << wcstomb(response) << std::endl;
		}
	}

	client.EndSession();
	
	return 0;
}

int client_main()
{
	//launch_server();
	Sleep(1000);
	weasel::Client client;
	if (!client.Connect())
	{
		std::cerr << "failed to connect to server." << std::endl;
		return -2;
	}
	client.StartSession();
	if (!client.Echo())
	{
		std::cerr << "failed to login." << std::endl;
		return -3;
	}
	bool eaten = client.ProcessKeyEvent(weasel::KeyEvent(L'A', 0));
	std::cout << "server replies: " << eaten << std::endl;
	if (eaten)
	{
		WCHAR response[WEASEL_IPC_BUFFER_LENGTH];
		bool ret = client.GetResponseData(std::bind<bool>(read_buffer, std::placeholders::_1, std::placeholders::_2, std::ref(response)));
		std::cout << "get response data: " << ret << std::endl;
		std::cout << "buffer reads: " << std::endl << wcstomb(response) << std::endl;
	}
	client.EndSession();

	system("pause");
	return 0;
}

class TestRequestHandler : public weasel::RequestHandler
{
public:
	TestRequestHandler() : m_counter(0)
	{
		std::cerr << "handler ctor." << std::endl;
	}
	virtual ~TestRequestHandler()
	{
		std::cerr << "handler dtor: " << m_counter << std::endl;
	}
	virtual RimeSessionId FindSession(RimeSessionId session_id) override
	{
		std::cerr << "FindSession: " << session_id << std::endl;
		return (session_id <= m_counter ? session_id : 0);
	}
	virtual RimeSessionId AddSession(LPWSTR buffer, EatLine eat = 0) override
	{
		std::cerr << "AddSession: " << m_counter + 1 << std::endl;
		return ++m_counter;
	}
	virtual RimeSessionId RemoveSession(RimeSessionId session_id) override
	{
		std::cerr << "RemoveClient: " << session_id << std::endl;
		return 0;
	}
	virtual BOOL ProcessKeyEvent(weasel::KeyEvent keyEvent, RimeSessionId session_id, EatLine eat) override
	{
		std::cerr << "ProcessKeyEvent: " << session_id 
			  << " keycode: " << keyEvent.keycode 
			  << " mask: " << keyEvent.mask 
			  << std::endl;
		eat(std::wstring(L"Greeting=Hello, 小狼毫.\n"));
		return TRUE;
	}
	virtual void CommitComposition(RimeSessionId session_id) override {}
	virtual void ClearComposition(RimeSessionId session_id) override {}
	virtual void SelectCandidateOnCurrentPage(size_t index, RimeSessionId session_id) override {}
	virtual bool HighlightCandidateOnCurrentPage(size_t index, RimeSessionId session_id, EatLine eat) override { return false; }
	virtual bool ChangePage(bool backward, RimeSessionId session_id, EatLine eat) override { return false; }
	virtual void FocusIn(PARAM param, RimeSessionId session_id) override {}
	virtual void FocusOut(PARAM param, RimeSessionId session_id) override {}
	virtual void UpdateInputPosition(RECT const& rc, RimeSessionId session_id) override {}
	virtual void StartMaintenance() override {}
	virtual void EndMaintenance() override {}
	virtual void SetOption(RimeSessionId session_id, const std::string& opt, bool val) override {}
	virtual void UpdateColorTheme(bool darkMode) override {}
private:
	unsigned int m_counter;
};

int server_main()
{
	HRESULT hRes = _Module.Init(NULL, GetModuleHandle(NULL));
	ATLASSERT(SUCCEEDED(hRes));

	weasel::Server server;
	//weasel::UI ui;
	//const std::unique_ptr<weasel::RequestHandler> handler(new RimeWithWeaselHandler(&ui));
	const std::unique_ptr<weasel::RequestHandler> handler(new TestRequestHandler);

	server.SetRequestHandler(handler.get());
	if (!server.Start())
		return -4;
	std::cerr << "server running." << std::endl;
	int ret = server.Run();
	std::cerr << "server quitting." << std::endl;
	return ret;
}
