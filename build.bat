@echo off

REM vulkan paths
set vki=-I"C:\VulkanSDK\1.3.290.0\Include"
set vkl=-LIBPATH:"C:\VulkanSDK\1.3.290.0\Lib"

REM compiler flags
set cf=-nologo -FC -Z7 -W4 -WX -wd4189 -wd4100 -wd4101

IF NOT EXIST bin mkdir bin
pushd bin
cl %cf% ..\main.c %vki% -link %vkl% user32.lib vulkan-1.lib
popd