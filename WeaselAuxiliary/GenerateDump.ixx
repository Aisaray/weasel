module;
#include "pch.h"
#include "framework.h"
#include <Windows.h>
#include <DbgHelp.h>
export module GenerateDump;
import <memory>;
import <string>;
import <filesystem>;
import <format>;
import <chrono>;

export
{
	class CreateDump
	{
	public:
		CreateDump();
		~CreateDump();

		static CreateDump* Instance();
		static long __stdcall UnhandleExceptionFilter(_EXCEPTION_POINTERS* ExceptionInfo);
		// ����Dump�ļ����쳣ʱ���Զ����ɡ����Զ�����.dmp�ļ�����׺
		void DeclarDumpFile(std::wstring_view dmpFileName = L"");

	private:
		static std::wstring m_dumpFile;
		static std::shared_ptr<CreateDump*> m_instance;
	};

	void CatchUnhandledException();
}