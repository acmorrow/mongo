buildscripts/scons.py \
  --variables-files=etc/scons/xcode_macosx.vars \
  --link-model=dynamic \
  --install-mode=hygienic \
  --opt=off --dbg=on \ 
  --build-fast-and-loose --implicit-cache \
  --install-action=hardlink \
  ICECC= CCACHE=ccache \
  -j12 \
  test-outcomes
