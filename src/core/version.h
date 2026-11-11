// The one place the version lives. res/cue.rc, the installer and the update check read it.
#pragma once
#include "build_identity.h"
#define CUE_VERSION_MAJOR 1
#define CUE_VERSION_MINOR 3
#define CUE_VERSION_PATCH 2
#define CUE_VERSION "1.3.2"
#define CUE_VERSION_SHORT "1.3.2"
// The version whose news the home shows: a fix release (1.2.1, 1.2.3) does not bring them back;
// 1.3 has its own (the mini player, controls that fit any window).
#define CUE_NEWS_VERSION "1.3"
// Releases are published on GitHub. The update check reads the latest release of this repo.
#define CUE_UPDATE_REPO "tinypeep/cue"
// The publisher certificate an official update must carry: the thumbprint (the
// SHA-1 of the certificate) of the signing certificate, lowercase hex, no
// spaces. Several may be listed, separated by commas. Empty means the project
// does not sign its releases yet: the update is then checked by its digest
// only, and the log says that it is unsigned.
#define CUE_PUBLISHER_THUMBPRINTS ""
// The project's contact address, shown in About (mailto: link, copied when no mail app answers).
#define CUE_CONTACT_EMAIL "tinypeepstudio@gmail.com"
