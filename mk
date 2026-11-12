#!/bin/sh
# Add 'l' to add clang compilation, though it probably will not work.
for l in "" ; do
  for t in n i q; do
    mkdir -p build/$l$t
    (
      test -n "$l" && export CC=clang CXX=clang++
      case $t in
        n) arg= ;;
	i) arg="-DUSE_IMGUI=ON -DIMGUI_LIB=" ;;
	q) arg=-DUSE_QT=ON ;;
      esac
      # IMGUI with Freetype gives:
      # .../imgui_draw.cpp:2629:11: error: ...memset... specified size between 18446744071562067968 and 18446744073709551615 exceeds maximum object size 9223372036854775807
      # It's probably a legitimate error, but I'll ignore for now.
      # g=" -Wno-stringop-overflow -Wno-error=maybe-uninitialized"
      test $l$t = i || g=$g
      # In addition, there's a pragma in imgui that makes clang barf.
      test $l$t = li && g=" -Wno-unknown-warning-option $g"
      echo cmake -S . -B build/$l$t -DCMAKE_C{,XX}_FLAGS=-Wall\ -Wextra\ -Werror"$g" $arg
      cmake -S . -B build/$l$t -DCMAKE_C{,XX}_FLAGS=-Wall\ -Wextra\ -Werror"$g" $arg
    ) || exit 1
    make -j9 -C build/$l$t "$@" || exit 1
  done
done
