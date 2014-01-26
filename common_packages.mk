# The default product packages treated as base.mk in sprdroid4.1
PRODUCT_PACKAGES += \
	AudioProfile \
	FMPlayer \
	SprdRamOptimizer \
	VideoWallpaper \
	FileExplorer \
	NoteBook \
	EngineerMode \
	ValidationTools \
	DrmProvider \
	CellBroadcastReceiver \
	SprdQuickSearchBox \
        Carddav-Sync \
        Caldav-Sync.apk
#	libsprddm \

ifeq ($(TARGET_LOWCOST_SUPPORT),true)
    ifneq ($(MULTILANGUAGE_SUPPORT),true)
        PRODUCT_PACKAGES += PinyinIME
    endif
else
    PRODUCT_PACKAGES += PinyinIME
endif
