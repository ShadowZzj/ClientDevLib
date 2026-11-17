1) Ensure your Go installation is working and that all enviroment variables are set (GOPATH, GOROOT, etc).

2) Open a copy of Command Prompt in any directory within your GOPATH (go/, go/src, go/pkg, etc).

3) Run the command "go get github.com/oreans/virtualizersdk".

4) Now the package has been installed, you can include it in your application by importing "github.com/oreans/virtualizersdk" alongside your other imports.

5) To specify a start point for a macro, you need to call the Macro function of the virtualizersdk package, using a macro as a parameter.

This is done as follows: "virtualizersdk.Macro(virtualizersdk.TIGER_BLACK_START)"

6) To specify an end point for a macro, it is the same as specifying a start point, but instead it is done as follows: "virtualizersdk.Macro(virtualizersdk.TIGER_BLACK_END)"

TIGER_BLACK_START and TIGER_BLACK_END are just examples, and any Macro of your choosing can be used.