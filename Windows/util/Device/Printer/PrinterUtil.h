#pragma once
#include <General/util/BaseUtil.hpp>
#include <initializer_list>
#include <Windows.h>
#include <string>
#include <vector>

namespace WinPrintWrapper {
	class WinSpoolerWrapper;
	//single
	struct PrinterInfo {
		PRINTER_INFO_2* printerInfo;
		DWORD printerStatus;
	};
	//enum
	struct PrinterInfos {
		unsigned int printerInfoNum;
		PRINTER_INFO_2* printerInfos;
		DWORD* printerStatus;
	};
	//enum
	struct PrinterDriverInfos {
		int driverInfoNum;
		DRIVER_INFO_2* driverInfos;
	};
	class PdfParam {
	public:
		unsigned int copy;
		unsigned int pageFrom;
		unsigned int pageTo;
		bool collate;
		bool color;
		enum PageSet{
			flat,
			odd,
			even
		} foe;
		enum Duplex{
			duplex,
			duplexshort,
			duplexlong,
			simplex
		} savePaper;
		enum Paper{
			letter,
			legal,
			tabloid,
			statement,
			A2,
			A3,
			A4,
			A5,
			A6
		} paperSize;
		//eg. "1-3,2x,collate,paper=A4,duplex,even"
		std::wstring ToSumatraCmdStr() const;

	};

	class PrinterBase {
	public:
		static wchar_t* PrinterStatusToWstr(DWORD status);
		static std::vector<std::wstring> JobStatusToWstr(DWORD status);
		static std::vector<std::string> PrinterCapabilitiesToWstrs(UINT32* capabilities, UINT len);
		static std::string PrinterCapabilityToStr(UINT32 capability);
		static std::string PrinterPaperSizeToStr(UINT32 size);
		static WCHAR* GetDefaultPrinterName();
		static BOOL SetPrinterDevmode(wchar_t* pPrinterName, DWORD field, DWORD value);
	};
	class PrinterJobManager {
	public:
		PrinterJobManager(std::wstring printerName=L"") :printerName(printerName) {}
		bool SetPrinter(std::wstring printerName);
		bool GetPrinterJobs(JOB_INFO_2W** ppJobInfo, int* pcJobs, DWORD* printerStatus);
		bool GetPrinterJob(DWORD jobID, JOB_INFO_2W** pJobInfo);
		bool ControlJob(std::wstring printerName,const wchar_t* document, DWORD commond);

	private:
		std::wstring printerName;
		HANDLE hPrinter;
	};
}
