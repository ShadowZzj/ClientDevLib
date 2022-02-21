#!/bin/sh
set -e
if test "$CONFIGURATION" = "Debug"; then :
  cd /Users/bixiangyang/kepm/kepm_client/ClientDevLib/General/ThirdParty/antiword-0.37/build
  make -f /Users/bixiangyang/kepm/kepm_client/ClientDevLib/General/ThirdParty/antiword-0.37/build/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "Release"; then :
  cd /Users/bixiangyang/kepm/kepm_client/ClientDevLib/General/ThirdParty/antiword-0.37/build
  make -f /Users/bixiangyang/kepm/kepm_client/ClientDevLib/General/ThirdParty/antiword-0.37/build/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "MinSizeRel"; then :
  cd /Users/bixiangyang/kepm/kepm_client/ClientDevLib/General/ThirdParty/antiword-0.37/build
  make -f /Users/bixiangyang/kepm/kepm_client/ClientDevLib/General/ThirdParty/antiword-0.37/build/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "RelWithDebInfo"; then :
  cd /Users/bixiangyang/kepm/kepm_client/ClientDevLib/General/ThirdParty/antiword-0.37/build
  make -f /Users/bixiangyang/kepm/kepm_client/ClientDevLib/General/ThirdParty/antiword-0.37/build/CMakeScripts/ReRunCMake.make
fi

