include version.mak

TARGET=$(PROJECT)_$(VER).zip

INSTALLDIR=~/Desktop/X-Plane\ 10/Resources/plugins

#FILES=ReadMe.html $(PROJECT)/lin.xpl $(PROJECT)/mac.xpl $(PROJECT)/win.xpl $(PROJECT)/64/lin.xpl $(PROJECT)/64/win.xpl
FILES=ReadMe.html $(PROJECT)/mac.xpl $(PROJECT)/win.xpl $(PROJECT)/64/win.xpl

all:	$(TARGET)

clean:
	rm $(TARGET)

install:	$(TARGET)
	rm -rf $(INSTALLDIR)/$(PROJECT)
	unzip -o -d $(INSTALLDIR) $(TARGET)

$(TARGET):	$(FILES)
	chmod +x $(PROJECT)/*.xpl $(PROJECT)/64/*.xpl
	rm -f $(TARGET)
	zip -MM $(TARGET) $+
