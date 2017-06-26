// SchedulerServer.cpp : �������̨Ӧ�ó������ڵ㡣
//

#include "stdafx.h"
#include <iostream>
#include <vector>
#include <string>
#include <io.h>
#include <string>
#include <process.h >
#include <stdlib.h>
#include <direct.h>
#include <windows.h>
#include <fstream>
#include "LDebug.h"
#include <algorithm>

using namespace std;

//ʱ�����ޣ���ͷ��Ƚ϶࣬һ�дӼ�,�������е��ң����£��ܷ���������ҵ��ok
vector<string> m_strFolderList;
vector<string> m_strFileList;
HANDLE g_hFileMapping;
LPVOID g_lpShareBufferHeader;
string g_strInFilePath;
string g_strTargetFile;
string g_strOutFilePath;
string g_strModulePath;
string g_strMemMapName;
unsigned int g_nThreshold;
char g_szCmdFormat[MAX_PATH];
int g_nCurrentTaskIndex;
unsigned int g_nOddFileCount;
unsigned int g_nTZPicture;
unsigned int g_nLessThanThreshold;

#define TASK_ELE_COUNT 88
#define CORE_MAX_COUNT 16	//��˾���������16�˵�PC����


/*
���ʺ���boost::interprocess�����뻷����Ƚ��鷳�����ˣ����ڴ�ӳ������
��������ѹ���·����ȡ���������ǵ����������ļ��У���Ҫ�Թ���·�������ı������鷳
��Σ����̶�ϵͳ��Դ�Ŀ�������Ҫ�󲻸ߣ����ñ���ȫ·����

bIsAnlyze:���ļ��Ƿ񱻷���
uSquence:��ţ������ӽ��̶�ȡ
*/

void ShowLogHeader();
struct DrawingData{

	bool	bIsTZPicture;		//�Ƿ�������ͼֽ
	int		nEdoNum;            //ͼԪ����
	bool	bIsAnalyze;			//�Ƿ���Ҫ����
};

struct FileItem{
	char		 szFileName[MAX_PATH * 2];
	bool		 bIsOddFile;
	unsigned int uSquence;
	DrawingData	 ddResult;
	FileItem()
	{
		memset(this, 0, sizeof(*this));
	}
};

//����ṹ����������ӽ��̵����״̬,��Ȼ���ڴ湲����������
struct SubProcessStatus {
	bool			bIsRecord;				//��ʾ�����û�м�¼�ӽ���			
	unsigned int	nProcessID;
	bool			bIsOver;
	int				nStart;
	int				nEnd;
	int				nOddFileCount;
	int				nTZPitcure;
	int				nLessThanThreshold;
};

struct FileHeader{
	SubProcessStatus stStatusControl[CORE_MAX_COUNT];
	unsigned int nFileCount;
	FileHeader()
	{
		memset(this, 0, sizeof(*this));
	}
};

void WalkFile(std::string strFolderPath){

	if (strFolderPath.empty())
		return;

	long hFile = 0;
	struct _finddata_t fileinfo;  
	string p;
	if((hFile = _findfirst(p.assign(strFolderPath).append("\\*").c_str(),&fileinfo)) !=  -1)  
	{  
		do  
		{   
			if((fileinfo.attrib &  _A_SUBDIR))  
			{  
				if(strcmp(fileinfo.name,".") != 0  &&  strcmp(fileinfo.name,"..") != 0)
				{
					m_strFolderList.push_back(p.assign(strFolderPath).append("\\").append(fileinfo.name));
					WalkFile(p.assign(strFolderPath).append("\\").append(fileinfo.name));
				}
			}  
			else
			{  
				m_strFileList.push_back(p.assign(strFolderPath).append("\\").append(fileinfo.name));

			}  
		}while(_findnext(hFile, &fileinfo)  == 0); 

		_findclose(hFile); 
	} 
}

