//#ifdef DEBUGLOG

#include <iostream>
#include <windows.h>
#include <string>
#define LOG_BUF_LEN 1024

#define TLDC(LLV, ...) \
	CAutoTime t;	\
	t.SetString(LLV, __VA_ARGS__)

#define LDC0(...) CDebugLog::GetInstance()->OutDebug(LOG_NORMAL,__VA_ARGS__)
#define LDC(LLV, ...) CDebugLog::GetInstance()->OutDebug(LLV,__VA_ARGS__)


enum DEBUG_LOG_LEVEL
{
	LOG_NORMAL,
	LOG_WARNING,
	LOG_IMPORTANT
};

class CDebugLog
{
private:
	CDebugLog(){

		AllocConsole();
		m_hConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE); 
		SetConsoleTextAttribute(m_hConsoleHandle,FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
		freopen("CONOUT$", "w", stdout);
		
	}
	~CDebugLog(){
		CloseHandle(m_hConsoleHandle);
		FreeConsole();
	}
	
	HANDLE	m_hConsoleHandle;
	static CDebugLog* m_pHandle;

	void Out(std::string str, DEBUG_LOG_LEVEL llv = DEBUG_LOG_LEVEL::LOG_NORMAL) {

		if (LOG_NORMAL != llv)
		{
			switch(llv)
			{
			case LOG_WARNING:

				SetConsoleTextAttribute(m_hConsoleHandle,FOREGROUND_INTENSITY | FOREGROUND_GREEN);
				break;

			case LOG_IMPORTANT:

				SetConsoleTextAttribute(m_hConsoleHandle,FOREGROUND_INTENSITY | FOREGROUND_RED);
				break;
			}

			
			std::printf(str.c_str());
			SetConsoleTextAttribute(m_hConsoleHandle,FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
		} else {
			std::printf(str.c_str());
		}

	}

public:
	
	static CDebugLog* GetInstance() {

		if (nullptr == m_pHandle)
		{
			m_pHandle = new CDebugLog;
		}

		return m_pHandle;
	}

	void OutDebug(DEBUG_LOG_LEVEL llv, const char *format, ...)
	{
	    va_list arg;
	    char tmp[LOG_BUF_LEN] = {0};
	    va_start(arg, format); 
	    vsprintf(tmp, format, arg); 
	    va_end(arg); 
	    
	    Out(tmp, llv);
	}
};

class CAutoTime{
public:
	CAutoTime(){
		m_dwStartTime = GetTickCount();
		memset(szLogBuf, 0, sizeof(szLogBuf));
	}
	~CAutoTime(){
		auto ti = GetTickCount() - m_dwStartTime;

		char time[128] = {0};
		sprintf(time, " ºÄÊ±: %ld ms", ti);

		auto r = std::string(szLogBuf) + std::string(time);
		CDebugLog::GetInstance()->OutDebug(m_lvLog, r.c_str());
	}

	void SetString(DEBUG_LOG_LEVEL llv, const char *format, ...) {

		va_list arg;

		va_start(arg, format); 
		vsprintf(szLogBuf, format, arg); 
		va_end(arg);

		m_lvLog = llv;
	}

private:
	DWORD m_dwStartTime;
	char szLogBuf[LOG_BUF_LEN];
	DEBUG_LOG_LEVEL m_lvLog;
};

CDebugLog* CDebugLog::m_pHandle = nullptr;

//#endif