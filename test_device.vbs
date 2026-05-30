Set fso = CreateObject("Scripting.FileSystemObject")  
WScript.Echo "Testing device access..."  
Set f = CreateObject("Scripting.FileSystemObject")  
On Error Resume Next  
Set fs = CreateObject("Scripting.FileSystemObject")  
Set f = fs.GetFile("\\.\AMDBC250DreamV43")  
If Err.Number = 0 Then  
  WScript.Echo "Device EXISTS"  
Else  
