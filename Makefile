#
# Chrome Token Signing Native Host
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#

# This is the Makefile for Windows NMake. See GNUmakefile for OSX/Linux.

!IF !DEFINED(BUILD_NUMBER)
BUILD_NUMBER=1
!ENDIF
!include VERSION.mk
SIGN = signtool sign /v /n "$(SIGNER)" /fd SHA256 /tr http://timestamp.comodoca.com/?td=sha256 /td sha256
EXE = src\release\web-eid.exe
DISTNAME = Web-eID

$(EXE): src\*.h src\*.cpp src\win\*.cpp src\win\*.h src\qt\*.h src\qt\*.cpp
	cd src & make & cd ..
	IF DEFINED SIGNER ($(SIGN) $(EXE))

x64: $(DISTNAME)_$(VERSION)_x64.msi
$(DISTNAME)_$(VERSION)_x64.msi: $(EXE)
	"$(WIX)\bin\candle.exe" -nologo windows\web-eid.wxs -dVERSION=$(VERSIONEX) -dPlatform=x64
	"$(WIX)\bin\light.exe" -nologo -out $(DISTNAME)_$(VERSION)-unsigned_x64.msi web-eid.wixobj -ext WixUIExtension -ext WixUtilExtension -dPlatform=x64
	IF DEFINED SIGNER ($(SIGN) $(DISTNAME)_$(VERSION)-unsigned_x64.msi)
	IF DEFINED SIGNER (ren $(DISTNAME)_$(VERSION)-unsigned_x64.msi $(DISTNAME)_$(VERSION)_x64.msi)

x86: $(DISTNAME)_$(VERSION)_x86.msi
$(DISTNAME)_$(VERSION)_x86.msi: $(EXE)
	"$(WIX)\bin\candle.exe" -nologo windows\web-eid.wxs -dVERSION=$(VERSIONEX) -dPlatform=x86
	"$(WIX)\bin\light.exe" -nologo -out $(DISTNAME)_$(VERSION)-unsigned_x86.msi web-eid.wixobj -ext WixUIExtension -ext WixUtilExtension -dPlatform=x86
	IF DEFINED SIGNER ($(SIGN) $(DISTNAME)_$(VERSION)-unsigned_x86.msi)
	IF DEFINED SIGNER (ren $(DISTNAME)_$(VERSION)-unsigned_x86.msi $(DISTNAME)_$(VERSION)_x86.msi)

pkg: x86 x64

nsis:
	"C:\Program Files (x86)\NSIS\makensis.exe" /nocd /DVERSION=$(VERSION) windows\web-eid.nsi
	timeout /t 1
	IF DEFINED SIGNER ($(SIGN) $(DISTNAME)_$(VERSION)-local.exe)

clean:
	del /f $(EXE)
	del /f *.msi *.exe

test: $(EXE)
	C:\Python27\python.exe tests\pipe-test.py -v
