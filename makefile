
all: win32-progressive-wndproc.exe
run: win32-progressive-wndproc.exe
	win32-progressive-wndproc.exe

win32-progressive-wndproc.exe: makefile main.cc
	cl.exe main.cc /Fewin32-progressive-wndproc.exe /EHsc /MD /Ob2 /O1 /W4 /std:c++latest
