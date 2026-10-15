param([string]$Compiler='g++')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try{
 & $Compiler -std=c++20 -shared -O2 -static -static-libgcc -static-libstdc++ ArjunNativePlugin.cpp native_api\imgui\imgui.cpp native_api\imgui\imgui_draw.cpp native_api\imgui\imgui_widgets.cpp native_api\imgui\imgui_tables.cpp -o ArjunSuperAudioNative.dll -lole32 -luuid -limm32
 if($LASTEXITCODE -ne 0){throw 'Native DLL build failed'}
}finally{Pop-Location}
