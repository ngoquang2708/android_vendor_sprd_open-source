# The default product packages treated as base.mk in sprdroid4.1
PRODUCT_PACKAGES += \
	AudioProfile \
	FMPlayer \
	SprdRamOptimizer \
	VideoWallpaper \
	FileExplorer \
	NoteBook \
	SGPS \
	EngineerMode \
	ValidationTools \
	DrmProvider \
	CellBroadcastReceiver \
	SprdQuickSearchBox \
        Carddav-Sync \
        Caldav-Sync.apk\
        libsprd_agps_agent
#	libsprddm \
     
ifeq ($(TARGET_LOWCOST_SUPPORT),true)
    ifneq ($(MULTILANGUAGE_SUPPORT),true)
        PRODUCT_PACKAGES += PinyinIME
    endif
else
    PRODUCT_PACKAGES += PinyinIME
endif
