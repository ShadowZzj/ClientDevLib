#include <iostream>
#include "CPdfParser.h"
#include <string>
#include "CStrCvt.h"

int main(int argc, char* argv[])
{
	bool bRet = false;
	bool IsEncrypted = false;

    for (int i = 0; i < 1000; i++)
    {
		std::wstring text;
        CPdfParser* fileParser = new CPdfParser();
        
		std::wstring wFileName = string2wstring(argv[1]);
		if (!fileParser->Open(wFileName.c_str()))
		{
			if (fileParser->IsEncrypted() && IsEncrypted)
			{
				IsEncrypted = true;
			}
			break;
		}

		if (fileParser->IsEncrypted())
		{
			if (IsEncrypted)
				IsEncrypted = true;
			break;
		}

		bRet |= fileParser->GetPropText(text);

		bRet |= fileParser->GetText(text);
		fileParser->GetEmbedText(text);

		delete fileParser;

		cout << i << endl;
    }
    return 0;
}