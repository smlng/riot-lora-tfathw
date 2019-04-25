APPLICATION = lora-tfathw
# set RIOTBASE if not given
RIOTBASE ?= $(CURDIR)/../../RIOT
# this app is device specific
BOARD = b-l072z-lrwan1
# config
TFA_THW_GPIO  ?= GPIO_PIN\(0,0\)
REGION ?= EU868
# add lora stuff
USEPKG += semtech-loramac
USEMODULE += sx1276
# required riot modules
USEMODULE += fmt
USEMODULE += tfa_thw
USEMODULE += xtimer

# enable devel helpers
DEVELHELP=0
# pass config as CFLAGS
CFLAGS += -DREGION_$(REGION)
CFLAGS += -DLORAMAC_ACTIVE_REGION=LORAMAC_REGION_$(REGION)
CFLAGS += -DTFA_THW_PARAM_GPIO=$(TFA_THW_GPIO)

include $(RIOTBASE)/Makefile.include
