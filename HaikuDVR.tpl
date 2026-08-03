name			$(GUI_TARGET)
version			$(VERSION)-1
architecture	$(ARCH)
summary 		"HaikuDVR"
description 	"HaikuDVR - Native GUI Haiku DVR for HDHomeRun Tuners"
packager		"ablyss <HaikuDVR@epluribusunix.net>"
vendor			"epluribusunix.net Project"
licenses {
	"MIT"
}
copyrights {
	"$(YEAR) ablyss"
}
provides {
	$(GUI_TARGET) = $(VERSION)-1
	libhdhomerun
}
requires {
	haiku
	nlohmann_json
	curl$(is32bit)
	mpv$(is32bit)
	sqlite$(is32bit)
}	
urls {
	"https://github.com/ablyssx74/HaikuDVR"
}
