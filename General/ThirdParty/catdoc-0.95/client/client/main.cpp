//
//  main.cpp
//  client
//
//  Created by bixiangyang on 2022/1/14.
//

#include <iostream>

#include "CPptParser.h"
#include "CStrCvt.h"

int main(int argc, const char * argv[])
{
    
    std::string str = "sss毕向阳yyy";
    std::wstring wstr = utf8_to_wstring(str);
    
    CPptParser::SetKepmDataDirectory(L"/Library/Application Support/kepm");
    CPptParser parser;
    
    std::wstring content;
    parser.Open(L"/Users/bixiangyang/xxx/ppt/test.ppt");
    //parser.Open(L"/Users/bixiangyang/projects/catdoc-0.95/src/lua05.ppt");
    parser.GetText(content);
    
    
    return 0;
}
