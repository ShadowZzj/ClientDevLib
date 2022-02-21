//
//  main.cpp
//  client
//
//  Created by bixiangyang on 2022/1/14.
//

#include <iostream>

#include "CPptParser.h"

int main(int argc, const char * argv[])
{
    CPptParser::SetKepmDataDirectory(L"/Users/bixiangyang/projects/kepm_client/ClientDevLib/General/ThirdParty/catdoc-0.95");
    CPptParser parser;
    
    std::wstring content;
    parser.Open(L"/Users/bixiangyang/projects/catdoc-0.95/src/lua05.ppt");
    parser.GetText(content);
    
    return 0;
}
