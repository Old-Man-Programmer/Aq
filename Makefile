VERSION=0.6
QT=
IMGUI=
BUILD = build$(QT)$(IMGUI)

$(BUILD):
	mkdir -p $(BUILD)

# $(call GIT_DL,<proj>,<site>,<ver>)
GIT_DL = \
    echo "Downloading $2/$1 rev. $3"; \
    if [ -d external/$1 ]; then \
	cd external/$1; git fetch; \
    else \
	git clone $2/$1 external/$1; \
        cd external/$1; \
    fi; \
    git checkout $3

# dl is separate target; otherwise existence of external dir prevents updates
external: dl
dl:
	mkdir -p external
	@$(call GIT_DL,imgui,https://github.com/ocornut,v1.89.6)
	@# Note: the following ref is in the Lib_Only branch
	@#$(call GIT_DL,ImGuiFileDialog,https://github.com/aiekick,356fc97a21cc2a201e2616f7279189a54054fda5)
	@$(call GIT_DL,ImGuiFileDialog,https://github.com/aiekick,v0.6.5)
	@$(call GIT_DL,imgui_markdown,https://github.com/juliettef,61a181bdb83f450f852f7cf5d1282d8cda1c0f57)

.DEFAULT: $(BUILD)
	# Always reconfigure so that correct build is used
	cmake -S . -B $(BUILD) "${CMAKE_FLAGS}" \
	    $(if $(QT),-DUSE_QT=ON) \
	    $(if $(IMGUI),-DUSE_IMGUI=ON -DIMGUI_LIB=)
	$(MAKE) -C $(BUILD) $@

clean:
	rm -rf $(BUILD) build*

distclean: clean
	rm -rf external

archive: clean
	tar zcf ../Aq-$(VERSION).tgz -C .. `cat .files`
