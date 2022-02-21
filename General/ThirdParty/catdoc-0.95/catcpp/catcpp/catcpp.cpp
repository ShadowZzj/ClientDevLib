//
//  catcpp.cpp
//  catcpp
//
//  Created by bixiangyang on 2022/1/14.
//

#include <iostream>
#include "catcpp.hpp"
#include "catcppPriv.hpp"

void catcpp::HelloWorld(const char * s)
{
    catcppPriv *theObj = new catcppPriv;
    theObj->HelloWorldPriv(s);
    delete theObj;
};

void catcppPriv::HelloWorldPriv(const char * s) 
{
    std::cout << s << std::endl;
};

