/****************************************************************************** 
/* Module: Vc_example
/* Description: Shows how to call SecureEngine Macros in Visual C++ language
/*
/* Authors: Rafael Ahucha  
/* (c) 2004 Oreans Technologies
/*****************************************************************************/ 


/****************************************************************************** 
/*                   Libraries used by this module
/*****************************************************************************/ 

#include "stdafx.h"
#include <stdio.h>
#include "Resource.h"
#include <commctrl.h>
#include <windows.h>
#include "..\..\..\..\Include\C\ThemidaSDK.h"


/****************************************************************************** 
/*                          Function prototypes
/*****************************************************************************/ 

LRESULT CALLBACK  MainHandler(HWND, UINT, WPARAM, LPARAM);


/****************************************************************************** 
/*                          Global variables
/*****************************************************************************/ 


/*****************************************************************************
* WinMain
*
*  Main program function
*
* Inputs
*  Standard WinMain parameters
* 
* Outputs
*  None
*
* Returns
*  Exit Code
******************************************************************************/

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
    DialogBox(GetModuleHandle(NULL), (LPCTSTR)IDD_ABOUTBOX, NULL, (DLGPROC)MainHandler);

    return 0;
}


/*****************************************************************************
* MainHandler
*
*  Message handler for dialog box
*
* Inputs
*  Standard Dlgbox message handle parameters
* 
* Outputs
*  None
*
* Returns
*  Exit Code
******************************************************************************/

LRESULT CALLBACK MainHandler(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
   int value = 0; 

    switch (message)
    {

    case WM_INITDIALOG:
        
        return TRUE;

    case WM_COMMAND:

        if (LOWORD(wParam) == IDCANCEL) 
        {
			EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }

        if (LOWORD(wParam) == IDC_REMOVED1)
        { 
            // the following code will be virtualized
      
            VM_TIGER_WHITE_START

            for (int i = 0; i < 100; i++)
            {
                value += (value * i * 4) << 1;
            }

            MessageBox(NULL, "The TIGER WHITE has successfully been executed", "VM Macro", MB_OK + MB_ICONINFORMATION);

            VM_TIGER_WHITE_END
		}
        else if (LOWORD(wParam) == IDC_REMOVED2)
        {
            // the following code will be virtualized
      
            VM_FISH_WHITE_START

            for (int i = 0; i < 100; i++)
            {
                value += (value * i) >> 2;
            }
            
            MessageBox(NULL, "The FISH WHITE macro has successfully been executed", "VM Macro", MB_OK + MB_ICONINFORMATION);

            VM_FISH_WHITE_END
		}
        else if (LOWORD(wParam) == IDC_REMOVED3)
        {
            // the following code will be virtualized
      
            VM_FISH_RED_START

            for (int i = 0; i < 100; i++)
            {
                value += (value * i / 4) >> 2;
            }
           
            MessageBox(NULL, "The FISH RED macro has successfully been executed", "VM Macro", MB_OK + MB_ICONINFORMATION);

            VM_FISH_RED_END
		}

	}
    return FALSE;
}
