include version.mak

TARGET=$(PROJECT)_$(VER).zip

FILES=ReadMe.html $(PROJECT)/lin.xpl $(PROJECT)/mac.xpl $(PROJECT)/win.xpl $(PROJECT)/64/lin.xpl $(PROJECT)/64/win.xpl

all:	$(TARGET)

clean:
	rm $(TARGET)

$(TARGET):	$(FILES)
	touch $^
	chmod +x $(PROJECT)/*.xpl $(PROJECT)/64/*.xpl
	rm -f $(TARGET)
	zip -MM -o $(TARGET) $+