bool InitFileMap()
{
	 g_strMemMapName = string("ShareMemory");                // �ڴ�ӳ���������

	 g_hFileMapping = ::OpenFileMapping(FILE_MAP_ALL_ACCESS, 0, g_strMemMapName.c_str());
	 if (g_hFileMapping == 0)
	 {
		 g_hFileMapping = ::CreateFileMapping(INVALID_HANDLE_VALUE,
									NULL,
									PAGE_READWRITE,
									0,
									m_strFileList.size()*sizeof(FileItem)+sizeof(FileHeader),
									g_strMemMapName.c_str());

		 if (INVALID_HANDLE_VALUE == g_hFileMapping)
			 return false;

		 g_lpShareBufferHeader = ::MapViewOfFile(g_hFileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);

		 memset(g_lpShareBufferHeader, 0, m_strFileList.size()*sizeof(FileItem)+sizeof(FileHeader));

		 FileHeader* pHeader = (FileHeader*)g_lpShareBufferHeader;
		 pHeader->nFileCount = m_strFileList.size();

		 FileItem* pItem = (FileItem*)(pHeader+1);
		 for (int i=0; i<pHeader->nFileCount; i++)
		 {
			 memmove_s(pItem->szFileName, sizeof(pItem->szFileName), m_strFileList[i].c_str(), m_strFileList[i].length());
			 pItem->bIsOddFile	= false;
			 pItem->uSquence	= 0;
			 pItem++;
		 }
	 }
	 else
	 {
		 g_lpShareBufferHeader = ::MapViewOfFile(g_hFileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	 }

	 return true;
}

void ReleaseFileMap()
{
	UnmapViewOfFile(g_lpShareBufferHeader);
	CloseHandle(g_hFileMapping);
}

/*
���������ʵĽ��̻�ռ��һ��cpu�������ӽ��̵ĸ������ܶ���CPu�ĺ�������ֻ��һ���ˣ�ֻ����һ������
*/
unsigned int GetCoreCount()
{
	SYSTEM_INFO si;  
	GetSystemInfo(&si);  
	return  si.dwNumberOfProcessors;
}

struct ProcessTaskItem {
	int nPID;					//��ǰ���̵�ID
	int nStartIndex;
	int nEndIndex;
};


unsigned int CheckActiveProcess()	//�����ж��ٸ���Ծ���ӽ���
{
	FileHeader* pHeader = (FileHeader*)g_lpShareBufferHeader;

	auto cn(0);
	for (int i=0; i<CORE_MAX_COUNT; i++)
	{
		if (pHeader->stStatusControl[i].bIsRecord)
		{
			cn++;
		}
	}

	return cn;
}

unsigned int GetFreeChunkInmap()
{
	for (auto o=0; o<CORE_MAX_COUNT; o++)
	{
		if (((FileHeader*)g_lpShareBufferHeader)->stStatusControl[o].bIsRecord == false )
			return o;
	}

	return -1;
}

unsigned int CreateSubProcess(unsigned int nStart, unsigned int nEnd)
{
	auto nmapIndex = GetFreeChunkInmap();
	if (-1 == nmapIndex)
	{
		LDC(LOG_IMPORTANT, "�ֲ������еı�ǿ�!\n");
		return -1;
	}

	sprintf_s(g_szCmdFormat,"\"%s\" \"%s\" \"%s\" \"%d\" \"%d\" \"%d\" \"%d\"", 
		g_strMemMapName.c_str(), 
		g_strTargetFile.c_str(), 
		g_strOutFilePath.c_str(),
		g_nThreshold, nStart, nEnd, nmapIndex);


	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	
	si.cb = sizeof(STARTUPINFO);
	si.lpReserved = NULL;
	si.lpDesktop = NULL;
	si.lpTitle = NULL;
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_SHOW;
	si.cbReserved2 = NULL;
	si.lpReserved2 = NULL;
	
	auto bRet = ::CreateProcess (   
		g_strModulePath.c_str(),  
		g_szCmdFormat,     
		NULL,   
		NULL,   
		FALSE,   
		CREATE_NEW_CONSOLE,   
		NULL,   
		NULL,   
		&si,   
		&pi);
	
	if (bRet)
	{
		::CloseHandle (pi.hThread);  
		::CloseHandle (pi.hProcess);
	}
	else
		return -1;

	
	((FileHeader*)g_lpShareBufferHeader)->stStatusControl[nmapIndex].bIsRecord	 = true;
	((FileHeader*)g_lpShareBufferHeader)->stStatusControl[nmapIndex].bIsOver	 = false;
	((FileHeader*)g_lpShareBufferHeader)->stStatusControl[nmapIndex].nProcessID	 = pi.dwProcessId;
	((FileHeader*)g_lpShareBufferHeader)->stStatusControl[nmapIndex].nStart		 = nStart;
	((FileHeader*)g_lpShareBufferHeader)->stStatusControl[nmapIndex].nEnd		 = nEnd;
	
	
	LDC(LOG_NORMAL, "�����ӽ���(0x%08x),ִ������: %d-%d ,ʣ��: %d \n", pi.dwProcessId, nStart+1, nEnd, m_strFileList.size() - nEnd);
	
	return pi.dwProcessId;
}

int AllocateTaskItem()
{
	auto nMaxIndex(-1);
	if (g_nCurrentTaskIndex == (m_strFileList.size()-1))
		return nMaxIndex;
	
	if (g_nCurrentTaskIndex < (int)m_strFileList.size())
	{
		nMaxIndex = g_nCurrentTaskIndex + TASK_ELE_COUNT;
		if (nMaxIndex >= m_strFileList.size())
			nMaxIndex = m_strFileList.size()-1;

	}

	//..log

	return nMaxIndex;
}

int CheckAndWriteToResult(){

	FileHeader* pHeader = (FileHeader*)g_lpShareBufferHeader;

	auto cn(0);
	auto cnWorking(0);
	for (int i=0; i<CORE_MAX_COUNT; i++)
	{
		if (pHeader->stStatusControl[i].bIsRecord && pHeader->stStatusControl[i].bIsOver)
		{
			//log
			LDC(LOG_IMPORTANT, "����: 0x%08x ,ִ���������,�������� %d ��\n", pHeader->stStatusControl[i].nProcessID, 
				pHeader->stStatusControl[i].nEnd-pHeader->stStatusControl[i].nStart);
			//write to log
			pHeader->stStatusControl[i].bIsRecord = false;
			pHeader->stStatusControl[i].bIsOver   = false;
			g_nOddFileCount += pHeader->stStatusControl[i].nOddFileCount;
			g_nTZPicture	+= pHeader->stStatusControl[i].nTZPitcure;
			g_nLessThanThreshold += pHeader->stStatusControl[i].nLessThanThreshold;
			cn++;
		}
		if (pHeader->stStatusControl[i].bIsRecord)
		{
			cnWorking++;
		}
	}

	return cnWorking;
}

DWORD SchedulerProcess() {

	auto bIsTaskOver(false);
	while (true) {

		auto cnWorkingProcess = CheckAndWriteToResult();
		if (bIsTaskOver)
		{
			static int nRecordLastRemainProcessForLog = 0;
			
			if (nRecordLastRemainProcessForLog != cnWorkingProcess)
			{
				LDC(LOG_WARNING, "�������,���ڵȴ�����������ҵ���,ʣ��:%d������!\n", cnWorkingProcess);
				nRecordLastRemainProcessForLog = cnWorkingProcess;
			}
			if (cnWorkingProcess == 0)
			{
				break;
			}
			continue;
		}
		auto cnActive = CheckActiveProcess();
		if (cnActive < GetCoreCount()-1)
		{
			auto nIdex = AllocateTaskItem();
			if (-1 == nIdex)
			{
				LDC(LOG_IMPORTANT, "�������,���ڵȴ�����������ҵ���!\n");
				bIsTaskOver = true;
				//log
				continue;
			}

			auto PID = CreateSubProcess(g_nCurrentTaskIndex, nIdex);
			g_nCurrentTaskIndex = nIdex;
			
			if (-1 == PID)
			{
				//log
				LDC(LOG_IMPORTANT, "��������ʧ��: errcode:%d !\n", GetLastError());
				break;
			}
			
			//log

		}
		Sleep(2000);
	}
	return -1;
}

bool Init(int argc, char* argv[])
{
	if (argc != 5)
	{
		LDC(LOG_IMPORTANT, "����������������!");
		return false;
	}
	g_strInFilePath		= string(argv[1]);//�ļ���·��
	g_strTargetFile		= string(argv[2]);
	g_strOutFilePath	= string(argv[3]);
	g_nThreshold		= atoi(argv[4]);

	memset(g_szCmdFormat, 0, sizeof(g_szCmdFormat));
	g_nCurrentTaskIndex		= -1;
	g_lpShareBufferHeader	= nullptr;
	g_nOddFileCount			= 0;
	g_nTZPicture			= 0;
	g_nLessThanThreshold	= 0;

	char szMePath[MAX_PATH] = {0};
	_getcwd(szMePath, MAX_PATH);
	g_strModulePath = string(szMePath);
	g_strModulePath.append("\\GCADStatisticsWork.exe");
	SetConsoleTitle("���ȷ���(����ر�,��bug��ϵxiaob-a)");
	return true;
}

void WriteToLog()
{
	
	SYSTEMTIME t;
	GetLocalTime(&t);

	char szLogName[MAX_PATH]={0};
	sprintf(szLogName, "\\Result_%02d_%02d_%02d_%02d.log", t.wDay, t.wHour, t.wMinute, t.wSecond);
	
	ofstream out;  
	out.open(g_strOutFilePath.append(szLogName).c_str(), ios::app|ios::out);

	if(out.is_open())
	{
		out << "�����ļ���:"		<< g_strInFilePath.c_str()	<< std::endl;
		out << "�趨��ֵ:"		<< g_nThreshold				<< endl;
		out << "���ҵ�"			<< m_strFileList.size()		<< "���ļ�!" << endl;
		out << "�쳣�ļ�:"		<< g_nOddFileCount			<< endl;
		out << "������:"			<< (m_strFileList.size() - g_nOddFileCount)*100/m_strFileList.size() <<endl;
		out << "�����ļ�:"		<< g_nTZPicture				<< endl;
		out << "С����ֵ���ļ�:"	<< g_nLessThanThreshold		<< endl;
		out << endl;

		FileItem* pResult = (FileItem*)((FileHeader*)g_lpShareBufferHeader+1);

		std::sort(pResult, pResult+m_strFileList.size(), [&](FileItem& l, FileItem& r)->bool{
			return l.ddResult.nEdoNum > r.ddResult.nEdoNum;
		});

		for (auto i=0; i<m_strFileList.size(); i++)
		{
			auto GetFileName = [&](string path)->string{

				int pos = path.find_last_of('\\');
				return path.substr(pos+1);
			};
			FileItem* pItem  = (pResult+i);

			out << GetFileName((pResult+i)->szFileName);
			out << "	";
			if (pItem->bIsOddFile)
			{
				out << "�쳣�ļ�";
			}
			else
			{
				pItem->ddResult.bIsAnalyze ? out << "��Ҫ����" : out << "�������";
				out << "	";
				pItem->ddResult.bIsTZPicture ? out << "����ͼֽ" : out << "������ͼֽ";
				out << "	";
				out << "ͼԪ����:" << pItem->ddResult.nEdoNum;
			}
			out << endl;
			
		}

		out.close();
	}
	else {
		LDC(LOG_IMPORTANT, "�̻���[%s]��ʧ��!", g_strOutFilePath.c_str());
	}
}

void ShowLogHeader()
{
	LDC(LOG_WARNING, "�������ļ��� %d ����ÿ�η��� %d ���� Ԥ�Ƶ��� %d ��\n", m_strFileList.size(), TASK_ELE_COUNT, m_strFileList.size()%TASK_ELE_COUNT ? m_strFileList.size()/TASK_ELE_COUNT +1 : m_strFileList.size()/TASK_ELE_COUNT);
	LDC(LOG_IMPORTANT, "����������: %d, ����н�����: %d, �����ڴ��(%x) \n",GetCoreCount(), GetCoreCount()-1, (DWORD)g_lpShareBufferHeader);
}

void ShowLogResult()
{
	LDC(LOG_WARNING,"�������:\n");
	LDC(LOG_NORMAL,"�����ļ���: %s \n", g_strInFilePath.c_str() );
	LDC(LOG_NORMAL,"�趨��ֵ: %d\n", g_nThreshold  );
	LDC(LOG_NORMAL,"���ҵ�: %d ���ļ�\n" , m_strFileList.size() );
	LDC(LOG_NORMAL,"�쳣�ļ�: %d\n", g_nOddFileCount  );
	LDC(LOG_NORMAL,"������: %d%%\n" , (m_strFileList.size() - g_nOddFileCount)*100/m_strFileList.size() );
	LDC(LOG_NORMAL,"�����ļ�: %d\n" , g_nTZPicture );
	LDC(LOG_NORMAL,"С����ֵ���ļ�: %d\n", g_nLessThanThreshold );
}

//�ӽ�����һ�ֿ��ܣ�����Ϊ�رջ��߿���������������Ͳ���д�������������µ��ȷ�����ͳ�ƽ������Ҫ�����������
//��ʱûʱ�䣬��дһ��
//
DWORD InputCmdToaAvoidAccident(){

	char szInputCmd[1024] = {0};
	std::cin >> szInputCmd;


	std::string cmd  = std::string(szInputCmd);

	//�Ͳ���������ȥ���ո��ˣ�����רҵ��ô
	if (cmd == "help")
	{
		std::cout << cmd.c_str() <<endl;
		std::cout << "You can follow command:" << endl;
		std::cout << "[show all]:\n		Displays information for all pending processes" <<endl;
		std::cout << "[retry [pid]]:\n		Reactivate the process" << endl;
		std::cout << "[show [pid]]:\n		Displays full details" <<endl;
	}
	else if (cmd == "show all")
	{
		std::cout << szInputCmd << endl;
	}
	else
	{
		auto pos = cmd.find("retry");
		auto len = cmd.length();
		if (pos != -1)
		{
			std::cout << cmd.substr(pos + 5);
		}
	}
	system("pause");
}

int main(int argc, char* argv[])
{
	
	unsigned int nStartTime = GetTickCount();
	if (!Init(argc, argv))
	{
		system("pause");
		return -1;
	}
	
	WalkFile(g_strInFilePath);
	
	if (false ==InitFileMap())
	{
		std::cout << "�����ڴ�ӳ���ļ�ʧ��,�������: " << GetLastError() <<std::endl;
		system("pause");
		return -1;
	}
	ShowLogHeader();

	//���������ķ�������Ҫ����ӽ��̵ľ������Ȼ�����ڴ�ӳ�䣬�߼����Ѿ����ͬ��������
	HANDLE hSThread = CreateThread(NULL, 0, LPTHREAD_START_ROUTINE(SchedulerProcess), NULL, 0, 0);
	WaitForSingleObject(hSThread, INFINITE);
	WriteToLog();
	CloseHandle(hSThread);
	ReleaseFileMap();

	auto nWasteTime = GetTickCount() - nStartTime;
	LDC(LOG_WARNING, "������ͼֽ: %d ��,���ƺ�ʱ: %d ms,ƽ��ÿ�ź�ʱ: %d ms\n\n\n", m_strFileList.size(), nWasteTime, nWasteTime/m_strFileList.size());
	
	ShowLogResult();
	
	system("pause");
	return 0;
}

