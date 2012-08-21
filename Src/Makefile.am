bin_PROGRAMS = LunaSysService

include_HEADERS = LocalePrefsHandler.h Logging.h PrefsDb.h PrefsFactory.h PrefsHandler.h TimePrefsHandler.h Utils.h

LunaSysService_SOURCES = LocalePrefsHandler.cpp Logging.cpp Main.cpp PrefsDb.cpp PrefsFactory.cpp TimePrefsHandler.cpp Utils.cpp

LunaSysService_CXXFLAGS = @GLIB_CFLAGS@ @CJSON_CFLAGS@ @GDBUS_CFLAGS@ @LUNASERVICE_CFLAGS@ @SQLITE_CFLAGS@
LunaSysService_LDADD = @GLIB_LIBS@ @CJSON_LIBS@ @GDBUS_LIBS@ @LUNASERVICE_LIBS@ @SQLITE_LIBS@

#pkgconfigdir = $(libdir)/pkgconfig

