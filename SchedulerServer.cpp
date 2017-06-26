// SchedulerServer.cpp : 定义控制台应用程序的入口点。
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

//时间有限，手头活比较多，一切从简,代码风格有点乱，体谅，能方便的完成作业就ok
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
#define CORE_MAX_COUNT 16	//公司不会真的有16核的PC机吧


/*
最适合用boost::interprocess，编译环境搭建比较麻烦，算了，用内存映射管理吧
本来打算把公共路径提取出来，考虑到可能有子文件夹，需要对公共路径做多层的标记相对麻烦
其次，进程对系统资源的开销不大，要求不高，就用保存全路径吧

bIsAnlyze:本文件是否被分析
uSquence:编号，方便子进程读取
*/

void ShowLogHeader();
struct DrawingData{

	bool	bIsTZPicture;		//是否是天正图纸
	int		nEdoNum;            //图元数量
	bool	bIsAnalyze;			//是否需要分析
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

//这个结构体用来检测子进程的完成状态,既然有内存共享，这样方便
struct SubProcessStatus {
	bool			bIsRecord;				//表示这块有没有记录子进程			
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
	 g_strMemMapName = string("ShareMemory");                // 内存映射对象名称

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
分析倒入率的进程会占满一个cpu，所以子进程的个数不能多于CPu的核数，若只有一个核，只创建一个进程
*/
unsigned int GetCoreCount()
{
	SYSTEM_INFO si;  
	GetSystemInfo(&si);  
	return  si.dwNumberOfProcessors;
}

struct ProcessTaskItem {
	int nPID;					//当前进程的ID
	int nStartIndex;
	int nEndIndex;
};


unsigned int CheckActiveProcess()	//返回有多少个活跃的子进程
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
		LDC(LOG_IMPORTANT, "分不到空闲的标记块!\n");
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
	
	
	LDC(LOG_NORMAL, "创建子进程(0x%08x),执行任务: %d-%d ,剩余: %d \n", pi.dwProcessId, nStart+1, nEnd, m_strFileList.size() - nEnd);
	
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
			LDC(LOG_IMPORTANT, "进程: 0x%08x ,执行任务完毕,处理数据 %d 条\n", pHeader->stStatusControl[i].nProcessID, 
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
				LDC(LOG_WARNING, "调度完成,正在等待其他进程作业完成,剩余:%d个进程!\n", cnWorkingProcess);
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
				LDC(LOG_IMPORTANT, "调度完成,正在等待其他进程作业完成!\n");
				bIsTaskOver = true;
				//log
				continue;
			}

			auto PID = CreateSubProcess(g_nCurrentTaskIndex, nIdex);
			g_nCurrentTaskIndex = nIdex;
			
			if (-1 == PID)
			{
				//log
				LDC(LOG_IMPORTANT, "创建进程失败: errcode:%d !\n", GetLastError());
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
		LDC(LOG_IMPORTANT, "启动参数个数错误!");
		return false;
	}
	g_strInFilePath		= string(argv[1]);//文件夹路径
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
	SetConsoleTitle("调度服务(切勿关闭,有bug联系xiaob-a)");
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
		out << "分析文件夹:"		<< g_strInFilePath.c_str()	<< std::endl;
		out << "设定阈值:"		<< g_nThreshold				<< endl;
		out << "共找到"			<< m_strFileList.size()		<< "个文件!" << endl;
		out << "异常文件:"		<< g_nOddFileCount			<< endl;
		out << "导入率:"			<< (m_strFileList.size() - g_nOddFileCount)*100/m_strFileList.size() <<endl;
		out << "天正文件:"		<< g_nTZPicture				<< endl;
		out << "小于阈值的文件:"	<< g_nLessThanThreshold		<< endl;
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
				out << "异常文件";
			}
			else
			{
				pItem->ddResult.bIsAnalyze ? out << "需要分析" : out << "不需分析";
				out << "	";
				pItem->ddResult.bIsTZPicture ? out << "天正图纸" : out << "非天正图纸";
				out << "	";
				out << "图元个数:" << pItem->ddResult.nEdoNum;
			}
			out << endl;
			
		}

		out.close();
	}
	else {
		LDC(LOG_IMPORTANT, "固化到[%s]中失败!", g_strOutFilePath.c_str());
	}
}

void ShowLogHeader()
{
	LDC(LOG_WARNING, "待分析文件共 %d 个，每次分配 %d 个， 预计调度 %d 次\n", m_strFileList.size(), TASK_ELE_COUNT, m_strFileList.size()%TASK_ELE_COUNT ? m_strFileList.size()/TASK_ELE_COUNT +1 : m_strFileList.size()/TASK_ELE_COUNT);
	LDC(LOG_IMPORTANT, "本机核心数: %d, 最大并行进程数: %d, 分配内存块(%x) \n",GetCoreCount(), GetCoreCount()-1, (DWORD)g_lpShareBufferHeader);
}

void ShowLogResult()
{
	LDC(LOG_WARNING,"结果如下:\n");
	LDC(LOG_NORMAL,"分析文件夹: %s \n", g_strInFilePath.c_str() );
	LDC(LOG_NORMAL,"设定阈值: %d\n", g_nThreshold  );
	LDC(LOG_NORMAL,"共找到: %d 个文件\n" , m_strFileList.size() );
	LDC(LOG_NORMAL,"异常文件: %d\n", g_nOddFileCount  );
	LDC(LOG_NORMAL,"导入率: %d%%\n" , (m_strFileList.size() - g_nOddFileCount)*100/m_strFileList.size() );
	LDC(LOG_NORMAL,"天正文件: %d\n" , g_nTZPicture );
	LDC(LOG_NORMAL,"小于阈值的文件: %d\n", g_nLessThanThreshold );
}

//子进程有一种可能，被人为关闭或者卡死的情况，这样就不能写入分析结果，导致调度服务不能统计结果，需要额外的命令激活窗口
//暂时没时间，先写一半
//
DWORD InputCmdToaAvoidAccident(){

	char szInputCmd[1024] = {0};
	std::cin >> szInputCmd;


	std::string cmd  = std::string(szInputCmd);

	//就不考虑两边去掉空格了，操作专业点么
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
		std::cout << "创建内存映射文件失败,错误代码: " << GetLastError() <<std::endl;
		system("pause");
		return -1;
	}
	ShowLogHeader();

	//这里最合理的方法是需要检测子进程的句柄，既然用了内存映射，逻辑上已经解决同步的问题
	HANDLE hSThread = CreateThread(NULL, 0, LPTHREAD_START_ROUTINE(SchedulerProcess), NULL, 0, 0);
	WaitForSingleObject(hSThread, INFINITE);
	WriteToLog();
	CloseHandle(hSThread);
	ReleaseFileMap();

	auto nWasteTime = GetTickCount() - nStartTime;
	LDC(LOG_WARNING, "共处理图纸: %d 张,共计耗时: %d ms,平均每张耗时: %d ms\n\n\n", m_strFileList.size(), nWasteTime, nWasteTime/m_strFileList.size());
	
	ShowLogResult();
	
	system("pause");
	return 0;
}

