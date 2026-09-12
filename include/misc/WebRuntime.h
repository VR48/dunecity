#ifndef WEBRUNTIME_H
#define WEBRUNTIME_H

namespace WebRuntime {

int defaultVideoWidth();
int defaultVideoHeight();
void yieldToBrowser();
void markGameReady();
void syncPersistentFiles();

}

#endif
