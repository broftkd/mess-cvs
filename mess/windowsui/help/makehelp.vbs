Option Explicit
Dim objFSO
Dim objTextStream
Dim objTextStream2
Dim objSysInfoDictionary
Dim objFile
Dim strObjDir
Dim strSysInfoFile
Dim strHHC
Dim strLine
Dim strSystem
Dim strSystemText
Dim arrSystems
Dim i
Dim j

Function HtmlEncode(ByVal strText)
	Dim i
	Dim ch
	Dim strHtml
	
	For i = 1 To Len(strText)
		ch = Mid(strText, i, 1)
		If ((ch = " ") Or ((UCase(ch) >= "A") And (UCase(ch) <= "Z"))) Then
			strHtml = strHtml & ch
		Else
			strHtml = strHtml & "&#" & Asc(ch) & ";"
		End If			
	Next
	HtmlEncode = strHtml
End Function

Sub AddFile(ByVal objTextStream, ByVal strBaseDir, ByVal strRelPath, ByVal strTitle)
	Dim objFile
	Set objFile = objFSO.GetFile(strBaseDir & "\" & strRelPath)
	objFile.Copy(strObjDir & "\" & strRelPath)
	objTextStream.WriteLine("		<LI> <OBJECT type=""text/sitemap"">")
	objTextStream.WriteLine("			<param name=""Name"" value=""" & strTitle & """>")
	objTextStream.WriteLine("			<param name=""Local"" value=""" & strRelPath & """>")
	objTextStream.WriteLine("			</OBJECT>")
End Sub

Sub AddImage(ByVal objTextStream, ByVal strBaseDir, ByVal strRelPath)
	Dim objFile
	Set objFile = objFSO.GetFile(strBaseDir & "\" & strRelPath)
	objFile.Copy(strObjDir & "\" & strRelPath)
End Sub

strObjDir = "obj\mess\mess\windowsui\help\"
strSysInfoFile = "sysinfo.dat"
strHHC = """C:\Program Files\HTML Help Workshop\hhc.exe"""

Set objFSO = CreateObject("Scripting.FileSystemObject")
If objFSO.FileExists(strObjDir & "mess.chm") Then
	objFSO.DeleteFile(strObjDir & "mess.chm")
End If
On Error Resume Next
objFSO.CreateFolder(strObjDir)
objFSO.CreateFolder(strObjDir & "sysinfo")
objFSO.CreateFolder(strObjDir & "html")
objFSO.CreateFolder(strObjDir & "images")
On Error Goto 0

' ---------------------------------------------------------------------------
' Make sysinfo pages
' ---------------------------------------------------------------------------
Set objSysInfoDictionary = CreateObject("Scripting.Dictionary")
Set objTextStream = objFSO.OpenTextFile(strSysInfoFile)
Do While Not objTextStream.AtEndOfStream
	strLine = objTextStream.ReadLine
	If Len(strLine) > 0 Then
		If Left(strLine, 1) = "$" Then
			Select Case Split(strLine, "=")(0)
			Case "$info"
				strSystem = Split(strLine, "=")(1)
			Case "$bio"
				strSystemText = ""
			Case "$end"
				Set objTextStream2 = objFSO.CreateTextFile(strObjDir & "sysinfo\" & strSystem & ".htm")
				objTextStream2.Write strSystemText
				objTextStream2.Close
				strSystem = ""
			End Select
		ElseIf Left(strLine, 1) <> "#" Then
			If strSystemText = "" Then
				objSysInfoDictionary(strSystem & ".htm") = strLine
				strSystemText = "<h2>" & HtmlEncode(strLine) & "</h2>" & vbCrlf		
			ElseIf Right(strLine, 1) = ":" Then
				strSystemText = strSystemText & "<h3>" & HtmlEncode(strLine) & "</h3>"
			Else
				strSystemText = strSystemText & HtmlEncode(strLine) & " "
			End If			
		End If
	ElseIf strSystemText <> "" Then
		strSystemText = strSystemText & "<br>" & vbCrlf
	End If
Loop
objTextStream.Close
arrSystems = objSysInfoDictionary.Keys
For i = LBound(arrSystems) To UBound(arrSystems)-1
	Dim nSmallest
	nSmallest = i
	For j = i+1 To UBound(arrSystems)
		If objSysInfoDictionary(arrSystems(nSmallest)) > objSysInfoDictionary(arrSystems(j)) Then
			nSmallest = j
		End if
	Next
	If (nSmallest <> i) Then
		Dim temp
		temp = arrSystems(nSmallest)
		arrSystems(nSmallest) = arrSystems(i)
		arrSystems(i) = temp
	End If
Next

' ---------------------------------------------------------------------------
' Make help project
' ---------------------------------------------------------------------------
Set objTextStream = objFSO.CreateTextFile(strObjDir & "mess.hhp")
objTextStream.WriteLine("[OPTIONS]")
objTextStream.WriteLine("Compiled file=mess.chm")
objTextStream.WriteLine("Contents file=mess.hhc")
objTextStream.WriteLine("Default topic=html\overview.html")
objTextStream.WriteLine("Language=0x409 English (United States)")
objTextStream.WriteLine("Title=MESS Help")
objTextStream.WriteLine("")
objTextStream.Close

' ---------------------------------------------------------------------------
' Make help contents
' ---------------------------------------------------------------------------
Set objTextStream = objFSO.CreateTextFile(strObjDir & "mess.hhc")
objTextStream.WriteLine("<!DOCTYPE HTML PUBLIC ""-//IETF//DTD HTML//EN"">")
objTextStream.WriteLine("<HTML>")
objTextStream.WriteLine("<HEAD>")
objTextStream.WriteLine("</HEAD><BODY>")
objTextStream.WriteLine("<OBJECT type=""text/site properties"">")
objTextStream.WriteLine("<param name=""Window Styles"" value=""0x800625"">")
objTextStream.WriteLine("<param name=""ImageType"" value=""Folder"">")
objTextStream.WriteLine("<param name=""Font"" value=""Arial,8,0"">")
objTextStream.WriteLine("</OBJECT>")
objTextStream.WriteLine("<UL>")

'Help files
AddFile objTextStream,	"mess\windowsui\help",	"html\overview.html",	"Overview"
AddFile objTextStream,	".",					"messnew.txt",			"Whats new"
AddFile objTextStream,	"mess\windowsui\help",	"html\compilewin.html",	"Compiling"
AddImage objTextStream,	"mess\windowsui\help",	"images\messlogo.gif"

' Emulated systems
objTextStream.WriteLine("	<LI> <OBJECT type=""text/sitemap"">")
objTextStream.WriteLine("		<param name=""Name"" value=""Emulated systems"">")
objTextStream.WriteLine("		</OBJECT>")
objTextStream.WriteLine("	<UL>")
For i = LBound(arrSystems) To UBound(arrSystems)
	objTextStream.WriteLine("		<LI> <OBJECT type=""text/sitemap"">")
	objTextStream.WriteLine("			<param name=""Name"" value=""" & objSysInfoDictionary(arrSystems(i)) & """>")
	objTextStream.WriteLine("			<param name=""Local"" value=""sysinfo\" & arrSystems(i) & """>")
	objTextStream.WriteLine("			</OBJECT>")
Next
objTextStream.WriteLine("	</UL>")

objTextStream.WriteLine("</UL>")
objTextStream.WriteLine("</BODY></HTML>")
objTextStream.Close

' ---------------------------------------------------------------------------
' Invoke help compiler
' ---------------------------------------------------------------------------
WScript.CreateObject("WScript.Shell").Run strHHC & " " & strObjDir & "mess.hhp",, True

